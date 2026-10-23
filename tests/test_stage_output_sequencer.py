from __future__ import annotations

import unittest

from tools.stage_output_sequencer import (
    Authority,
    ChannelState,
    OutputSnapshot,
    SequencerConfig,
    SequencerState,
    StageCommand,
    StageOutputSequencer,
)


def permit_all(at_s: float) -> OutputSnapshot:
    return OutputSnapshot(ChannelState.PERMIT, ChannelState.PERMIT, at_s)


def block_re1(at_s: float) -> OutputSnapshot:
    return OutputSnapshot(ChannelState.BLOCK, ChannelState.PERMIT, at_s)


def block_all(at_s: float) -> OutputSnapshot:
    return OutputSnapshot(ChannelState.BLOCK, ChannelState.BLOCK, at_s)


def invalid_order(at_s: float) -> OutputSnapshot:
    return OutputSnapshot(ChannelState.PERMIT, ChannelState.BLOCK, at_s)


def unknown(at_s: float) -> OutputSnapshot:
    return OutputSnapshot(observed_at_s=at_s)


def sequencer() -> StageOutputSequencer:
    return StageOutputSequencer(
        SequencerConfig(
            minimum_change_interval_s=2.0,
            feedback_timeout_s=3.0,
            settle_time_s=0.5,
            maximum_snapshot_age_s=1.0,
        )
    )


