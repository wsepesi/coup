"""Helpers for the OpenSpiel-on-Coup smoke tests.

`make pyspiel` writes a .pth into .venv that puts lib/OpenSpiel (for
`open_spiel.python`) and cpp_engine/build-py/python (for pyspiel.so) on
sys.path. The fallback below adds the same paths so the tests also run when
the venv was recreated after the build.
"""
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
for p in (os.path.join(ROOT, "lib", "OpenSpiel"),
          os.path.join(ROOT, "cpp_engine", "build-py", "python")):
  if p not in sys.path:
    sys.path.append(p)

try:
  import pyspiel  # noqa: E402
except ImportError as e:  # pragma: no cover
  raise pytest.UsageError(
      f"cannot import pyspiel ({e}); run `make pyspiel` first") from e

if "coup" not in pyspiel.registered_names():  # pragma: no cover
  raise pytest.UsageError("pyspiel was built without the coup game")


def has_infostate_tensor(game) -> bool:
  return bool(game.get_type().provides_information_state_tensor)


def resample_supported(game) -> tuple[bool, str]:
  """True if State.resample_from_infostate works (needed by ISMCTS)."""
  import random
  s = game.new_initial_state()
  while s.is_chance_node():
    s.apply_action(s.chance_outcomes()[0][0])
  rng = random.Random(0)
  try:
    s.resample_from_infostate(s.current_player(), rng.random)
  except Exception as e:  # SpielFatalError surfaces as RuntimeError
    return False, f"ResampleFromInfostate unavailable: {e}"
  return True, ""


