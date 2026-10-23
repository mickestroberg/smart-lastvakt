"""Pure reference policy for quarter-hourly electricity-price limiting.

The module has no network, GPIO, relay or ESP-IDF dependencies.  It turns one
complete local day's Tibber prices into short-lived *blocking* requests for the
two-stage load-guard model.  A price decision can limit the highest permitted
heater power, but it can never request that heat is turned on.
"""

from __future__ import annotations

from dataclasses import dataclass
from calendar import monthrange
from datetime import date, datetime, time, timedelta, timezone
from enum import Enum
import math
from typing import Iterable, Optional
from tools.load_guard_policy import ExternalBlock, STAGE_COUNT


QUARTER = timedelta(minutes=15)
DEFAULT_TIMEZONE = "Europe/Stockholm"
DEFAULT_CURRENCY = "SEK"
DEFAULT_MAX_LEASE_S = 60.0
DEFAULT_INACTIVE_MONTHS = (6, 7, 8, 9)


class PriceTier(str, Enum):
    """Maximum smart-control allowance for one quarter."""

    FULL = "full"
    HALF = "half"
    OFF = "off"

    @property
    def minimum_blocked_stages(self) -> int:
        return {
            PriceTier.FULL: 0,
            PriceTier.HALF: 1,
            PriceTier.OFF: 2,
        }[self]


class PriceDataState(str, Enum):
    READY = "ready"
    BYPASS = "bypass"
    SEASONAL_PAUSE = "seasonal_pause"
    MISSING = "missing"
    INVALID_TIME = "invalid_time"
    OUTSIDE_DAY = "outside_day"


@dataclass(frozen=True)
class QuarterPrice:
    starts_at: datetime
    total: float
    currency: str = DEFAULT_CURRENCY


@dataclass(frozen=True)
class PricePolicyConfig:
    """Ranking quotas for each *actual* local day.

    The defaults mean five most expensive quarters off, the following ten at
    half power, and all remaining quarters unrestricted by price.  The ranked
    schedule is still built during the default June--September seasonal pause,
    but the effective decision remains transparent.
    """

    off_quarters: int = 5
    half_quarters: int = 10
    timezone_name: str = DEFAULT_TIMEZONE
    expected_currency: str = DEFAULT_CURRENCY
    inactive_months: tuple[int, ...] = DEFAULT_INACTIVE_MONTHS

    def validate(self) -> None:
        if isinstance(self.off_quarters, bool) or not isinstance(self.off_quarters, int):
            raise ValueError("off_quarters must be an integer")
        if isinstance(self.half_quarters, bool) or not isinstance(
            self.half_quarters, int
        ):
            raise ValueError("half_quarters must be an integer")
        if self.off_quarters < 0 or self.half_quarters < 0:
            raise ValueError("quarter quotas must be non-negative")
        if self.timezone_name != DEFAULT_TIMEZONE:
            raise ValueError(
                f"this reference model supports only {DEFAULT_TIMEZONE}"
            )
        if not self.expected_currency or self.expected_currency != self.expected_currency.upper():
            raise ValueError("expected_currency must be a non-empty uppercase code")
        if not isinstance(self.inactive_months, tuple):
            raise ValueError("inactive_months must be a tuple")
        if any(
            isinstance(month, bool) or not isinstance(month, int)
            for month in self.inactive_months
        ):
            raise ValueError("inactive_months must contain integers")
        if any(month < 1 or month > 12 for month in self.inactive_months):
            raise ValueError("inactive_months must contain calendar months 1--12")
        if len(set(self.inactive_months)) != len(self.inactive_months):
            raise ValueError("inactive_months must not contain duplicates")


@dataclass(frozen=True)
class PriceSlot:
    starts_at: datetime
    ends_at: datetime
    total: float
    rank: int
    tier: PriceTier


@dataclass(frozen=True)
class PriceSchedule:
    local_day: date
    timezone_name: str
    currency: str
    slots: tuple[PriceSlot, ...]
    inactive_months: tuple[int, ...]


@dataclass(frozen=True)
class PriceDecision:
    data_state: PriceDataState
    tier: PriceTier
    minimum_blocked_stages: int
    valid_until: Optional[datetime]
    current_price_total: Optional[float] = None
    currency: Optional[str] = None
    rank: Optional[int] = None
    scheduled_tier: Optional[PriceTier] = None


