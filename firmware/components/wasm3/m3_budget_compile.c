/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wasm3/m3_exec_defs.h"

M3Result m3_YieldMemory(void);

/* Bulk operations can do a page of work without a call or a backward branch.
 * Use the dispatch macro here to keep the vendored interpreter unchanged. */
#undef nextOpDirect
#define nextOpDirect()                                                          \
    do {                                                                        \
        if (strcmp(__func__, "op_MemFill") == 0 ||                              \
            strcmp(__func__, "op_MemCopy") == 0 ||                              \
            strcmp(__func__, "op_MemInit") == 0) {                              \
            m3ret_t stopped = m3_YieldMemory();                                       \
            if (stopped != m3Err_none) {                                        \
                return stopped;                                                 \
            }                                                                   \
        }                                                                       \
        M3_MUSTTAIL return nextOpImpl();                                        \
    } while (0)

#include "wasm3/m3_compile.c"
