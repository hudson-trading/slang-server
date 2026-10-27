//------------------------------------------------------------------------------
// FilePoller.cpp
// Snapshot-based change detection for clients that cannot watch files
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "lsp/FilePoller.h"

namespace fs = std::filesystem;

namespace lsp {

FilePoller::Snapshot FilePoller::snapshot(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return {};
    // A file removed between these calls reports as missing.
    Snapshot result{.exists = true, .writeTime = fs::last_write_time(path, ec)};
    if (ec)
        return {};
    result.size = fs::file_size(path, ec);
    if (ec)
        return {};
    return result;
}

std::vector<FileEvent> FilePoller::poll(const std::vector<fs::path>& paths) {
    std::map<fs::path, Snapshot> current;
    std::vector<FileEvent> events;
    for (const auto& path : paths) {
        if (current.contains(path))
            continue;
        auto now = snapshot(path);
        if (auto previous = m_snapshots.find(path);
            previous != m_snapshots.end() && previous->second != now) {
            auto type = !now.exists                ? FileChangeType::Deleted
                        : !previous->second.exists ? FileChangeType::Created
                                                   : FileChangeType::Changed;
            events.push_back({.uri = URI::fromFile(path), .type = type});
        }
        current.emplace(path, now);
    }
    m_snapshots = std::move(current);
    return events;
}

} // namespace lsp
