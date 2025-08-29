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
