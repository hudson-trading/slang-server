//------------------------------------------------------------------------------
// FilePoller.h
// Snapshot-based change detection for clients that cannot watch files
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace lsp {

/// Detects created, changed, and deleted files by comparing timestamps and sizes between polls.
class FilePoller {
public:
    /// Compare paths against their previous snapshots, returning events in path order.
    /// Paths seen for the first time only record a snapshot; unlisted paths are forgotten.
    std::vector<FileEvent> poll(const std::vector<std::filesystem::path>& paths);

private:
    struct Snapshot {
        bool exists = false;
        std::filesystem::file_time_type writeTime;
        std::uintmax_t size = 0;

        bool operator==(const Snapshot&) const = default;
    };

    static Snapshot snapshot(const std::filesystem::path& path);

    /// The last observed state of each polled path.
    std::map<std::filesystem::path, Snapshot> m_snapshots;
};

} // namespace lsp
