from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class MicrowaveGuardInput:
    l1_current_a: float = 0.0
    tibber_available: bool = False
    tibber_simulated: bool = False
    tibber_age_s: float = 0.0
    shelly_ready: bool = False
    shelly_output_on: bool = False
    shelly_power_w: float = 0.0
    command_pending: bool = False


@dataclass(frozen=True)
class MicrowaveGuardConfig:
    l1_limit_a: float = 20.0
    active_power_w: float = 500.0
    maximum_tibber_age_s: float = 5.0


def should_cut_power(
    sample: MicrowaveGuardInput,
    config: MicrowaveGuardConfig = MicrowaveGuardConfig(),
) -> bool:
    """Return whether the one-way microwave guard should request Shelly OFF."""

    tibber_fresh = (
        sample.tibber_available
        and not sample.tibber_simulated
        and 0.0 <= sample.tibber_age_s <= config.maximum_tibber_age_s
    )
    return (
        tibber_fresh
        and sample.shelly_ready
        and sample.shelly_output_on
        and not sample.command_pending
        and sample.shelly_power_w >= config.active_power_w
        and sample.l1_current_a > config.l1_limit_a
    )
