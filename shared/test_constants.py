"""
test_constants.py

Verify that the C engine (coup_core.h) and C++ engine (coup_game.h) define
identical constants for action indices, phase enums, card types, and
observation dimensions.

Run from the repo root:
    python3 shared/test_constants.py

Or via pytest:
    pytest shared/test_constants.py -v
"""

import os
import re
import subprocess
import sys
import tempfile

SHARED_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SHARED_DIR)
C_ENGINE_DIR = os.path.join(REPO_ROOT, "c_engine")
CPP_HEADER = os.path.join(REPO_ROOT, "cpp_engine", "coup_game.h")

# ---- Mapping from C #define names to C++ enum/constexpr names ---------------

# Each entry: C key -> C++ key
# We keep explicit mappings so the test fails loudly if either side renames
# something without updating the other.

CARD_TYPE_MAP = {
    "DUKE":       "kDuke",
    "ASSASSIN":   "kAssassin",
    "CAPTAIN":    "kCaptain",
    "AMBASSADOR": "kAmbassador",
    "CONTESSA":   "kContessa",
}

PHASE_MAP = {
    "PHASE_DEAL":              "kDeal",
    "PHASE_CHANCE_REDRAW":     "kChanceRedraw",
    "PHASE_CHANCE_EXCHANGE":   "kChanceExchange",
    "PHASE_MAIN_ACTION":       "kMainAction",
    "PHASE_CHALLENGE_ACTION":  "kChallengeAction",
    "PHASE_BLOCK":             "kBlock",
    "PHASE_CHALLENGE_BLOCK":   "kChallengeBlock",
    "PHASE_LOSE_CARD":         "kLoseCard",
    "PHASE_EXCHANGE_DISCARD":  "kExchangeDiscard",
    "PHASE_RESOLVE":           "kResolve",
}

ACTION_MAP = {
    "ACT_INCOME":           "kIncome",
    "ACT_FOREIGN_AID":      "kForeignAid",
    "ACT_TAX":              "kTax",
    "ACT_EXCHANGE":         "kExchange",
    "ACT_COUP_P0":          "kCoupPlayer0",
    "ACT_STEAL_P0":         "kStealPlayer0",
    "ACT_ASSASSINATE_P0":   "kAssassinatePlayer0",
    "ACT_CHALLENGE":        "kChallenge",
    "ACT_PASS":             "kPass",
    "ACT_BLOCK_CONTESSA":   "kBlockContessa",
    "ACT_BLOCK_CAPTAIN":    "kBlockCaptain",
    "ACT_BLOCK_AMBASSADOR": "kBlockAmbassador",
    "ACT_BLOCK_DUKE":       "kBlockDuke",
    "ACT_DISCARD_SLOT0":    "kDiscardSlot0",
    "ACT_DISCARD_SLOT1":    "kDiscardSlot1",
    "ACT_DISCARD_SLOT2":    "kDiscardSlot2",
    "ACT_DISCARD_SLOT3":    "kDiscardSlot3",
}

MISC_MAP = {
    "MAX_PLAYERS":        "kMaxPlayers",
    "MAX_CHANCE_OUTCOMES": "kNumCardTypes",  # C uses 5 via MAX_CHANCE_OUTCOMES, C++ via kNumCardTypes
    "OBS_SIZE":           "kObservationTensorSize",
}


# ---- C constants via compiled helper ----------------------------------------

def get_c_constants():
    """Compile and run print_c_constants.c; return {name: int_value} dict."""
    src = os.path.join(SHARED_DIR, "print_c_constants.c")
    assert os.path.isfile(src), f"Missing helper source: {src}"

    with tempfile.NamedTemporaryFile(suffix="_print_c_constants", delete=False) as tmp:
        binary = tmp.name

    try:
        comp = subprocess.run(
            ["cc", "-std=c11", "-o", binary, src, "-I", C_ENGINE_DIR],
            capture_output=True, text=True,
        )
        if comp.returncode != 0:
            raise RuntimeError(
                f"C compilation failed (exit {comp.returncode}):\n{comp.stderr}"
            )

        run = subprocess.run([binary], capture_output=True, text=True)
        if run.returncode != 0:
            raise RuntimeError(
                f"C helper crashed (exit {run.returncode}):\n{run.stderr}"
            )
    finally:
        if os.path.exists(binary):
            os.unlink(binary)

    constants = {}
    for line in run.stdout.strip().splitlines():
        key, val = line.split("=", 1)
        constants[key.strip()] = int(val.strip())
    return constants


# ---- C++ constants via regex on the header ----------------------------------

def get_cpp_constants():
    """Parse coup_game.h and return {name: int_value} dict for all enums and
    inline constexpr int declarations."""
    assert os.path.isfile(CPP_HEADER), f"Missing C++ header: {CPP_HEADER}"

    with open(CPP_HEADER) as f:
        content = f.read()

    constants = {}

    # Match: inline constexpr int kFoo = <expr>;
    for m in re.finditer(
        r"inline\s+constexpr\s+int\s+(\w+)\s*=\s*([^;]+);", content
    ):
        name = m.group(1)
        expr = m.group(2).strip()
        # Resolve simple expressions like "kNumCardTypes * kCardsPerType"
        try:
            # Replace already-known constants in the expression
            resolved = expr
            for k, v in constants.items():
                resolved = resolved.replace(k, str(v))
            constants[name] = int(eval(resolved))  # safe: only int arithmetic
        except Exception:
            pass  # skip complex expressions we cannot resolve

    # Match enum values:  kFoo = 0,
    for m in re.finditer(r"(\w+)\s*=\s*(\d+)\s*[,}]", content):
        name = m.group(1)
        val = int(m.group(2))
        if name.startswith("k"):
            constants[name] = val

    return constants


# ---- The actual test --------------------------------------------------------

def test_constants_match():
    c_consts = get_c_constants()
    cpp_consts = get_cpp_constants()

    all_maps = {
        "Card types": CARD_TYPE_MAP,
        "Phases": PHASE_MAP,
        "Actions": ACTION_MAP,
        "Misc": MISC_MAP,
    }

    errors = []

    for category, mapping in all_maps.items():
        for c_name, cpp_name in mapping.items():
            if c_name not in c_consts:
                errors.append(f"[{category}] C constant missing: {c_name}")
                continue
            if cpp_name not in cpp_consts:
                errors.append(f"[{category}] C++ constant missing: {cpp_name}")
                continue
            c_val = c_consts[c_name]
            cpp_val = cpp_consts[cpp_name]
            if c_val != cpp_val:
                errors.append(
                    f"[{category}] MISMATCH {c_name}(C)={c_val} vs "
                    f"{cpp_name}(C++)={cpp_val}"
                )

    if errors:
        msg = "Constants out of sync between C and C++ engines:\n" + "\n".join(
            f"  - {e}" for e in errors
        )
        raise AssertionError(msg)


# ---- CLI entry point --------------------------------------------------------

if __name__ == "__main__":
    try:
        test_constants_match()
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)

    c = get_c_constants()
    cpp = get_cpp_constants()
    total_checked = sum(len(m) for m in [CARD_TYPE_MAP, PHASE_MAP, ACTION_MAP, MISC_MAP])
    print(f"OK -- {total_checked} constant pairs verified between C and C++ engines.")
    print(f"  C constants extracted:   {len(c)}")
    print(f"  C++ constants extracted: {len(cpp)}")
    sys.exit(0)
