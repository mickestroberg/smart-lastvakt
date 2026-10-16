"""Non-actuating reference policy for future two-stage load control.

This module deliberately has no network, GPIO, Shelly or ESP-IDF dependencies. It is
an executable specification used to explore and test decisions before any physical
output is implemented in the firmware.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import math
from typing import Optional


STAGE_COUNT = 2


class DataState(str, Enum):
    LIVE = "live"
    MISSING = "missing"
    SIMULATED = "simulated"
    STALE = "stale"
    INVALID = "invalid"


class DecisionReason(str, Enum):
    LIVE_NORMAL = "live_normal"
    MISSING_DATA = "missing_data"
    SIMULATED_DATA = "simulated_data"
    STALE_DATA = "stale_data"
    INVALID_DATA = "invalid_data"
    EXTERNAL_BLOCK_ACTIVE = "external_block_active"
    EXTERNAL_SHED = "external_shed"
    PHASE_OVERLOAD_WAIT = "phase_overload_wait"
    PHASE_SHED = "phase_shed"
    RESTORE_WAIT = "restore_wait"
    RESTORED_STAGE = "restored_stage"
    INSUFFICIENT_HEADROOM = "insufficient_headroom"
    MINIMUM_CHANGE_INTERVAL = "minimum_change_interval"
    BLOCK_ACTIVE = "block_active"


@dataclass(frozen=True)
class PolicyConfig:
    """Policy values supplied by the future installation configuration.

    Tuple position 0 is the Re1/lowest-priority stage: first shed, last restored.
    Tuple position 1 is the Re2/higher-priority stage: second shed, first restored.
    """

    trip_current_a: float
    restore_margin_a: float
    stage_current_a: tuple[float, float]
    shed_delay_s: tuple[float, float]
    restore_delay_s: tuple[float, float]
    minimum_change_interval_s: float
    maximum_sample_age_s: float

    def validate(self) -> None:
        if len(self.stage_current_a) != STAGE_COUNT:
            raise ValueError("exactly two stage currents are required")
        if len(self.shed_delay_s) != STAGE_COUNT:
            raise ValueError("exactly two shed delays are required")
        if len(self.restore_delay_s) != STAGE_COUNT:
            raise ValueError("exactly two restore delays are required")
        finite_values = (
            self.trip_current_a,
            self.restore_margin_a,
            *self.stage_current_a,
            *self.shed_delay_s,
            *self.restore_delay_s,
            self.minimum_change_interval_s,
            self.maximum_sample_age_s,
        )
        if not all(math.isfinite(value) for value in finite_values):
            raise ValueError("all policy values must be finite")
        if self.trip_current_a <= 0:
            raise ValueError("trip_current_a must be positive")
        if not 0 <= self.restore_margin_a < self.trip_current_a:
            raise ValueError("restore_margin_a must be non-negative and below trip current")
        if any(value <= 0 for value in self.stage_current_a):
            raise ValueError("stage currents must be positive")
        if any(value < 0 for value in (*self.shed_delay_s, *self.restore_delay_s)):
            raise ValueError("delays must be non-negative")
        if self.minimum_change_interval_s < 0:
            raise ValueError("minimum change interval must be non-negative")
        if self.maximum_sample_age_s <= 0:
            raise ValueError("maximum sample age must be positive")


@dataclass(frozen=True)
class PhaseSample:
    current_a: tuple[float, float, float] = (0.0, 0.0, 0.0)
    age_s: float = 0.0
    available: bool = True
    simulated: bool = False


@dataclass(frozen=True)
class ExternalBlock:
    """A temporary request that may block, but can never enable, stages."""

    minimum_blocked_stages: int = 0
    valid_until_s: float = 0.0

    def active_count(self, now_s: float) -> int:
        if not 0 <= self.minimum_blocked_stages <= STAGE_COUNT:
            raise ValueError("minimum_blocked_stages must be between 0 and 2")
        if not math.isfinite(self.valid_until_s):
            raise ValueError("valid_until_s must be finite")
        return self.minimum_blocked_stages if now_s < self.valid_until_s else 0


@dataclass(frozen=True)
class Decision:
    data_state: DataState
    blocked_stages: int
    allow_re1: bool
    allow_re2: bool
    max_current_a: Optional[float]
    predicted_restore_current_a: Optional[float]
    changed: bool
    reasons: tuple[DecisionReason, ...]


class LoadGuardPolicy:
    """Stateful two-stage policy with no physical side effects."""

    def __init__(self, config: PolicyConfig) -> None:
        config.validate()
        self.config = config
        self.blocked_stages = 0
        self._overload_since_s: Optional[float] = None
        self._restore_since_s: Optional[float] = None
        self._last_change_s: Optional[float] = None
        self._last_update_s: Optional[float] = None

    def update(
        self,
        now_s: float,
        sample: PhaseSample,
        external_block: ExternalBlock = ExternalBlock(),
    ) -> Decision:
        if not math.isfinite(now_s) or now_s < 0:
            raise ValueError("now_s must be finite and non-negative")
        if self._last_update_s is not None and now_s < self._last_update_s:
            raise ValueError("now_s must be monotonic")
        self._last_update_s = now_s

        external_minimum = external_block.active_count(now_s)
        data_state = self._classify_sample(sample)
        reasons: list[DecisionReason] = []
        changed = False
        max_current_a: Optional[float] = None
        predicted_restore_current_a: Optional[float] = None

        if data_state is not DataState.LIVE:
            reasons.append(self._data_reason(data_state))
            if external_minimum:
                reasons.append(DecisionReason.EXTERNAL_BLOCK_ACTIVE)
            if self.blocked_stages != external_minimum:
                self.blocked_stages = external_minimum
                self._last_change_s = now_s
                changed = True
            self._reset_condition_timers()
            return self._decision(
                data_state,
                max_current_a,
                predicted_restore_current_a,
                changed,
                reasons,
            )

        max_current_a = max(sample.current_a)

        # A current, unexpired external request may only add blocking. Applying it
        # immediately is conservative and cannot force a load to start.
        if external_minimum:
            reasons.append(DecisionReason.EXTERNAL_BLOCK_ACTIVE)
        if external_minimum > self.blocked_stages:
            self.blocked_stages = external_minimum
            self._last_change_s = now_s
            self._reset_condition_timers()
            changed = True
            reasons.append(DecisionReason.EXTERNAL_SHED)

        overloaded = max_current_a > self.config.trip_current_a
        if overloaded and self.blocked_stages < STAGE_COUNT and not changed:
            self._restore_since_s = None
            if self._overload_since_s is None:
                self._overload_since_s = now_s
            delay = self.config.shed_delay_s[self.blocked_stages]
            delay_elapsed = now_s - self._overload_since_s >= delay
            interval_elapsed = self._minimum_interval_elapsed(now_s)
            if delay_elapsed and interval_elapsed:
                self.blocked_stages += 1
                self._last_change_s = now_s
                self._overload_since_s = None
                changed = True
                reasons.append(DecisionReason.PHASE_SHED)
            else:
                reasons.append(DecisionReason.PHASE_OVERLOAD_WAIT)
                if delay_elapsed and not interval_elapsed:
                    reasons.append(DecisionReason.MINIMUM_CHANGE_INTERVAL)
        elif overloaded:
            self._restore_since_s = None
            if self.blocked_stages >= STAGE_COUNT:
                self._overload_since_s = None
        else:
            self._overload_since_s = None
            if self.blocked_stages > external_minimum and not changed:
                restore_stage = self.blocked_stages - 1
                predicted_restore_current_a = (
                    max_current_a + self.config.stage_current_a[restore_stage]
                )
                restore_limit = self.config.trip_current_a - self.config.restore_margin_a
                if predicted_restore_current_a <= restore_limit:
                    if self._restore_since_s is None:
                        self._restore_since_s = now_s
                    delay = self.config.restore_delay_s[restore_stage]
                    delay_elapsed = now_s - self._restore_since_s >= delay
                    interval_elapsed = self._minimum_interval_elapsed(now_s)
                    if delay_elapsed and interval_elapsed:
                        self.blocked_stages -= 1
                        self._last_change_s = now_s
                        self._restore_since_s = None
                        changed = True
                        reasons.append(DecisionReason.RESTORED_STAGE)
                    else:
                        reasons.append(DecisionReason.RESTORE_WAIT)
                        if delay_elapsed and not interval_elapsed:
                            reasons.append(DecisionReason.MINIMUM_CHANGE_INTERVAL)
                else:
                    self._restore_since_s = None
                    reasons.append(DecisionReason.INSUFFICIENT_HEADROOM)
            else:
                self._restore_since_s = None

        if self.blocked_stages:
            reasons.append(DecisionReason.BLOCK_ACTIVE)
        if not reasons:
            reasons.append(DecisionReason.LIVE_NORMAL)

        return self._decision(
            data_state,
            max_current_a,
            predicted_restore_current_a,
            changed,
            reasons,
        )

    def _classify_sample(self, sample: PhaseSample) -> DataState:
        if not sample.available:
            return DataState.MISSING
        if sample.simulated:
            return DataState.SIMULATED
        if len(sample.current_a) != 3:
            return DataState.INVALID
        if not math.isfinite(sample.age_s) or sample.age_s < 0:
            return DataState.INVALID
        if sample.age_s > self.config.maximum_sample_age_s:
            return DataState.STALE
        if any(not math.isfinite(value) or value < 0 for value in sample.current_a):
            return DataState.INVALID
        return DataState.LIVE

    @staticmethod
    def _data_reason(data_state: DataState) -> DecisionReason:
        return {
            DataState.MISSING: DecisionReason.MISSING_DATA,
            DataState.SIMULATED: DecisionReason.SIMULATED_DATA,
            DataState.STALE: DecisionReason.STALE_DATA,
            DataState.INVALID: DecisionReason.INVALID_DATA,
        }[data_state]

    def _minimum_interval_elapsed(self, now_s: float) -> bool:
        return (
            self._last_change_s is None
            or now_s - self._last_change_s >= self.config.minimum_change_interval_s
        )

    def _reset_condition_timers(self) -> None:
        self._overload_since_s = None
        self._restore_since_s = None

    def _decision(
        self,
        data_state: DataState,
        max_current_a: Optional[float],
        predicted_restore_current_a: Optional[float],
        changed: bool,
        reasons: list[DecisionReason],
    ) -> Decision:
        return Decision(
            data_state=data_state,
            blocked_stages=self.blocked_stages,
            allow_re1=self.blocked_stages == 0,
            allow_re2=self.blocked_stages < 2,
            max_current_a=max_current_a,
            predicted_restore_current_a=predicted_restore_current_a,
            changed=changed,
            reasons=tuple(dict.fromkeys(reasons)),
        )


def pf_candidate_example_config() -> PolicyConfig:
    """Illustrative values only; not approved installation settings."""

    return PolicyConfig(
        trip_current_a=23.0,
        restore_margin_a=1.0,
        stage_current_a=(6.5, 6.5),
        shed_delay_s=(2.0, 5.0),
        restore_delay_s=(30.0, 30.0),
        minimum_change_interval_s=2.0,
        maximum_sample_age_s=10.0,
    )
