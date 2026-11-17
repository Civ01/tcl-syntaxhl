#!/usr/bin/env python3
"""Sample module
with a multi-line docstring."""
import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Point:
	x: float = 0.0
	y: float = 0x10

	def norm(self) -> float:
		return (self.x ** 2 + self.y ** 2) ** 0.5  # Euclidean


def main(args: list[str]) -> int:
	name = f"{os.getcwd()}\n"
	match args:
		case [first, *rest]:
			print(first, rest, r"\d+", b'\x00')
		case _:
			pass
	return 0 if name else None
