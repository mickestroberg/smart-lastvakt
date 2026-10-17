from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto


CHANNEL_COUNT = 4
MAXIMUM_MS = ((99 * 60 * 60) + (59 * 60) + 59) * 1000


class TimerState(Enum):
    IDLE = auto()
    READY = auto()
    RUNNING = auto()
    PAUSED = auto()
    FINISHED = auto()


@dataclass
class TimerSlot:
    state: TimerState = TimerState.IDLE
    remaining_ms: int = 0
    deadline_ms: int = 0


@dataclass
class CookingTimerBank:
    slots: list[TimerSlot] = field(
        default_factory=lambda: [TimerSlot() for _ in range(CHANNEL_COUNT)]
    )

    def remaining(self, index: int, now_ms: int) -> int:
        if not 0 <= index < CHANNEL_COUNT:
            return 0
        slot = self.slots[index]
        if slot.state is TimerState.RUNNING:
            return max(0, min(MAXIMUM_MS, slot.deadline_ms - now_ms))
        return max(0, min(MAXIMUM_MS, slot.remaining_ms))

    def adjust(self, index: int, delta_ms: int, now_ms: int) -> bool:
        if not 0 <= index < CHANNEL_COUNT:
            return False
        slot = self.slots[index]
        if slot.state is TimerState.RUNNING:
            return False
        before = self.remaining(index, now_ms)
        after = max(0, min(MAXIMUM_MS, before + delta_ms))
        slot.remaining_ms = after
        slot.deadline_ms = 0
        if after == 0:
            slot.state = TimerState.IDLE
        elif slot.state is not TimerState.PAUSED:
            slot.state = TimerState.READY
        return after != before

    def toggle(self, index: int, now_ms: int) -> bool:
        if not 0 <= index < CHANNEL_COUNT:
            return False
        slot = self.slots[index]
        if slot.state is TimerState.RUNNING:
            slot.remaining_ms = self.remaining(index, now_ms)
            slot.deadline_ms = 0
            slot.state = (
                TimerState.PAUSED if slot.remaining_ms > 0 else TimerState.FINISHED
            )
            return True
        if slot.state in (TimerState.READY, TimerState.PAUSED) and slot.remaining_ms > 0:
            slot.deadline_ms = now_ms + slot.remaining_ms
            slot.state = TimerState.RUNNING
            return True
        return False

    def reset(self, index: int) -> None:
        if not 0 <= index < CHANNEL_COUNT:
            return
        self.slots[index] = TimerSlot()

    def update(self, now_ms: int) -> int:
        newly_finished = 0
        for index, slot in enumerate(self.slots):
            if slot.state is TimerState.RUNNING and now_ms >= slot.deadline_ms:
                slot.state = TimerState.FINISHED
                slot.remaining_ms = 0
                slot.deadline_ms = 0
                newly_finished |= 1 << index
        return newly_finished

    def finished_mask(self) -> int:
        return sum(
            1 << index
            for index, slot in enumerate(self.slots)
            if slot.state is TimerState.FINISHED
        )

    def acknowledge_finished(self) -> None:
        for index, slot in enumerate(self.slots):
            if slot.state is TimerState.FINISHED:
                self.reset(index)
