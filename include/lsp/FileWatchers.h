//------------------------------------------------------------------------------
// FileWatchers.h
// Pattern-based registration and dispatch for watched-file notifications
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace lsp {

/// Registers client watch patterns and dispatches file changes by extension.
class FileWatchers {
public:
    /// Receives matching events in their original order; return true to stop dispatching the batch.
    using Handler = std::function<bool(const DidChangeWatchedFilesParams&)>;

    /// Watch extensions (including the leading dot), delivering them together in one batch.
    /// Optional patterns restrict client registration; dispatch always matches by extension.
    void addExtensions(std::vector<std::string> extensions, Handler handler,
                       std::vector<Pattern> patterns = {});

    /// Build the client registration for these patterns relative to the workspace.
    DidChangeWatchedFilesRegistrationOptions registrationOptions(const URI& workspace) const;

    /// Dispatch batches by extension; the client filters paths using the registered patterns.
    void dispatch(const DidChangeWatchedFilesParams& params) const;

private:
    /// Client patterns paired with their dispatch extensions and batch callback.
    struct Watcher {
        /// The LSP globs sent to the client.
        std::vector<Pattern> patterns;
        /// File extensions dispatched to this handler.
        std::vector<std::string> extensions;
        /// The callback for matching file changes.
        Handler handler;
    };

    /// Watchers in dispatch order; an earlier handler can consume the whole notification.
    std::vector<Watcher> m_watchers;
};

} // namespace lsp
