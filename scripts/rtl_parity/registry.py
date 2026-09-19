"""Declarative registry and lazy loader for RTL parity adapters."""

from dataclasses import dataclass
from importlib import import_module
from typing import Callable, Iterable, Sequence


@dataclass(frozen=True)
class TargetAdapter:
    """Metadata and lazy entry point for one RTL parity target."""

    name: str
    description: str
    module: str
    entrypoint: str = "run"

    def load(self) -> Callable[[Sequence[str]], int]:
        runner = getattr(import_module(self.module), self.entrypoint)
        if not callable(runner):
            raise TypeError(f"{self.module}:{self.entrypoint} is not callable")
        return runner

    def run(self, arguments: Sequence[str]) -> int:
        return self.load()(arguments)


_TARGETS = (
    TargetAdapter(
        name="rvcomp",
        description="RVComp five-stage RV32IM core",
        module="rtl_parity.targets.rvcomp",
    ),
    TargetAdapter(
        name="cfu-pg",
        description="CFU Proving Ground RVProc core",
        module="rtl_parity.targets.cfu_pg",
    ),
)


def iter_targets() -> Iterable[TargetAdapter]:
    return iter(_TARGETS)


def get_target(name: str) -> TargetAdapter | None:
    return next((target for target in _TARGETS if target.name == name), None)
