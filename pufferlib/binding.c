/*
 * binding.c — CPython C extension for PufferLib integration with Coup engine.
 *
 * Exposes vec_init, vec_step, vec_close, vec_log as module functions.
 * Uses zero-copy numpy array pointers for observations, actions, rewards,
 * terminals, and truncations.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <numpy/arrayobject.h>
#include <stdlib.h>
#include <string.h>

#include "../c_engine/coup_core.h"
#include "../c_engine/history.h"

/* Total observation size: engine obs + 32-float action mask */
#define TOTAL_OBS_SIZE (OBS_SIZE + 32)

/* Episode statistics accumulator */
typedef struct {
    float episode_length;
    float winner_seat;
    float game_result;  /* +1 for each completed episode */
    float n;            /* number of completed episodes */
} Log;

/* Per-environment state */
typedef struct {
    Game game;
    HistoryBuffer history;
    int num_players;
    int step_count;
} CoupEnv;

/* Global state set by vec_init */
static float *observations_buf = NULL;
static int *actions_buf = NULL;
static float *rewards_buf = NULL;
static unsigned char *terminals_buf = NULL;
static unsigned char *truncations_buf = NULL;
static CoupEnv *envs = NULL;
static Log *logs = NULL;
static int num_envs_global = 0;

/* ---------- helpers ---------- */

/* Write observation for environment i into the observations buffer.
 * Writes OBS_SIZE floats from observe(), then 32 floats for the action mask. */
static void write_obs(int i) {
    float *obs = &observations_buf[i * TOTAL_OBS_SIZE];
    CoupEnv *env = &envs[i];

    int active = get_active_player_ext(&env->game);

    /* Linearize ring buffer for observe(): oldest first.
     * observe() reads history[0..n-1] sequentially. */
    HistoryEntry linear[64];
    int n = env->history.len;
    for (int j = 0; j < n; j++) {
        /* history_get(buf, 0) = most recent, so we reverse to get oldest first */
        linear[n - 1 - j] = history_get(&env->history, j);
    }

    /* Write core observation (407 floats) */
    observe(&env->game, active, linear, n, obs);

    /* Write action mask as last 32 floats */
    uint32_t mask = get_valid_actions(&env->game);
    float *mask_out = obs + OBS_SIZE;
    for (int a = 0; a < 32; a++) {
        mask_out[a] = (mask >> a) & 1 ? 1.0f : 0.0f;
    }
}

/* Reset environment i with a new game */
static void reset_env(int i) {
    CoupEnv *env = &envs[i];
    uint64_t deal_seed = xoshiro256_next(&env->game.rng);
    uint64_t proc_seed = xoshiro256_next(&env->game.rng);
    game_init(&env->game, env->num_players, deal_seed, proc_seed);
    history_init(&env->history);
    env->step_count = 0;

    /* game_init auto-deals when seeds are provided, so we should be
     * in PHASE_MAIN_ACTION. Write the initial observation. */
    write_obs(i);
}

/* ---------- vec_init ---------- */

static PyObject* vec_init(PyObject *self, PyObject *args) {
    PyArrayObject *obs_arr, *act_arr, *rew_arr, *term_arr, *trunc_arr;
    int num_envs, seed, num_players;

    if (!PyArg_ParseTuple(args, "O!O!O!O!O!iii",
            &PyArray_Type, &obs_arr,
            &PyArray_Type, &act_arr,
            &PyArray_Type, &rew_arr,
            &PyArray_Type, &term_arr,
            &PyArray_Type, &trunc_arr,
            &num_envs, &seed, &num_players)) {
        return NULL;
    }

    /* Validate array types */
    if (PyArray_TYPE(obs_arr) != NPY_FLOAT32) {
        PyErr_SetString(PyExc_TypeError, "observations must be float32");
        return NULL;
    }
    if (PyArray_TYPE(act_arr) != NPY_INT32) {
        PyErr_SetString(PyExc_TypeError, "actions must be int32");
        return NULL;
    }
    if (PyArray_TYPE(rew_arr) != NPY_FLOAT32) {
        PyErr_SetString(PyExc_TypeError, "rewards must be float32");
        return NULL;
    }
    if (PyArray_TYPE(term_arr) != NPY_UINT8) {
        PyErr_SetString(PyExc_TypeError, "terminals must be uint8");
        return NULL;
    }
    if (PyArray_TYPE(trunc_arr) != NPY_UINT8) {
        PyErr_SetString(PyExc_TypeError, "truncations must be uint8");
        return NULL;
    }

    /* Store buffer pointers */
    observations_buf = (float *)PyArray_DATA(obs_arr);
    actions_buf = (int *)PyArray_DATA(act_arr);
    rewards_buf = (float *)PyArray_DATA(rew_arr);
    terminals_buf = (unsigned char *)PyArray_DATA(term_arr);
    truncations_buf = (unsigned char *)PyArray_DATA(trunc_arr);
    num_envs_global = num_envs;

    /* Allocate env and log arrays */
    if (envs) { free(envs); envs = NULL; }
    if (logs) { free(logs); logs = NULL; }

    envs = (CoupEnv *)calloc(num_envs, sizeof(CoupEnv));
    logs = (Log *)calloc(num_envs, sizeof(Log));
    if (!envs || !logs) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate environments");
        return NULL;
    }

    /* Initialize each environment */
    Xoshiro256 seed_rng;
    xoshiro256_seed(&seed_rng, (uint64_t)seed);

    for (int i = 0; i < num_envs; i++) {
        CoupEnv *env = &envs[i];
        env->num_players = num_players;
        env->step_count = 0;
        history_init(&env->history);

        uint64_t deal_seed = xoshiro256_next(&seed_rng);
        uint64_t proc_seed = xoshiro256_next(&seed_rng);
        game_init(&env->game, num_players, deal_seed, proc_seed);

        /* game_init auto-deals when seeds are provided */

        /* Clear buffers for this env */
        rewards_buf[i] = 0.0f;
        terminals_buf[i] = 0;
        truncations_buf[i] = 0;

        /* Write initial observation */
        write_obs(i);
    }

    Py_RETURN_NONE;
}

