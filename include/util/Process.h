// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace server {

/// Exit status and captured standard output of a child process.
struct ProcessResult {
    /// Process exit code, or 128 plus the terminating signal on POSIX.
    int exitCode;
    /// Standard output, including any embedded null bytes.
    std::string output;
};

/// Run an argument vector without a shell, supplying stdin and capturing stdout.
/// Discard stderr; return no result if process creation or stream handling fails.
std::optional<ProcessResult> runProcess(std::span<const std::string> arguments,
                                        std::string_view input = {});

} // namespace server
