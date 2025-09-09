//------------------------------------------------------------------------------
// Logging.h
// Logging utilities for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <chrono>
#include <fmt/color.h>
#include <fmt/format.h>
#include <string>

#include "slang/text/SourceLocation.h"

#define INFO(format_string, ...) \
    fmt::print(stderr, "INFO: {}\n", fmt::format(format_string, ##__VA_ARGS__));

#define WARN(format_string, ...) \
    fmt::print(stderr, "WARN: {}\n", fmt::format(format_string, ##__VA_ARGS__));

#define ERROR(format_string, ...) \
    fmt::print(stderr, "ERROR: {}\n", fmt::format(format_string, ##__VA_ARGS__));

#define RFL_INFO(some_struct)                                      \
    fmt::print(stderr, "{}\n",                                     \
               rfl::json::write<rfl::UnderlyingEnums>(some_struct, \
                                                      YYJSON_WRITE_PRETTY_TWO_SPACES));

class ScopedTimer {
public:
    explicit ScopedTimer(std::string name) : m_name(std::move(name)) {
        INFO("ScopedTimer({})", m_name);
        m_start = std::chrono::high_resolution_clock::now();
    }

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start);
        double seconds = static_cast<double>(duration.count()) / 1000000.0;
        INFO("{} scope took {:.3f}s", m_name, seconds);
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;

private:
    std::string m_name;
    std::chrono::high_resolution_clock::time_point m_start;
};

template<>
struct fmt::formatter<slang::SourceLocation> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    constexpr auto format(const slang::SourceLocation& loc, FormatContext& ctx) const {
        if (loc == slang::SourceLocation::NoLocation)
            return fmt::format_to(ctx.out(), "NoLocation");
        else {
            return fmt::format_to(ctx.out(), "{}", loc.offset());
        }
    }
};

template<>
struct fmt::formatter<slang::SourceRange> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    constexpr auto format(const slang::SourceRange& range, FormatContext& ctx) const {
        if (range == slang::SourceRange::NoLocation)
            return fmt::format_to(ctx.out(), "NoRange");
        else {
#ifdef SLANG_DEBUG
            return fmt::format_to(ctx.out(), "{}: {} - {}", range.start().bufferName, range.start(),
                                  range.end());
#else
            return fmt::format_to(ctx.out(), "{} - {}", range.start(), range.end());
#endif
        }
    }
};