/* ---------- vec_step ---------- */

static PyObject* vec_step(PyObject *self, PyObject *args) {
    for (int i = 0; i < num_envs_global; i++) {
        CoupEnv *env = &envs[i];
        int action = actions_buf[i];

        /* Record who is acting before we step */
        int acting_player = get_active_player_ext(&env->game);

        /* Push history entry */
        HistoryEntry entry;
        entry.acting_player = (uint8_t)acting_player;
        entry.action = (uint8_t)action;
        entry.phase = (uint8_t)get_phase(&env->game);
        entry.result = 0;
        history_push(&env->history, entry);

        /* Step the game (handles chance nodes and resolve internally) */
        step_with_rng(&env->game, action);
        env->step_count++;

        /* Check terminal */
        if (is_done(&env->game)) {
            int winner = get_winner(&env->game);

            /* Reward from the perspective of the acting player:
             * +1 if the acting player is the winner, -1 otherwise.
             * In self-play with player rotation, this is the reward
             * for the agent that just took the action. */
            rewards_buf[i] = (winner == acting_player) ? 1.0f : -1.0f;
            terminals_buf[i] = 1;
            truncations_buf[i] = 0;

            /* Accumulate log statistics */
            logs[i].episode_length += (float)env->step_count;
            logs[i].winner_seat += (float)winner;
            logs[i].game_result += 1.0f;
            logs[i].n += 1.0f;

            /* Auto-reset */
            reset_env(i);
        } else {
            rewards_buf[i] = 0.0f;
            terminals_buf[i] = 0;
            truncations_buf[i] = 0;

            /* Write observation for next active player */
            write_obs(i);
        }
    }

    Py_RETURN_NONE;
}

/* ---------- vec_close ---------- */

static PyObject* vec_close(PyObject *self, PyObject *args) {
    if (envs) { free(envs); envs = NULL; }
    if (logs) { free(logs); logs = NULL; }
    observations_buf = NULL;
    actions_buf = NULL;
    rewards_buf = NULL;
    terminals_buf = NULL;
    truncations_buf = NULL;
    num_envs_global = 0;
    Py_RETURN_NONE;
}

/* ---------- vec_log ---------- */

static PyObject* vec_log(PyObject *self, PyObject *args) {
    /* Aggregate across all envs */
    float total_length = 0.0f;
    float total_winner = 0.0f;
    float total_result = 0.0f;
    float total_n = 0.0f;

    for (int i = 0; i < num_envs_global; i++) {
        total_length += logs[i].episode_length;
        total_winner += logs[i].winner_seat;
        total_result += logs[i].game_result;
        total_n += logs[i].n;

        /* Reset counters */
        logs[i].episode_length = 0.0f;
        logs[i].winner_seat = 0.0f;
        logs[i].game_result = 0.0f;
        logs[i].n = 0.0f;
    }

    /* Return averaged values */
    if (total_n > 0.0f) {
        return Py_BuildValue("{s:f,s:f,s:f}",
            "episode_length", (double)(total_length / total_n),
            "winner_seat", (double)(total_winner / total_n),
            "episode_return", (double)(total_result / total_n));
    } else {
        /* No episodes completed yet */
        return Py_BuildValue("{s:f,s:f,s:f}",
            "episode_length", 0.0,
            "winner_seat", 0.0,
            "episode_return", 0.0);
    }
}

/* ---------- Module definition ---------- */

static PyMethodDef CoupMethods[] = {
    {"vec_init",  vec_init,  METH_VARARGS, "Initialize vectorized Coup environments."},
    {"vec_step",  vec_step,  METH_NOARGS,  "Step all environments."},
    {"vec_close", vec_close, METH_NOARGS,  "Free environment memory."},
    {"vec_log",   vec_log,   METH_NOARGS,  "Get and reset episode statistics."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef coupmodule = {
    PyModuleDef_HEAD_INIT,
    "coup_binding",
    "PufferLib C binding for the Coup game engine.",
    -1,
    CoupMethods
};

PyMODINIT_FUNC PyInit_coup_binding(void) {
    import_array();  /* Initialize numpy C API; returns NULL on failure */
    PyObject *m = PyModule_Create(&coupmodule);
    return m;
}
