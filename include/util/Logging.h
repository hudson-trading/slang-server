//------------------------------------------------------------------------------
// Logging.h
// Logging utilities for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <fmt/color.h>
#include <fmt/format.h>

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