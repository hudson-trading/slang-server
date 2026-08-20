//------------------------------------------------------------------------------
// Log.h
// Logging functions for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <chrono>
#include <cstdio>
#include <fmt/format.h>
#include <iterator>
#include <string_view>
#include <utility>

namespace server::logging {

struct Milliseconds {
    template<typename Rep, typename Period>
    explicit Milliseconds(std::chrono::duration<Rep, Period> duration) :
        value(std::chrono::round<std::chrono::milliseconds>(duration).count()) {}

    std::chrono::milliseconds::rep value;
};

} // namespace server::logging

template<>
struct fmt::formatter<server::logging::Milliseconds> {
    constexpr auto parse(format_parse_context& context) { return context.begin(); }

    template<typename FormatContext>
    auto format(server::logging::Milliseconds duration, FormatContext& context) const {
        return fmt::format_to(context.out(), "{} ms", duration.value);
    }
};

namespace server::logging {

namespace detail {
inline thread_local FILE* output = stderr;

template<typename... Args>
void write(std::string_view prefix, fmt::format_string<Args...> format, Args&&... args) {
    fmt::memory_buffer buffer;
    buffer.append(prefix.data(), prefix.data() + prefix.size());
    fmt::format_to(std::back_inserter(buffer), format, std::forward<Args>(args)...);
    buffer.push_back('\n');
    std::fwrite(buffer.data(), 1, buffer.size(), output);
}
} // namespace detail

inline FILE* setOutput(FILE* output) {
    return std::exchange(detail::output, output);
}

inline void blankLine() {
    std::fputc('\n', detail::output);
}

template<typename... Args>
void info(fmt::format_string<Args...> format, Args&&... args) {
    detail::write("INFO: ", format, std::forward<Args>(args)...);
}

template<typename... Args>
void warn(fmt::format_string<Args...> format, Args&&... args) {
    detail::write("WARN: ", format, std::forward<Args>(args)...);
}

template<typename... Args>
void error(fmt::format_string<Args...> format, Args&&... args) {
    detail::write("ERROR: ", format, std::forward<Args>(args)...);
}

} // namespace server::logging