def expected_quarter_starts(
    local_day: date, timezone_name: str = DEFAULT_TIMEZONE
) -> tuple[datetime, ...]:
    """Return every real quarter start in a local day, normalized to UTC.

    Constructing the sequence in UTC naturally yields 92, 96 or 100 entries on
    Stockholm's 23-, 24- and 25-hour days without inventing or collapsing local
    wall-clock times.
    """

    if timezone_name != DEFAULT_TIMEZONE:
        raise ValueError(f"this reference model supports only {DEFAULT_TIMEZONE}")
    start_utc = _stockholm_midnight_utc(local_day)
    end_utc = _stockholm_midnight_utc(local_day + timedelta(days=1))
    count, remainder = divmod(end_utc - start_utc, QUARTER)
    if remainder:
        raise ValueError("local day length is not divisible into 15-minute slots")
    return tuple(start_utc + index * QUARTER for index in range(count))


def build_price_schedule(
    local_day: date,
    prices: Iterable[QuarterPrice],
    config: PricePolicyConfig = PricePolicyConfig(),
) -> PriceSchedule:
    """Validate and rank one complete local day of quarter-hourly prices.

    Invalid, incomplete or duplicate input raises ``ValueError``.  A runtime
    caller must retain no new schedule in that case, making the price layer
    transparent while the independent COALA and phase policy continue to work.
    """

    config.validate()
    expected_starts = expected_quarter_starts(local_day, config.timezone_name)
    entries = tuple(prices)
    if len(entries) != len(expected_starts):
        raise ValueError(
            f"expected {len(expected_starts)} quarter prices, got {len(entries)}"
        )
    if config.off_quarters + config.half_quarters > len(entries):
        raise ValueError("price quotas exceed the number of quarters in this day")

    by_start: dict[datetime, float] = {}
    for entry in entries:
        if not isinstance(entry.starts_at, datetime):
            raise ValueError("every starts_at value must be a datetime")
        if entry.starts_at.tzinfo is None or entry.starts_at.utcoffset() is None:
            raise ValueError("every starts_at value must be timezone-aware")
        if isinstance(entry.total, bool):
            raise ValueError("every total price must be numeric")
        try:
            total = float(entry.total)
        except (TypeError, ValueError) as exc:
            raise ValueError("every total price must be numeric") from exc
        if not math.isfinite(total):
            raise ValueError("every total price must be finite")
        if entry.currency != config.expected_currency:
            raise ValueError("every price must use the configured currency")
        starts_at_utc = entry.starts_at.astimezone(timezone.utc)
        if starts_at_utc in by_start:
            raise ValueError("duplicate quarter start")
        by_start[starts_at_utc] = total

    if set(by_start) != set(expected_starts):
        raise ValueError("prices do not form one complete, contiguous local day")

    ranked_starts = sorted(expected_starts, key=lambda value: (-by_start[value], value))
    rank_by_start = {starts_at: rank for rank, starts_at in enumerate(ranked_starts, 1)}

    slots: list[PriceSlot] = []
    for starts_at in expected_starts:
        rank = rank_by_start[starts_at]
        if rank <= config.off_quarters:
            tier = PriceTier.OFF
        elif rank <= config.off_quarters + config.half_quarters:
            tier = PriceTier.HALF
        else:
            tier = PriceTier.FULL
        slots.append(
            PriceSlot(
                starts_at=starts_at,
                ends_at=starts_at + QUARTER,
                total=by_start[starts_at],
                rank=rank,
                tier=tier,
            )
        )

    return PriceSchedule(
        local_day=local_day,
        timezone_name=config.timezone_name,
        currency=config.expected_currency,
        slots=tuple(slots),
        inactive_months=tuple(sorted(config.inactive_months)),
    )