class StageOutputSequencerTests(unittest.TestCase):
    def test_boot_adopts_valid_readback_without_command(self) -> None:
        for snapshot, expected in (
            (permit_all(0.0), 0),
            (block_re1(0.0), 1),
            (block_all(0.0), 2),
        ):
            with self.subTest(snapshot=snapshot):
                decision = sequencer().update(0.0, expected, Authority.SMART, snapshot)
                self.assertEqual(SequencerState.READY, decision.state)
                self.assertEqual(expected, decision.observed_blocked_stages)
                self.assertIsNone(decision.command)

    def test_zero_to_two_is_re1_then_re2_with_feedback_and_interval(self) -> None:
        policy = sequencer()
        policy.update(0.0, 0, Authority.SMART, permit_all(0.0))
        first = policy.update(0.5, 2, Authority.SMART, permit_all(0.5))
        acknowledged = policy.update(0.6, 2, Authority.SMART, block_re1(0.6))
        too_soon = policy.update(1.9, 2, Authority.SMART, block_re1(1.9))
        second = policy.update(2.5, 2, Authority.SMART, block_re1(2.5))

        self.assertEqual(StageCommand.BLOCK_RE1, first.command)
        self.assertEqual(SequencerState.READY, acknowledged.state)
        self.assertIsNone(too_soon.command)
        self.assertEqual(StageCommand.BLOCK_RE2, second.command)

    def test_two_to_zero_is_re2_then_re1(self) -> None:
        policy = sequencer()
        policy.update(0.0, 2, Authority.SMART, block_all(0.0))
        first = policy.update(0.5, 0, Authority.SMART, block_all(0.5))
        policy.update(0.6, 0, Authority.SMART, block_re1(0.6))
        second = policy.update(2.5, 0, Authority.SMART, block_re1(2.5))

        self.assertEqual(StageCommand.PERMIT_RE2, first.command)
        self.assertEqual(StageCommand.PERMIT_RE1, second.command)

    def test_fast_feedback_does_not_bypass_settle_time(self) -> None:
        policy = StageOutputSequencer(
            SequencerConfig(
                0.0,
                feedback_timeout_s=3.0,
                settle_time_s=1.0,
                maximum_snapshot_age_s=1.0,
            )
        )
        policy.update(0.0, 0, Authority.SMART, permit_all(0.0))
        policy.update(1.0, 2, Authority.SMART, permit_all(1.0))
        policy.update(1.01, 2, Authority.SMART, block_re1(1.01))
        waiting = policy.update(2.0, 2, Authority.SMART, block_re1(2.0))
        ready = policy.update(2.02, 2, Authority.SMART, block_re1(2.02))

        self.assertIsNone(waiting.command)
        self.assertEqual(StageCommand.BLOCK_RE2, ready.command)

    def test_changed_target_waits_for_pending_ack_then_reverses_safely(self) -> None:
        policy = sequencer()
        policy.update(0.0, 0, Authority.SMART, permit_all(0.0))
        policy.update(1.0, 2, Authority.SMART, permit_all(1.0))
        pending = policy.update(1.5, 0, Authority.SMART, permit_all(1.5))
        policy.update(1.6, 0, Authority.SMART, block_re1(1.6))
        reversed_command = policy.update(3.0, 0, Authority.SMART, block_re1(3.0))

        self.assertIsNone(pending.command)
        self.assertEqual(StageCommand.BLOCK_RE1, pending.pending_command)
        self.assertEqual(StageCommand.PERMIT_RE1, reversed_command.command)

    def test_feedback_timeout_faults_and_never_permits(self) -> None:
        policy = sequencer()
        policy.update(0.0, 2, Authority.SMART, block_all(0.0))
        issued = policy.update(1.0, 0, Authority.SMART, block_all(1.0))
        fault = policy.update(4.0, 0, Authority.SMART, block_all(4.0))
        after_fault = policy.update(5.0, 0, Authority.SMART, block_all(5.0))

        self.assertTrue(issued.command and issued.command.is_permit)
        self.assertEqual(SequencerState.FAULT, fault.state)
        self.assertIsNone(fault.command)
        self.assertIsNone(after_fault.command)

    def test_late_matching_feedback_is_rejected_after_timeout(self) -> None:
        policy = sequencer()
        policy.update(0.0, 2, Authority.SMART, block_all(0.0))
        policy.update(1.0, 0, Authority.SMART, block_all(1.0))
        late = policy.update(4.0, 0, Authority.SMART, block_re1(4.0))

        self.assertEqual(SequencerState.FAULT, late.state)
        self.assertIsNone(late.command)

    def test_readback_gap_cannot_hide_an_uncommanded_permissive_jump(self) -> None:
        policy = sequencer()
        policy.update(0.0, 2, Authority.SMART, block_all(0.0))
        lost = policy.update(1.0, 2, Authority.SMART, unknown(1.0))
        changed = policy.update(1.1, 2, Authority.SMART, permit_all(1.1))

        self.assertEqual(SequencerState.UNKNOWN, lost.state)
        self.assertEqual(2, lost.observed_blocked_stages)
        self.assertEqual(SequencerState.FAULT, changed.state)

    def test_cached_pre_gap_snapshot_cannot_end_unknown_state(self) -> None:
        policy = sequencer()
        policy.update(0.0, 2, Authority.SMART, block_all(0.0))
        policy.update(0.2, 2, Authority.SMART, unknown(0.2))
        cached = policy.update(0.3, 2, Authority.SMART, block_all(0.0))
        fresh = policy.update(0.4, 2, Authority.SMART, block_all(0.4))

        self.assertEqual(SequencerState.UNKNOWN, cached.state)
        self.assertEqual(SequencerState.READY, fresh.state)

    def test_cached_pre_command_snapshot_cannot_acknowledge_command(self) -> None:
        policy = sequencer()
        policy.update(0.0, 0, Authority.SMART, permit_all(0.0))
        policy.update(1.0, 2, Authority.SMART, permit_all(1.0))
        stale_match = policy.update(1.1, 2, Authority.SMART, block_re1(0.5))
        fresh_match = policy.update(1.2, 2, Authority.SMART, block_re1(1.2))

        self.assertEqual(SequencerState.WAITING_FEEDBACK, stale_match.state)
        self.assertEqual(SequencerState.READY, fresh_match.state)

    def test_fresh_feedback_may_repeat_old_state_but_may_not_skip_a_stage(self) -> None:
        waiting = sequencer()
        waiting.update(0.0, 0, Authority.SMART, permit_all(0.0))
        waiting.update(1.0, 2, Authority.SMART, permit_all(1.0))
        old_state = waiting.update(1.1, 2, Authority.SMART, permit_all(1.1))

        skipped = sequencer()
        skipped.update(0.0, 0, Authority.SMART, permit_all(0.0))
        skipped.update(1.0, 2, Authority.SMART, permit_all(1.0))
        fault = skipped.update(1.1, 2, Authority.SMART, block_all(1.1))

        self.assertEqual(SequencerState.WAITING_FEEDBACK, old_state.state)
        self.assertEqual(SequencerState.FAULT, fault.state)

    def test_unknown_readback_never_emits_permit(self) -> None:
        decision = sequencer().update(0.0, 0, Authority.SMART, unknown(0.0))
        self.assertEqual(SequencerState.UNKNOWN, decision.state)
        self.assertIsNone(decision.command)

    def test_invalid_order_is_corrected_only_by_blocking_re1(self) -> None:
        decision = sequencer().update(0.0, 0, Authority.SMART, invalid_order(0.0))
        self.assertEqual(SequencerState.WAITING_FEEDBACK, decision.state)
        self.assertEqual(StageCommand.BLOCK_RE1, decision.command)
        self.assertFalse(decision.command.is_permit)

    def test_uncommanded_valid_state_change_faults(self) -> None:
        policy = sequencer()
        policy.update(0.0, 0, Authority.SMART, permit_all(0.0))
        fault = policy.update(1.0, 0, Authority.SMART, block_re1(1.0))
        self.assertEqual(SequencerState.FAULT, fault.state)
        self.assertIsNone(fault.command)

    def test_bypass_cancels_pending_and_exit_requires_new_read_cycle(self) -> None:
        policy = sequencer()
        policy.update(0.0, 0, Authority.SMART, permit_all(0.0))
        policy.update(1.0, 2, Authority.SMART, permit_all(1.0))
        bypass = policy.update(1.1, 2, Authority.BYPASS, unknown(1.1))
        exit_bypass = policy.update(2.0, 0, Authority.SMART, permit_all(2.0))
        adopted = policy.update(2.1, 0, Authority.SMART, permit_all(2.1))

        self.assertEqual(SequencerState.BYPASSED, bypass.state)
        self.assertIsNone(bypass.pending_command)
        self.assertEqual(SequencerState.UNKNOWN, exit_bypass.state)
        self.assertIsNone(exit_bypass.command)
        self.assertEqual(SequencerState.READY, adopted.state)

    def test_rejects_invalid_or_non_monotonic_inputs(self) -> None:
        policy = sequencer()
        policy.update(1.0, 0, Authority.SMART, permit_all(1.0))
        for now, target in ((0.0, 0), (2.0, -1), (2.0, 3), (2.0, True)):
            with self.subTest(now=now, target=target):
                with self.assertRaises(ValueError):
                    policy.update(now, target, Authority.SMART, permit_all(min(now, 1.0)))
        with self.assertRaises(ValueError):
            policy.update(2.0, 0, "smart", permit_all(2.0))  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
