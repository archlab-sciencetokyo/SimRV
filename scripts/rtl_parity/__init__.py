"""Reusable infrastructure for RTL parity adapters."""

from .registry import TargetAdapter, get_target, iter_targets

__all__ = ["TargetAdapter", "get_target", "iter_targets"]