def decide_price_limit(
    schedule: Optional[PriceSchedule],
    now: datetime,
    *,
    enabled: bool = True,
    time_trusted: bool = True,
) -> PriceDecision:
    """Choose the active quarter using an exclusive UTC end boundary.

    During a configured seasonal pause, the returned effective tier is FULL
    and no stages are blocked.  Price, rank and ``scheduled_tier`` still expose
    what the already-built schedule would have requested if control were
    active, so a UI can display and evaluate the plan without actuating it.
    """

    if not enabled:
        return _neutral_decision(PriceDataState.BYPASS)
    if not time_trusted or now.tzinfo is None or now.utcoffset() is None:
        return _neutral_decision(PriceDataState.INVALID_TIME)
    if schedule is None:
        return _neutral_decision(PriceDataState.MISSING)

    now_utc = now.astimezone(timezone.utc)
    if stockholm_local_time(now_utc).date() != schedule.local_day:
        return _neutral_decision(PriceDataState.OUTSIDE_DAY)

    for slot in schedule.slots:
        if slot.starts_at <= now_utc < slot.ends_at:
            if schedule.local_day.month in schedule.inactive_months:
                return PriceDecision(
                    data_state=PriceDataState.SEASONAL_PAUSE,
                    tier=PriceTier.FULL,
                    minimum_blocked_stages=0,
                    valid_until=None,
                    current_price_total=slot.total,
                    currency=schedule.currency,
                    rank=slot.rank,
                    scheduled_tier=slot.tier,
                )
            return PriceDecision(
                data_state=PriceDataState.READY,
                tier=slot.tier,
                minimum_blocked_stages=slot.tier.minimum_blocked_stages,
                valid_until=slot.ends_at,
                current_price_total=slot.total,
                currency=schedule.currency,
                rank=slot.rank,
                scheduled_tier=slot.tier,
            )
    return _neutral_decision(PriceDataState.MISSING)


def to_external_block(
    decision: PriceDecision,
    now_wall: datetime,
    now_monotonic_s: float,
    *,
    max_lease_s: float = DEFAULT_MAX_LEASE_S,
) -> ExternalBlock:
    """Translate a decision into a lease ending at the original quarter boundary."""

    if not math.isfinite(now_monotonic_s) or now_monotonic_s < 0:
        raise ValueError("now_monotonic_s must be finite and non-negative")
    if not math.isfinite(max_lease_s) or max_lease_s <= 0:
        raise ValueError("max_lease_s must be finite and positive")
    if now_wall.tzinfo is None or now_wall.utcoffset() is None:
        return ExternalBlock()
    if (
        decision.data_state is not PriceDataState.READY
        or decision.valid_until is None
        or decision.minimum_blocked_stages == 0
    ):
        return ExternalBlock()
    if not 0 <= decision.minimum_blocked_stages <= STAGE_COUNT:
        raise ValueError("price decision requested an invalid number of stages")

    remaining_s = (
        decision.valid_until - now_wall.astimezone(timezone.utc)
    ).total_seconds()
    if remaining_s <= 0:
        return ExternalBlock()
    return ExternalBlock(
        minimum_blocked_stages=decision.minimum_blocked_stages,
        valid_until_s=now_monotonic_s + min(remaining_s, max_lease_s),
    )


def _neutral_decision(state: PriceDataState) -> PriceDecision:
    return PriceDecision(
        data_state=state,
        tier=PriceTier.FULL,
        minimum_blocked_stages=0,
        valid_until=None,
    )


def stockholm_local_time(value: datetime) -> datetime:
    """Convert an aware datetime to Swedish civil time without external tzdata.

    Sweden follows the EU rule: CEST begins at 01:00 UTC on the last Sunday in
    March and ends at 01:00 UTC on the last Sunday in October.  ESP-IDF uses its
    own POSIX timezone rule; this small equivalent keeps the desktop reference
    model dependency-free on Windows as well.
    """

    if value.tzinfo is None or value.utcoffset() is None:
        raise ValueError("value must be timezone-aware")
    value_utc = value.astimezone(timezone.utc)
    offset = _stockholm_offset_at_utc(value_utc)
    return value_utc.astimezone(timezone(offset))


def _stockholm_midnight_utc(local_day: date) -> datetime:
    spring = _last_sunday(local_day.year, 3)
    autumn = _last_sunday(local_day.year, 10)
    offset_hours = 2 if spring < local_day <= autumn else 1
    local_midnight_as_utc = datetime.combine(local_day, time.min, tzinfo=timezone.utc)
    return local_midnight_as_utc - timedelta(hours=offset_hours)


def _stockholm_offset_at_utc(value_utc: datetime) -> timedelta:
    spring = _last_sunday(value_utc.year, 3)
    autumn = _last_sunday(value_utc.year, 10)
    dst_start = datetime.combine(spring, time(hour=1), tzinfo=timezone.utc)
    dst_end = datetime.combine(autumn, time(hour=1), tzinfo=timezone.utc)
    return timedelta(hours=2 if dst_start <= value_utc < dst_end else 1)


def _last_sunday(year: int, month: int) -> date:
    last_day = date(year, month, monthrange(year, month)[1])
    return last_day - timedelta(days=(last_day.weekday() + 1) % 7)
