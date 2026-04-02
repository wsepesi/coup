// T4: C FFI raw bindings via bun:ffi
import { dlopen, FFIType, ptr, suffix } from "bun:ffi";
import { resolve, dirname } from "path";

const LIB_NAME = `libcoup.${suffix}`;
const LIB_PATH = resolve(dirname(import.meta.dir), "..", "..", "c_engine", LIB_NAME);

let _lib: ReturnType<typeof openLib> | null = null;

function openLib() {
  return dlopen(LIB_PATH, {
    game_init: {
      args: [FFIType.ptr, FFIType.i32, FFIType.u64, FFIType.u64],
      returns: FFIType.void,
    },
    step_deterministic: {
      args: [FFIType.ptr, FFIType.i32],
      returns: FFIType.void,
    },
    step_with_rng: {
      args: [FFIType.ptr, FFIType.i32],
      returns: FFIType.void,
    },
    get_valid_actions: {
      args: [FFIType.ptr],
      returns: FFIType.u32,
    },
    observe: {
      args: [FFIType.ptr, FFIType.i32, FFIType.ptr, FFIType.i32, FFIType.ptr],
      returns: FFIType.void,
    },
    get_observation_size: {
      args: [],
      returns: FFIType.i32,
    },
    is_done: {
      args: [FFIType.ptr],
      returns: FFIType.i32,
    },
    get_winner: {
      args: [FFIType.ptr],
      returns: FFIType.i32,
    },
    get_active_player_ext: {
      args: [FFIType.ptr],
      returns: FFIType.i32,
    },
    get_num_players_ext: {
      args: [FFIType.ptr],
      returns: FFIType.i32,
    },
    is_chance_node: {
      args: [FFIType.ptr],
      returns: FFIType.i32,
    },
    chance_outcomes: {
      args: [FFIType.ptr, FFIType.ptr],
      returns: FFIType.i32,
    },
    apply_chance: {
      args: [FFIType.ptr, FFIType.i32],
      returns: FFIType.void,
    },
  });
}

export function getLib() {
  if (!_lib) {
    _lib = openLib();
  }
  return _lib;
}

// Game struct size: 6*2 (players) + 2 (deck) + 2 (phase_state) + 2 (aux) + 2 (aux2) + 2 (pad) + 32 (xoshiro256 = 4*uint64) = 48 bytes
// Actually: Xoshiro256 is uint64_t s[4] = 32 bytes. Total = 12 + 4 + 32 = 48
// 6*uint16 + 4*uint16 + 2 pad + Xoshiro256(4*uint64=32) = 22 + 2 align + 32 = 56
// Use 64 for safety
export const GAME_STRUCT_SIZE = 64;

export function allocGameBuffer(): ArrayBuffer {
  return new ArrayBuffer(GAME_STRUCT_SIZE);
}

export { ptr };
