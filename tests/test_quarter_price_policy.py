from __future__ import annotations

from datetime import date, datetime, timedelta, timezone
import math
import random
import unittest
from tools.load_guard_policy import LoadGuardPolicy, PhaseSample
from tools.quarter_price_policy import (
    PriceDataState,
    PricePolicyConfig,
    PriceTier,
    QuarterPrice,
    build_price_schedule,
    decide_price_limit,
    expected_quarter_starts,
    stockholm_local_time,
    to_external_block,
)

from tests.test_load_guard_policy import test_config


def prices_for_day(day: date) -> list[QuarterPrice]:
    return [
        QuarterPrice(starts_at=starts_at, total=float(index))
        for index, starts_at in enumerate(expected_quarter_starts(day))
    ]


class QuarterPricePolicyTests(unittest.TestCase):
    def test_v1_defaults_have_no_off_and_exactly_fifteen_half_quarters(self) -> None:
        config = PricePolicyConfig()
        schedule = build_price_schedule(date(2026, 10, 23), prices_for_day(date(2026, 10, 23)))

        self.assertEqual(0, config.off_quarters)
        self.assertEqual(15, config.half_quarters)
        self.assertEqual((6, 7, 8, 9), config.inactive_months)
        self.assertEqual(96, len(schedule.slots))
        self.assertEqual(0, sum(slot.tier is PriceTier.OFF for slot in schedule.slots))
        self.assertEqual(15, sum(slot.tier is PriceTier.HALF for slot in schedule.slots))
        self.assertEqual(81, sum(slot.tier is PriceTier.FULL for slot in schedule.slots))
        highest = sorted(schedule.slots, key=lambda slot: slot.total, reverse=True)[:15]
        self.assertTrue(all(slot.tier is PriceTier.HALF for slot in highest))

    def test_default_summer_pause_is_transparent_at_month_boundaries(self) -> None:
        cases = (
            (date(2026, 5, 31), PriceDataState.READY),
            (date(2026, 6, 1), PriceDataState.SEASONAL_PAUSE),
            (date(2026, 9, 30), PriceDataState.SEASONAL_PAUSE),
            (date(2026, 10, 1), PriceDataState.READY),
        )

        for day, expected_state in cases:
            with self.subTest(day=day):
                schedule = build_price_schedule(day, prices_for_day(day))
                half_slot = next(
                    slot for slot in schedule.slots if slot.tier is PriceTier.HALF
                )
                decision = decide_price_limit(schedule, half_slot.starts_at)

                self.assertEqual(expected_state, decision.data_state)
                if expected_state is PriceDataState.SEASONAL_PAUSE:
                    self.assertEqual(PriceTier.FULL, decision.tier)
                    self.assertEqual(0, decision.minimum_blocked_stages)
                    self.assertEqual(PriceTier.HALF, decision.scheduled_tier)
                else:
                    self.assertEqual(PriceTier.HALF, decision.tier)
                    self.assertEqual(1, decision.minimum_blocked_stages)

    def test_all_default_inactive_months_are_paused(self) -> None:
        for month in (6, 7, 8, 9):
            with self.subTest(month=month):
                day = date(2026, month, 15)
                schedule = build_price_schedule(day, prices_for_day(day))
                decision = decide_price_limit(schedule, schedule.slots[-1].starts_at)

                self.assertEqual(PriceDataState.SEASONAL_PAUSE, decision.data_state)
                self.assertEqual(0, decision.minimum_blocked_stages)

    def test_summer_schedule_remains_ranked_and_visible_while_control_is_paused(self) -> None:
        day = date(2026, 7, 15)
        schedule = build_price_schedule(day, prices_for_day(day))
        half_slot = next(slot for slot in schedule.slots if slot.tier is PriceTier.HALF)
        decision = decide_price_limit(schedule, half_slot.starts_at)

        self.assertEqual(0, sum(slot.tier is PriceTier.OFF for slot in schedule.slots))
        self.assertEqual(15, sum(slot.tier is PriceTier.HALF for slot in schedule.slots))
        self.assertEqual(PriceDataState.SEASONAL_PAUSE, decision.data_state)
        self.assertEqual(PriceTier.FULL, decision.tier)
        self.assertEqual(PriceTier.HALF, decision.scheduled_tier)
        self.assertEqual(half_slot.total, decision.current_price_total)
        self.assertEqual(half_slot.rank, decision.rank)
        self.assertEqual(schedule.currency, decision.currency)
        self.assertIsNone(decision.valid_until)
        self.assertEqual(
            0,
            to_external_block(decision, half_slot.starts_at, 100.0).active_count(100.0),
        )

    def test_off_quarters_remain_available_for_future_shadow_simulation(self) -> None:
        day = date(2026, 10, 23)
        config = PricePolicyConfig(off_quarters=5, half_quarters=10)
        schedule = build_price_schedule(day, prices_for_day(day), config)

        self.assertEqual(5, sum(slot.tier is PriceTier.OFF for slot in schedule.slots))
        self.assertEqual(10, sum(slot.tier is PriceTier.HALF for slot in schedule.slots))
        self.assertEqual(81, sum(slot.tier is PriceTier.FULL for slot in schedule.slots))
        highest = sorted(schedule.slots, key=lambda slot: slot.total, reverse=True)[:5]
        self.assertTrue(all(slot.tier is PriceTier.OFF for slot in highest))

    def test_seasonal_pause_months_are_configurable_or_can_be_disabled(self) -> None:
        june = date(2026, 6, 15)
        july = date(2026, 7, 15)
        september = date(2026, 9, 15)
        custom = PricePolicyConfig(inactive_months=(7, 8))

        june_schedule = build_price_schedule(june, prices_for_day(june), custom)
        july_schedule = build_price_schedule(july, prices_for_day(july), custom)
        september_schedule = build_price_schedule(
            september, prices_for_day(september), custom
        )
        always_active = build_price_schedule(
            july,
            prices_for_day(july),
            PricePolicyConfig(inactive_months=()),
        )

        self.assertEqual(
            PriceDataState.READY,
            decide_price_limit(june_schedule, june_schedule.slots[-1].starts_at).data_state,
        )
        self.assertEqual(
            PriceDataState.SEASONAL_PAUSE,
            decide_price_limit(july_schedule, july_schedule.slots[-1].starts_at).data_state,
        )
        self.assertEqual(
            PriceDataState.READY,
            decide_price_limit(
                september_schedule, september_schedule.slots[-1].starts_at
            ).data_state,
        )
        self.assertEqual(
            PriceDataState.READY,
            decide_price_limit(always_active, always_active.slots[-1].starts_at).data_state,
        )

    def test_input_order_does_not_affect_schedule(self) -> None:
        day = date(2026, 10, 23)
        entries = prices_for_day(day)
        shuffled = list(entries)
        random.Random(42).shuffle(shuffled)
        ordered = build_price_schedule(day, entries)
        unordered = build_price_schedule(day, shuffled)

        self.assertEqual(ordered, unordered)

    def test_equal_prices_use_earliest_utc_start_as_tie_break(self) -> None:
        day = date(2026, 10, 23)
        entries = [QuarterPrice(starts_at=start, total=1.0) for start in expected_quarter_starts(day)]
        schedule = build_price_schedule(day, reversed(entries))

        self.assertEqual(list(range(1, 97)), [slot.rank for slot in schedule.slots])
        self.assertTrue(all(slot.tier is PriceTier.HALF for slot in schedule.slots[:15]))
        self.assertTrue(all(slot.tier is PriceTier.FULL for slot in schedule.slots[15:]))

    def test_negative_prices_are_ranked_numerically(self) -> None:
        day = date(2026, 10, 23)
        entries = [
            QuarterPrice(starts_at=start, total=-100.0 + index)
            for index, start in enumerate(expected_quarter_starts(day))
        ]
        schedule = build_price_schedule(day, entries)

        self.assertEqual(PriceTier.HALF, schedule.slots[-1].tier)
        self.assertEqual(1, schedule.slots[-1].rank)
        self.assertEqual(PriceTier.FULL, schedule.slots[0].tier)

    def test_spring_dst_day_has_92_real_quarters(self) -> None:
        day = date(2026, 3, 29)
        schedule = build_price_schedule(day, prices_for_day(day))
        local_starts = [stockholm_local_time(slot.starts_at) for slot in schedule.slots]

        self.assertEqual(92, len(schedule.slots))
        self.assertFalse(any(value.hour == 2 for value in local_starts))
        self.assertEqual(0, sum(slot.tier is PriceTier.OFF for slot in schedule.slots))
        self.assertEqual(15, sum(slot.tier is PriceTier.HALF for slot in schedule.slots))

    def test_fall_dst_day_has_100_real_quarters_and_both_offsets(self) -> None:
        day = date(2026, 10, 25)
        schedule = build_price_schedule(day, prices_for_day(day))
        two_oclock_offsets = {
            stockholm_local_time(slot.starts_at).utcoffset()
            for slot in schedule.slots
            if stockholm_local_time(slot.starts_at).hour == 2
        }

        self.assertEqual(100, len(schedule.slots))
        self.assertEqual({timedelta(hours=1), timedelta(hours=2)}, two_oclock_offsets)
        self.assertEqual(0, sum(slot.tier is PriceTier.OFF for slot in schedule.slots))
        self.assertEqual(15, sum(slot.tier is PriceTier.HALF for slot in schedule.slots))

    def test_incomplete_duplicate_or_non_contiguous_day_is_rejected(self) -> None:
        day = date(2026, 10, 23)
        entries = prices_for_day(day)
        cases = (
            entries[:-1],
            entries[:-1] + [entries[0]],
            entries[:-1]
            + [QuarterPrice(entries[-1].starts_at + timedelta(minutes=5), 1.0)],
        )
        for invalid in cases:
            with self.subTest(invalid=invalid[-1]):
                with self.assertRaises(ValueError):
                    build_price_schedule(day, invalid)

    def test_naive_timestamp_and_non_finite_price_are_rejected(self) -> None:
        day = date(2026, 10, 23)
        entries = prices_for_day(day)
        for replacement in (
            QuarterPrice(entries[0].starts_at.replace(tzinfo=None), 1.0),
            QuarterPrice(entries[0].starts_at, math.nan),
            QuarterPrice(entries[0].starts_at, math.inf),
        ):
            invalid = list(entries)
            invalid[0] = replacement
            with self.subTest(replacement=replacement):
                with self.assertRaises(ValueError):
                    build_price_schedule(day, invalid)

    def test_bypass_missing_naive_and_explicitly_untrusted_time_are_transparent(self) -> None:
        day = date(2026, 10, 23)
        schedule = build_price_schedule(day, prices_for_day(day))
        now = schedule.slots[-1].starts_at

        bypass = decide_price_limit(schedule, now, enabled=False)
        missing = decide_price_limit(None, now)
        naive_time = decide_price_limit(schedule, now.replace(tzinfo=None))
        untrusted_time = decide_price_limit(schedule, now, time_trusted=False)
        outside = decide_price_limit(schedule, now + timedelta(days=1))

        for decision in (bypass, missing, naive_time, untrusted_time, outside):
            self.assertEqual(PriceTier.FULL, decision.tier)
            self.assertEqual(0, decision.minimum_blocked_stages)
            self.assertIsNone(decision.valid_until)
        self.assertEqual(PriceDataState.BYPASS, bypass.data_state)
        self.assertEqual(PriceDataState.MISSING, missing.data_state)
        self.assertEqual(PriceDataState.INVALID_TIME, naive_time.data_state)
        self.assertEqual(PriceDataState.INVALID_TIME, untrusted_time.data_state)
        self.assertEqual(PriceDataState.OUTSIDE_DAY, outside.data_state)

    def test_quarter_boundary_selects_new_slot_without_a_gap(self) -> None:
        day = date(2026, 10, 23)
        schedule = build_price_schedule(day, prices_for_day(day))
        boundary = schedule.slots[10].starts_at
        before = decide_price_limit(schedule, boundary - timedelta(microseconds=1))
        at_boundary = decide_price_limit(schedule, boundary)

        self.assertEqual(schedule.slots[9].rank, before.rank)
        self.assertEqual(schedule.slots[10].rank, at_boundary.rank)
        self.assertEqual(boundary, before.valid_until)

    def test_local_day_boundaries_do_not_follow_the_utc_date(self) -> None:
        day = date(2026, 10, 23)
        schedule = build_price_schedule(day, prices_for_day(day))
        first_start = schedule.slots[0].starts_at
        final_end = schedule.slots[-1].ends_at

        self.assertEqual(date(2026, 10, 22), first_start.date())
        self.assertEqual(PriceDataState.READY, decide_price_limit(schedule, first_start).data_state)
        self.assertEqual(
            PriceDataState.OUTSIDE_DAY,
            decide_price_limit(schedule, first_start - timedelta(microseconds=1)).data_state,
        )
        self.assertEqual(
            PriceDataState.OUTSIDE_DAY,
            decide_price_limit(schedule, final_end).data_state,
        )

        next_day = day + timedelta(days=1)
        next_schedule = build_price_schedule(next_day, prices_for_day(next_day))
        self.assertEqual(
            PriceDataState.READY,
            decide_price_limit(next_schedule, final_end).data_state,
        )

    def test_both_repeated_fall_dst_hours_select_unique_slots(self) -> None:
        day = date(2026, 10, 25)
        schedule = build_price_schedule(day, prices_for_day(day))
        repeated_two_oclock = [
            slot
            for slot in schedule.slots
            if stockholm_local_time(slot.starts_at).hour == 2
            and stockholm_local_time(slot.starts_at).minute == 0
        ]

        self.assertEqual(2, len(repeated_two_oclock))
        first = decide_price_limit(schedule, repeated_two_oclock[0].starts_at)
        second = decide_price_limit(schedule, repeated_two_oclock[1].starts_at)
        self.assertEqual(PriceDataState.READY, first.data_state)
        self.assertEqual(PriceDataState.READY, second.data_state)
        self.assertNotEqual(first.rank, second.rank)

    def test_external_block_expires_at_original_quarter_end(self) -> None:
        day = date(2026, 10, 23)
        entries = [QuarterPrice(start, 0.0) for start in expected_quarter_starts(day)]
        schedule = build_price_schedule(
            day, entries, PricePolicyConfig(off_quarters=1, half_quarters=14)
        )
        decision = decide_price_limit(schedule, schedule.slots[0].starts_at + timedelta(minutes=4))
        block = to_external_block(
            decision,
            schedule.slots[0].starts_at + timedelta(minutes=4),
            now_monotonic_s=100.0,
        )

        self.assertEqual(2, block.minimum_blocked_stages)
        self.assertAlmostEqual(160.0, block.valid_until_s)
        self.assertEqual(2, block.active_count(159.999))
        self.assertEqual(0, block.active_count(160.0))

    def test_external_block_is_shorter_when_quarter_ends_soon(self) -> None:
        day = date(2026, 10, 23)
        entries = [QuarterPrice(start, 0.0) for start in expected_quarter_starts(day)]
        schedule = build_price_schedule(
            day, entries, PricePolicyConfig(off_quarters=1, half_quarters=14)
        )
        now = schedule.slots[0].ends_at - timedelta(seconds=3)
        block = to_external_block(decide_price_limit(schedule, now), now, 50.0)

        self.assertAlmostEqual(53.0, block.valid_until_s)

    def test_half_price_limit_blocks_one_stage_and_load_can_block_two(self) -> None:
        day = date(2026, 10, 23)
        entries = [QuarterPrice(start, 0.0) for start in expected_quarter_starts(day)]
        schedule = build_price_schedule(day, entries)
        half_slot = next(slot for slot in schedule.slots if slot.tier is PriceTier.HALF)
        now = half_slot.starts_at + timedelta(minutes=1)
        block = to_external_block(decide_price_limit(schedule, now), now, 0.0)
        policy = LoadGuardPolicy(test_config(shed_delay_s=(0.0, 0.0)))

        priced = policy.update(0.0, PhaseSample(current_a=(10.0, 10.0, 10.0)), block)
        overloaded = policy.update(1.0, PhaseSample(current_a=(24.0, 10.0, 10.0)), block)

        self.assertEqual(1, priced.blocked_stages)
        self.assertEqual(2, overloaded.blocked_stages)

    def test_price_expiry_with_live_data_never_forces_restore_without_headroom(self) -> None:
        day = date(2026, 10, 23)
        entries = [QuarterPrice(start, 0.0) for start in expected_quarter_starts(day)]
        schedule = build_price_schedule(
            day, entries, PricePolicyConfig(off_quarters=5, half_quarters=10)
        )
        off_slot = next(slot for slot in schedule.slots if slot.tier is PriceTier.OFF)
        now = off_slot.starts_at + timedelta(minutes=14, seconds=59)
        block = to_external_block(decide_price_limit(schedule, now), now, 0.0)
        policy = LoadGuardPolicy(test_config())

        blocked = policy.update(0.0, PhaseSample(current_a=(10.0, 10.0, 10.0)), block)
        still_blocked = policy.update(
            2.0, PhaseSample(current_a=(16.0, 15.0, 14.0)), block
        )

        self.assertEqual(2, blocked.blocked_stages)
        self.assertEqual(2, still_blocked.blocked_stages)

    def test_expired_price_lease_with_stale_phase_data_returns_to_coala_only(self) -> None:
        day = date(2026, 10, 23)
        entries = [QuarterPrice(start, 0.0) for start in expected_quarter_starts(day)]
        schedule = build_price_schedule(
            day, entries, PricePolicyConfig(off_quarters=5, half_quarters=10)
        )
        off_slot = next(slot for slot in schedule.slots if slot.tier is PriceTier.OFF)
        now = off_slot.starts_at + timedelta(minutes=14, seconds=59)
        block = to_external_block(decide_price_limit(schedule, now), now, 0.0)
        policy = LoadGuardPolicy(test_config())

        policy.update(0.0, PhaseSample(current_a=(10.0, 10.0, 10.0)), block)
        transparent = policy.update(
            2.0,
            PhaseSample(current_a=(24.0, 10.0, 10.0), age_s=20.0),
            block,
        )

        self.assertEqual(0, transparent.blocked_stages)
        self.assertTrue(transparent.allow_re1)
        self.assertTrue(transparent.allow_re2)

    def test_invalid_config_is_rejected(self) -> None:
        day = date(2026, 10, 23)
        entries = prices_for_day(day)
        for config in (
            PricePolicyConfig(off_quarters=-1),
            PricePolicyConfig(half_quarters=-1),
            PricePolicyConfig(off_quarters=100),
            PricePolicyConfig(timezone_name="Not/AZone"),
            PricePolicyConfig(expected_currency="sek"),
            PricePolicyConfig(inactive_months=[6, 7, 8, 9]),
            PricePolicyConfig(inactive_months=(0,)),
            PricePolicyConfig(inactive_months=(13,)),
            PricePolicyConfig(inactive_months=(True,)),
            PricePolicyConfig(inactive_months=(6, 6)),
        ):
            with self.subTest(config=config):
                with self.assertRaises(ValueError):
                    build_price_schedule(day, entries, config)

    def test_mixed_or_unexpected_currency_is_rejected(self) -> None:
        day = date(2026, 10, 23)
        entries = prices_for_day(day)
        entries[0] = QuarterPrice(entries[0].starts_at, entries[0].total, "EUR")

        with self.assertRaises(ValueError):
            build_price_schedule(day, entries)


if __name__ == "__main__":
    unittest.main()
