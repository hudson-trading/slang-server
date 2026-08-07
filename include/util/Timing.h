//------------------------------------------------------------------------------
// Timing.h
// Lightweight timing helpers (no logging macros — safe to include from tests).
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <chrono>

/// Elapsed milliseconds since @p start (steady clock).
inline double elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}
