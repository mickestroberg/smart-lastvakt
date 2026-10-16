"""Run a deterministic, non-actuating load-guard demonstration."""

from __future__ import annotations

from tools.load_guard_policy import LoadGuardPolicy, PhaseSample, pf_candidate_example_config


def main() -> None:
    policy = LoadGuardPolicy(pf_candidate_example_config())
    scenario = (
        (0.0, (12.0, 11.0, 10.0), "normal"),
        (1.0, (23.6, 12.0, 11.0), "L1 over example threshold"),
        (3.0, (23.6, 12.0, 11.0), "Re1 is shed after delay"),
        (4.0, (23.4, 12.0, 11.0), "overload persists"),
        (9.0, (23.4, 12.0, 11.0), "Re2 is shed after delay"),
        (10.0, (8.0, 7.5, 7.0), "headroom begins"),
        (40.0, (8.0, 7.5, 7.0), "Re2 is restored first"),
        (41.0, (14.5, 14.0, 13.5), "headroom for Re1 begins"),
        (71.0, (14.5, 14.0, 13.5), "Re1 is restored last"),
    )

    print("Illustrative PF-candidate scenario; this program has no physical outputs.\n")
    print(" time  currents L1/L2/L3       blocked  Re1   Re2   reasons")
    print("-----  ----------------------  -------  ----  ----  -----------------------------")
    for now_s, currents, note in scenario:
        decision = policy.update(now_s, PhaseSample(current_a=currents))
        reasons = ",".join(reason.value for reason in decision.reasons)
        formatted = "/".join(f"{value:4.1f}" for value in currents)
        print(
            f"{now_s:5.1f}  {formatted:22}  {decision.blocked_stages:^7}  "
            f"{'ON' if decision.allow_re1 else 'OFF':^4}  "
            f"{'ON' if decision.allow_re2 else 'OFF':^4}  {reasons}  # {note}"
        )


if __name__ == "__main__":
    main()
