"""pytest setup: make pyspiel / open_spiel.python importable (see coup_helpers)."""
import coup_helpers  # noqa: F401  (sys.path setup + pyspiel/coup check)
import pyspiel
import pytest


@pytest.fixture(scope="session")
def coup2():
  return pyspiel.load_game("coup")
