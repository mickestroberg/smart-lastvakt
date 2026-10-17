from __future__ import annotations

import unittest

from tools.microwave_guard_policy import MicrowaveGuardInput, should_cut_power


def active_sample(**overrides: object) -> MicrowaveGuardInput:
    values: dict[str, object] = {
        "l1_current_a": 20.1,
        "tibber_available": True,
        "tibber_simulated": False,
        "tibber_age_s": 1.0,
        "shelly_ready": True,
        "shelly_output_on": True,
        "shelly_power_w": 1700.0,
        "command_pending": False,
    }
    values.update(overrides)
    return MicrowaveGuardInput(**values)  # type: ignore[arg-type]


class MicrowaveGuardPolicyTests(unittest.TestCase):
    def test_cuts_only_above_20_a_while_microwave_is_active(self) -> None:
        self.assertTrue(should_cut_power(active_sample()))
        self.assertFalse(should_cut_power(active_sample(l1_current_a=20.0)))
        self.assertFalse(should_cut_power(active_sample(l1_current_a=19.9)))
        self.assertFalse(should_cut_power(active_sample(shelly_power_w=499.9)))

    def test_simulated_missing_or_stale_tibber_data_never_controls_relay(self) -> None:
        self.assertFalse(should_cut_power(active_sample(tibber_simulated=True)))
        self.assertFalse(should_cut_power(active_sample(tibber_available=False)))
        self.assertFalse(should_cut_power(active_sample(tibber_age_s=5.1)))
        self.assertFalse(should_cut_power(active_sample(tibber_age_s=-1.0)))

    def test_guard_never_requests_action_if_shelly_is_off_unavailable_or_busy(self) -> None:
        self.assertFalse(should_cut_power(active_sample(shelly_ready=False)))
        self.assertFalse(should_cut_power(active_sample(shelly_output_on=False)))
        self.assertFalse(should_cut_power(active_sample(command_pending=True)))

    def test_other_phases_are_intentionally_not_inputs_to_microwave_guard(self) -> None:
        self.assertFalse(should_cut_power(active_sample(l1_current_a=12.0)))


if __name__ == "__main__":
    unittest.main()
