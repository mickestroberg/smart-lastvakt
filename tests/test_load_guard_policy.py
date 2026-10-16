from __future__ import annotations

import math
import unittest

from tools.load_guard_policy import (
    DataState,
    DecisionReason,
    ExternalBlock,
    LoadGuardPolicy,
    PhaseSample,
    PolicyConfig,
)


def test_config(**overrides: object) -> PolicyConfig:
    values: dict[str, object] = {
        "trip_current_a": 23.0,
        "restore_margin_a": 1.0,
        "stage_current_a": (6.5, 6.5),
        "shed_delay_s": (2.0, 3.0),
        "restore_delay_s": (4.0, 4.0),
        "minimum_change_interval_s": 1.0,
        "maximum_sample_age_s": 10.0,
    }
    values.update(overrides)
    return PolicyConfig(**values)  # type: ignore[arg-type]


class LoadGuardPolicyTests(unittest.TestCase):
    def test_live_values_below_limit_allow_both_stages(self) -> None:
        decision = LoadGuardPolicy(test_config()).update(
            0.0, PhaseSample(current_a=(10.0, 12.0, 11.0))
        )
        self.assertEqual(DataState.LIVE, decision.data_state)
        self.assertEqual(0, decision.blocked_stages)
        self.assertTrue(decision.allow_re1)
        self.assertTrue(decision.allow_re2)
        self.assertEqual((DecisionReason.LIVE_NORMAL,), decision.reasons)

    def test_highest_phase_sheds_re1_after_delay(self) -> None:
        policy = LoadGuardPolicy(test_config())
        waiting = policy.update(0.0, PhaseSample(current_a=(12.0, 23.5, 11.0)))
        shed = policy.update(2.0, PhaseSample(current_a=(12.0, 23.5, 11.0)))

        self.assertIn(DecisionReason.PHASE_OVERLOAD_WAIT, waiting.reasons)
        self.assertEqual(1, shed.blocked_stages)
        self.assertFalse(shed.allow_re1)
        self.assertTrue(shed.allow_re2)
        self.assertIn(DecisionReason.PHASE_SHED, shed.reasons)

    def test_persistent_overload_sheds_re2_as_second_stage(self) -> None:
        policy = LoadGuardPolicy(test_config())
        policy.update(0.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))
        policy.update(2.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))
        policy.update(3.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))
        decision = policy.update(6.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))

        self.assertEqual(2, decision.blocked_stages)
        self.assertFalse(decision.allow_re1)
        self.assertFalse(decision.allow_re2)

    def test_restores_re2_before_re1_when_headroom_is_stable(self) -> None:
        policy = LoadGuardPolicy(test_config())
        policy.update(0.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))
        policy.update(2.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))
        policy.update(3.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))
        policy.update(6.0, PhaseSample(current_a=(23.5, 10.0, 10.0)))

        policy.update(7.0, PhaseSample(current_a=(8.0, 8.0, 8.0)))
        first_restore = policy.update(11.0, PhaseSample(current_a=(8.0, 8.0, 8.0)))
        self.assertEqual(1, first_restore.blocked_stages)
        self.assertFalse(first_restore.allow_re1)
        self.assertTrue(first_restore.allow_re2)

        policy.update(12.0, PhaseSample(current_a=(14.5, 14.0, 13.5)))
        second_restore = policy.update(16.0, PhaseSample(current_a=(14.5, 14.0, 13.5)))
        self.assertEqual(0, second_restore.blocked_stages)
        self.assertTrue(second_restore.allow_re1)
        self.assertTrue(second_restore.allow_re2)

    def test_does_not_restore_without_current_headroom_for_whole_stage(self) -> None:
        policy = LoadGuardPolicy(test_config())
        policy.update(0.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))
        policy.update(2.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))
        decision = policy.update(20.0, PhaseSample(current_a=(16.0, 15.0, 14.0)))

        self.assertEqual(1, decision.blocked_stages)
        self.assertAlmostEqual(22.5, decision.predicted_restore_current_a or 0.0)
        self.assertIn(DecisionReason.INSUFFICIENT_HEADROOM, decision.reasons)

    def test_exact_trip_value_is_not_an_overload(self) -> None:
        policy = LoadGuardPolicy(test_config())
        decision = policy.update(0.0, PhaseSample(current_a=(23.0, 10.0, 10.0)))
        self.assertEqual(0, decision.blocked_stages)
        self.assertNotIn(DecisionReason.PHASE_OVERLOAD_WAIT, decision.reasons)

    def test_simulated_data_never_keeps_phase_blocking_active(self) -> None:
        policy = LoadGuardPolicy(test_config())
        policy.update(0.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))
        policy.update(2.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))
        decision = policy.update(
            3.0, PhaseSample(current_a=(24.0, 10.0, 10.0), simulated=True)
        )

        self.assertEqual(DataState.SIMULATED, decision.data_state)
        self.assertEqual(0, decision.blocked_stages)
        self.assertTrue(decision.allow_re1)
        self.assertTrue(decision.allow_re2)
        self.assertIn(DecisionReason.SIMULATED_DATA, decision.reasons)

    def test_missing_stale_and_invalid_data_are_transparent(self) -> None:
        cases = (
            (PhaseSample(available=False), DataState.MISSING),
            (PhaseSample(current_a=(10.0, 10.0, 10.0), age_s=10.1), DataState.STALE),
            (PhaseSample(current_a=(math.nan, 10.0, 10.0)), DataState.INVALID),
            (PhaseSample(current_a=(-1.0, 10.0, 10.0)), DataState.INVALID),
            (PhaseSample(current_a=(10.0, 10.0)), DataState.INVALID),  # type: ignore[arg-type]
        )
        for sample, expected_state in cases:
            with self.subTest(expected_state=expected_state, sample=sample):
                decision = LoadGuardPolicy(test_config()).update(0.0, sample)
                self.assertEqual(expected_state, decision.data_state)
                self.assertEqual(0, decision.blocked_stages)
                self.assertTrue(decision.allow_re1)
                self.assertTrue(decision.allow_re2)

    def test_external_request_can_block_but_expiry_does_not_force_restore(self) -> None:
        policy = LoadGuardPolicy(test_config())
        request = ExternalBlock(minimum_blocked_stages=1, valid_until_s=10.0)
        blocked = policy.update(0.0, PhaseSample(current_a=(12.0, 11.0, 10.0)), request)
        self.assertEqual(1, blocked.blocked_stages)
        self.assertIn(DecisionReason.EXTERNAL_SHED, blocked.reasons)

        insufficient = policy.update(10.0, PhaseSample(current_a=(16.0, 15.0, 14.0)), request)
        self.assertEqual(1, insufficient.blocked_stages)
        self.assertIn(DecisionReason.INSUFFICIENT_HEADROOM, insufficient.reasons)

        policy.update(11.0, PhaseSample(current_a=(10.0, 9.0, 8.0)), request)
        restored = policy.update(15.0, PhaseSample(current_a=(10.0, 9.0, 8.0)), request)
        self.assertEqual(0, restored.blocked_stages)
        self.assertIn(DecisionReason.RESTORED_STAGE, restored.reasons)

    def test_stale_data_keeps_only_an_unexpired_external_block(self) -> None:
        policy = LoadGuardPolicy(test_config())
        request = ExternalBlock(minimum_blocked_stages=1, valid_until_s=10.0)
        decision = policy.update(
            1.0,
            PhaseSample(current_a=(24.0, 10.0, 10.0), age_s=20.0),
            request,
        )
        self.assertEqual(DataState.STALE, decision.data_state)
        self.assertEqual(1, decision.blocked_stages)
        self.assertFalse(decision.allow_re1)
        self.assertTrue(decision.allow_re2)

        expired = policy.update(
            10.0,
            PhaseSample(current_a=(24.0, 10.0, 10.0), age_s=20.0),
            request,
        )
        self.assertEqual(0, expired.blocked_stages)

    def test_rejects_non_monotonic_time(self) -> None:
        policy = LoadGuardPolicy(test_config())
        policy.update(2.0, PhaseSample(current_a=(10.0, 10.0, 10.0)))
        with self.assertRaises(ValueError):
            policy.update(1.0, PhaseSample(current_a=(10.0, 10.0, 10.0)))

    def test_minimum_interval_prevents_back_to_back_stage_changes(self) -> None:
        policy = LoadGuardPolicy(
            test_config(shed_delay_s=(0.0, 0.0), minimum_change_interval_s=5.0)
        )
        first = policy.update(0.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))
        waiting = policy.update(1.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))
        second = policy.update(5.0, PhaseSample(current_a=(24.0, 10.0, 10.0)))

        self.assertEqual(1, first.blocked_stages)
        self.assertEqual(1, waiting.blocked_stages)
        self.assertIn(DecisionReason.MINIMUM_CHANGE_INTERVAL, waiting.reasons)
        self.assertEqual(2, second.blocked_stages)

    def test_rejects_incomplete_stage_configuration(self) -> None:
        with self.assertRaises(ValueError):
            LoadGuardPolicy(test_config(stage_current_a=(6.5,)))  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
