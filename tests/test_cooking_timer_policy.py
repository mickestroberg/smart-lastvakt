from __future__ import annotations

import unittest

from tools.cooking_timer_policy import (
    MAXIMUM_MS,
    CookingTimerBank,
    TimerState,
)


class CookingTimerPolicyTests(unittest.TestCase):
    def test_four_channels_start_idle_and_zero_cannot_start(self) -> None:
        timers = CookingTimerBank()
        self.assertEqual(4, len(timers.slots))
        self.assertTrue(all(slot.state is TimerState.IDLE for slot in timers.slots))
        self.assertFalse(timers.toggle(0, 1000))

    def test_countdown_uses_an_absolute_monotonic_deadline(self) -> None:
        timers = CookingTimerBank()
        timers.adjust(0, 90_000, 1_000)
        self.assertTrue(timers.toggle(0, 1_000))
        self.assertEqual(45_000, timers.remaining(0, 46_000))
        self.assertEqual(0, timers.update(90_999))
        self.assertEqual(1, timers.update(91_000))
        self.assertEqual(TimerState.FINISHED, timers.slots[0].state)

    def test_pause_freezes_time_and_resume_sets_a_new_deadline(self) -> None:
        timers = CookingTimerBank()
        timers.adjust(0, 60_000, 0)
        timers.toggle(0, 1_000)
        timers.toggle(0, 21_000)
        self.assertEqual(40_000, timers.remaining(0, 50_000))
        self.assertEqual(TimerState.PAUSED, timers.slots[0].state)
        timers.toggle(0, 100_000)
        self.assertEqual(20_000, timers.remaining(0, 120_000))
        self.assertEqual(1, timers.update(140_000))

    def test_multiple_channels_finish_independently(self) -> None:
        timers = CookingTimerBank()
        for index in range(4):
            timers.adjust(index, (index + 1) * 10_000, 0)
            timers.toggle(index, 0)
        self.assertEqual(0b0011, timers.update(20_000))
        self.assertEqual(0b0011, timers.finished_mask())
        self.assertEqual(TimerState.RUNNING, timers.slots[2].state)
        self.assertEqual(0, timers.update(20_001))

    def test_alarm_latches_until_acknowledged(self) -> None:
        timers = CookingTimerBank()
        timers.adjust(1, 1_000, 0)
        timers.toggle(1, 0)
        timers.update(2_000)
        self.assertEqual(0b0010, timers.finished_mask())
        self.assertEqual(0, timers.update(3_000))
        timers.acknowledge_finished()
        self.assertEqual(0, timers.finished_mask())
        self.assertEqual(TimerState.IDLE, timers.slots[1].state)

    def test_reset_running_channel_prevents_a_late_alarm(self) -> None:
        timers = CookingTimerBank()
        timers.adjust(0, 1_000, 0)
        timers.toggle(0, 0)
        timers.reset(0)
        self.assertEqual(0, timers.update(10_000))

    def test_adjustment_is_blocked_while_running_and_clamped(self) -> None:
        timers = CookingTimerBank()
        timers.adjust(0, MAXIMUM_MS * 2, 0)
        self.assertEqual(MAXIMUM_MS, timers.remaining(0, 0))
        timers.toggle(0, 0)
        self.assertFalse(timers.adjust(0, -60_000, 0))
        timers.toggle(0, 1_000)
        timers.adjust(0, -MAXIMUM_MS * 2, 1_000)
        self.assertEqual(0, timers.remaining(0, 1_000))
        self.assertEqual(TimerState.IDLE, timers.slots[0].state)

    def test_acknowledging_does_not_touch_running_channels(self) -> None:
        timers = CookingTimerBank()
        timers.adjust(0, 1_000, 0)
        timers.adjust(1, 10_000, 0)
        timers.toggle(0, 0)
        timers.toggle(1, 0)
        timers.update(1_000)
        timers.acknowledge_finished()
        self.assertEqual(TimerState.IDLE, timers.slots[0].state)
        self.assertEqual(TimerState.RUNNING, timers.slots[1].state)

    def test_invalid_channel_indexes_are_ignored(self) -> None:
        timers = CookingTimerBank()
        before = [TimerState(slot.state) for slot in timers.slots]
        self.assertEqual(0, timers.remaining(-1, 0))
        self.assertEqual(0, timers.remaining(4, 0))
        self.assertFalse(timers.adjust(-1, 60_000, 0))
        self.assertFalse(timers.toggle(4, 0))
        timers.reset(99)
        self.assertEqual(before, [slot.state for slot in timers.slots])


if __name__ == "__main__":
    unittest.main()
