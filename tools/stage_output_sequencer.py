"""Hardware-neutral sequencer for two smart blocking outputs.

The upstream load and price policies calculate a desired number of blocked
stages.  This model deliberately translates only between semantic commands
(``BLOCK_RE1`` etc.) and read-back semantic channel states.  Raw relay levels,
Shelly calls, contactor auxiliary contacts and mains wiring belong in a later,
electrician-approved adapter.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import math
from typing import Optional

from tools.load_guard_policy import STAGE_COUNT


class Authority(str, Enum):
    SMART = "smart"
    BYPASS = "bypass"


class ChannelState(str, Enum):
    PERMIT = "permit"
    BLOCK = "block"
    UNKNOWN = "unknown"


class SequencerState(str, Enum):
    UNKNOWN = "unknown"
    READY = "ready"
    WAITING_FEEDBACK = "waiting_feedback"
    BYPASSED = "bypassed"
    FAULT = "fault"


class StageCommand(str, Enum):
    BLOCK_RE1 = "block_re1"
    BLOCK_RE2 = "block_re2"
    PERMIT_RE2 = "permit_re2"
    PERMIT_RE1 = "permit_re1"

    @property
    def is_permit(self) -> bool:
        return self in (StageCommand.PERMIT_RE1, StageCommand.PERMIT_RE2)


@dataclass(frozen=True)
class SequencerConfig:
    minimum_change_interval_s: float
    feedback_timeout_s: float
    settle_time_s: float
    maximum_snapshot_age_s: float

    def validate(self) -> None:
        values = (
            self.minimum_change_interval_s,
            self.feedback_timeout_s,
            self.settle_time_s,
            self.maximum_snapshot_age_s,
        )
        if not all(math.isfinite(value) for value in values):
            raise ValueError("all sequencer times must be finite")
        if self.minimum_change_interval_s < 0 or self.settle_time_s < 0:
            raise ValueError("change interval and settle time must be non-negative")
        if self.feedback_timeout_s <= 0:
            raise ValueError("feedback timeout must be positive")
        if self.maximum_snapshot_age_s <= 0:
            raise ValueError("maximum snapshot age must be positive")


@dataclass(frozen=True)
class OutputSnapshot:
    re1: ChannelState = ChannelState.UNKNOWN
    re2: ChannelState = ChannelState.UNKNOWN
    observed_at_s: float = 0.0


@dataclass(frozen=True)
class SequencerDecision:
    state: SequencerState
    desired_blocked_stages: int
    observed_blocked_stages: Optional[int]
    command: Optional[StageCommand]
    pending_command: Optional[StageCommand]


class StageOutputSequencer:
    """Sequence idempotent semantic commands, one acknowledged step at a time."""

    def __init__(self, config: SequencerConfig) -> None:
        config.validate()
        self.config = config
        self.state = SequencerState.UNKNOWN
        self._observed_blocked: Optional[int] = None
        self._pending_command: Optional[StageCommand] = None
        self._pending_expected: Optional[int] = None
        self._pending_since_s: Optional[float] = None
        self._last_command_s: Optional[float] = None
        self._last_feedback_s: Optional[float] = None
        self._last_update_s: Optional[float] = None
        self._unknown_since_s: Optional[float] = None

    def update(
        self,
        now_s: float,
        desired_blocked_stages: int,
        authority: Authority,
        observed: OutputSnapshot,
    ) -> SequencerDecision:
        self._validate_input(now_s, desired_blocked_stages, authority, observed)
        self._last_update_s = now_s

        if authority is Authority.BYPASS:
            self._enter_bypass(now_s)
            return self._decision(desired_blocked_stages, None)

        if self.state is SequencerState.BYPASSED:
            # A fresh read-back cycle is mandatory after physical bypass.  Do
            # not adopt the snapshot or emit a command in the same call.
            self._enter_unknown(now_s)
            return self._decision(desired_blocked_stages, None)

        snapshot_count = _snapshot_blocked_count(
            observed, now_s, self.config.maximum_snapshot_age_s
        )

        if self.state is SequencerState.FAULT:
            return self._decision(desired_blocked_stages, None)

        if self.state is SequencerState.UNKNOWN:
            if snapshot_count is None:
                return self._decision(desired_blocked_stages, None)
            if (
                self._unknown_since_s is not None
                and observed.observed_at_s <= self._unknown_since_s
            ):
                return self._decision(desired_blocked_stages, None)
            if snapshot_count < 0:
                # (PERMIT, BLOCK) violates the Re1/Re2 ordering.  The only
                # automatic correction is the safer direction: block Re1.
                return self._issue(
                    now_s,
                    desired_blocked_stages,
                    StageCommand.BLOCK_RE1,
                    expected_blocked=2,
                )
            if (
                self._observed_blocked is not None
                and snapshot_count != self._observed_blocked
            ):
                self._enter_fault()
                return self._decision(desired_blocked_stages, None)
            self._observed_blocked = snapshot_count
            self._last_feedback_s = now_s
            self._unknown_since_s = None
            self.state = SequencerState.READY
            return self._decision(desired_blocked_stages, None)

        if self.state is SequencerState.WAITING_FEEDBACK:
            assert self._pending_since_s is not None
            if now_s - self._pending_since_s >= self.config.feedback_timeout_s:
                self._enter_fault()
            elif (
                snapshot_count == self._pending_expected
                and observed.observed_at_s > self._pending_since_s
            ):
                self._observed_blocked = snapshot_count
                self._last_feedback_s = now_s
                self._pending_command = None
                self._pending_expected = None
                self._pending_since_s = None
                self.state = SequencerState.READY
            elif (
                snapshot_count is not None
                and observed.observed_at_s > self._pending_since_s
                and snapshot_count not in (self._observed_blocked, self._pending_expected)
            ):
                self._enter_fault()
            return self._decision(desired_blocked_stages, None)

        # READY: read-back remains authoritative.  Losing it forces a new
        # adoption cycle, while an unexpected valid change is a fault.
        if snapshot_count is None:
            self._enter_unknown(now_s, preserve_observed=True)
            return self._decision(desired_blocked_stages, None)
        if snapshot_count < 0:
            return self._issue(
                now_s,
                desired_blocked_stages,
                StageCommand.BLOCK_RE1,
                expected_blocked=2,
            )
        if snapshot_count != self._observed_blocked:
            self._enter_fault()
            return self._decision(desired_blocked_stages, None)

        assert self._observed_blocked is not None
        if desired_blocked_stages == self._observed_blocked:
            return self._decision(desired_blocked_stages, None)
        if not self._next_change_allowed(now_s):
            return self._decision(desired_blocked_stages, None)

        if desired_blocked_stages > self._observed_blocked:
            if self._observed_blocked == 0:
                return self._issue(
                    now_s,
                    desired_blocked_stages,
                    StageCommand.BLOCK_RE1,
                    expected_blocked=1,
                )
            return self._issue(
                now_s,
                desired_blocked_stages,
                StageCommand.BLOCK_RE2,
                expected_blocked=2,
            )

        if self._observed_blocked == 2:
            return self._issue(
                now_s,
                desired_blocked_stages,
                StageCommand.PERMIT_RE2,
                expected_blocked=1,
            )
        return self._issue(
            now_s,
            desired_blocked_stages,
            StageCommand.PERMIT_RE1,
            expected_blocked=0,
        )

    def _validate_input(
        self,
        now_s: float,
        desired_blocked_stages: int,
        authority: Authority,
        observed: OutputSnapshot,
    ) -> None:
        if not math.isfinite(now_s) or now_s < 0:
            raise ValueError("now_s must be finite and non-negative")
        if self._last_update_s is not None and now_s < self._last_update_s:
            raise ValueError("now_s must be monotonic")
        if isinstance(desired_blocked_stages, bool) or not isinstance(
            desired_blocked_stages, int
        ):
            raise ValueError("desired_blocked_stages must be an integer")
        if not 0 <= desired_blocked_stages <= STAGE_COUNT:
            raise ValueError("desired_blocked_stages must be between 0 and 2")
        if not isinstance(authority, Authority):
            raise ValueError("authority must be an Authority value")
        if not isinstance(observed, OutputSnapshot) or not all(
            isinstance(value, ChannelState) for value in (observed.re1, observed.re2)
        ):
            raise ValueError("observed must contain semantic ChannelState values")
        if (
            not math.isfinite(observed.observed_at_s)
            or observed.observed_at_s < 0
            or observed.observed_at_s > now_s
        ):
            raise ValueError("snapshot time must be finite and no later than now_s")

    def _next_change_allowed(self, now_s: float) -> bool:
        interval_ready = (
            self._last_command_s is None
            or now_s - self._last_command_s >= self.config.minimum_change_interval_s
        )
        settled = (
            self._last_feedback_s is None
            or now_s - self._last_feedback_s >= self.config.settle_time_s
        )
        return interval_ready and settled

    def _issue(
        self,
        now_s: float,
        desired_blocked_stages: int,
        command: StageCommand,
        *,
        expected_blocked: int,
    ) -> SequencerDecision:
        self.state = SequencerState.WAITING_FEEDBACK
        self._pending_command = command
        self._pending_expected = expected_blocked
        self._pending_since_s = now_s
        self._last_command_s = now_s
        self._unknown_since_s = None
        return self._decision(desired_blocked_stages, command)

    def _enter_unknown(
        self,
        now_s: float,
        *,
        preserve_observed: bool = False,
    ) -> None:
        self.state = SequencerState.UNKNOWN
        self._unknown_since_s = now_s
        if not preserve_observed:
            self._observed_blocked = None
        self._pending_command = None
        self._pending_expected = None
        self._pending_since_s = None
        self._last_feedback_s = None

    def _enter_bypass(self, now_s: float) -> None:
        self._enter_unknown(now_s)
        self.state = SequencerState.BYPASSED

    def _enter_fault(self) -> None:
        self.state = SequencerState.FAULT
        self._pending_command = None
        self._pending_expected = None
        self._pending_since_s = None

    def _decision(
        self,
        desired_blocked_stages: int,
        command: Optional[StageCommand],
    ) -> SequencerDecision:
        return SequencerDecision(
            state=self.state,
            desired_blocked_stages=desired_blocked_stages,
            observed_blocked_stages=self._observed_blocked,
            command=command,
            pending_command=self._pending_command,
        )


def _snapshot_blocked_count(
    snapshot: OutputSnapshot,
    now_s: float,
    maximum_age_s: float,
) -> Optional[int]:
    if now_s - snapshot.observed_at_s > maximum_age_s:
        return None
    if ChannelState.UNKNOWN in (snapshot.re1, snapshot.re2):
        return None
    if snapshot.re1 is ChannelState.PERMIT and snapshot.re2 is ChannelState.PERMIT:
        return 0
    if snapshot.re1 is ChannelState.BLOCK and snapshot.re2 is ChannelState.PERMIT:
        return 1
    if snapshot.re1 is ChannelState.BLOCK and snapshot.re2 is ChannelState.BLOCK:
        return 2
    return -1
