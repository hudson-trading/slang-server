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

/// Writes elapsed milliseconds into @p out on destruction (no logging).
class ScopedElapsedMs {
public:
    explicit ScopedElapsedMs(double& out) : m_out(out), m_start(std::chrono::steady_clock::now()) {}

    ~ScopedElapsedMs() { m_out = elapsedMs(m_start); }

    ScopedElapsedMs(const ScopedElapsedMs&) = delete;
    ScopedElapsedMs& operator=(const ScopedElapsedMs&) = delete;
    ScopedElapsedMs(ScopedElapsedMs&&) = delete;
    ScopedElapsedMs& operator=(ScopedElapsedMs&&) = delete;

private:
    double& m_out;
    std::chrono::steady_clock::time_point m_start;
};
