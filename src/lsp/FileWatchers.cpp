//------------------------------------------------------------------------------
// FileWatchers.cpp
// Pattern-based registration and dispatch for watched-file notifications
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "lsp/FileWatchers.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace lsp {

void FileWatchers::addExtensions(std::vector<std::string> extensions, Handler handler,
                                 std::vector<Pattern> patterns) {
    if (patterns.empty()) {
        for (const auto& extension : extensions)
            patterns.push_back("**/*" + extension);
    }
    m_watchers.push_back({std::move(patterns), std::move(extensions), std::move(handler)});
}

DidChangeWatchedFilesRegistrationOptions FileWatchers::registrationOptions(
    const URI& workspace) const {
    DidChangeWatchedFilesRegistrationOptions result;
    for (const auto& watcher : m_watchers) {
        for (const auto& pattern : watcher.patterns) {
            result.watchers.push_back(
                {.globPattern = RelativePattern{.baseUri = workspace, .pattern = pattern}});
        }
    }
    return result;
}

void FileWatchers::dispatch(const DidChangeWatchedFilesParams& params) const {
    std::vector<std::string> extensions;
    extensions.reserve(params.changes.size());
    for (const auto& change : params.changes)
        extensions.push_back(std::filesystem::path(change.uri.getPath()).extension().string());

    for (const auto& watcher : m_watchers) {
        DidChangeWatchedFilesParams matching;
        for (size_t i = 0; i < params.changes.size(); ++i) {
            if (std::ranges::find(watcher.extensions, extensions[i]) != watcher.extensions.end())
                matching.changes.push_back(params.changes[i]);
        }
        if (!matching.changes.empty() && watcher.handler(matching))
            return;
    }
}

} // namespace lsp
