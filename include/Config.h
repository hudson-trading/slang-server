//------------------------------------------------------------------------------
// Config.h
// Provide singleton configuration class debug printing macro.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once
#include "rfl/Description.hpp"
#include <optional>
#include <rfl/Result.hpp>
#include <rfl/Skip.hpp>
/// Server configuration, loaded from up to three JSON files:
///
/// 1. `.slang/server.json`       — workspace config (tracked in version control)
/// 2. `~/.slang/server.json`     — user config (personal defaults across projects)
/// 3. `.slang/local/server.json` — local config (untracked, personal overrides)
///
/// Merging rules:
/// - Array fields (e.g. index, indexGlobs) are appended across all files.
/// - Scalar fields are overwritten by later files (local > user > workspace).
/// - `flags` has special precedence: workspace overrides user (only one is used as the base),
///   and local flags are always appended on top. This means local flags add to whichever
///   of workspace/user flags won, rather than replacing them.
///
/// The "Add define" code action writes `-D` flags to `.slang/local/server.json`.
class SlangLspClient;

struct Config {
    /// generate json schema from this by running with --config-schema
    // all fields must be optional
    rfl::Description<"Flags to pass to slang", std::string> flags;

    // Legacy indexing globs, kept for backwards compatibility
    rfl::Description<"Deprecated: use 'index' instead. Globs of what to index. By default will "
                     "index all sv and svh files in the workspace.",
                     std::vector<std::string>>
        indexGlobs;

    struct IndexConfig {
        rfl::Description<"Directories to index", std::vector<std::string>> dirs;
        rfl::Description<
            "Directories to exclude; only supports single directory names and applies to "
            "all path levels",
            std::optional<std::vector<std::string>>>
            excludeDirs;
    };

    rfl::Description<"Index configurations; by default indexes all .sv, .svh, .v, and .vh files in "
                     "the workspace.",
                     std::vector<IndexConfig>>
        index = {};

    // This can't be defaulted because of how we paint configs over each other and append
    // lists together.
    std::vector<std::string> getIndexGlobs() {
        if (indexGlobs.value().size() == 0) {
            return std::vector<std::string>{"./.../*.sv*"};
        }
        else {
            auto list = indexGlobs.value();
            // dedup
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
            return list;
        }
    }

    rfl::Description<"Deprecated: use 'index' instead. Directories to exclude",
                     std::vector<std::string>>
        excludeDirs;
    rfl::Description<"Thread count to use for indexing", int> indexingThreads = 0;
    rfl::Description<"Build file to use", std::optional<std::string>> build;
    rfl::Description<"Build file glob pattern, e.g. `builds/{}.f`. Used for selecting build files.",
                     std::optional<std::string>>
        buildPattern = "**/*.f";
    rfl::Description<"Whether build files use paths relative to that file", bool>
        buildRelativePaths = false;
    rfl::Description<"Waveform file glob to open given a build. Name and top variables can be "
                     "passed with {name}, {top})",
                     std::optional<std::string>>
        wavesPattern;
    rfl::Description<"Waveform viewer command ({} will be replaced with the WCP port), used for "
                     "direct wcp connection with neovim and surfer.",
                     std::optional<std::string>>
        wcpCommand;

    struct InlayHints {
        rfl::Description<"Hints for port types", bool> portTypes = false;
        rfl::Description<"Hints for names of ordered ports and params", bool> orderedInstanceNames =
            true;
        rfl::Description<"Hints for port names in wildcard (.*) ports", bool> wildcardNames = true;
        rfl::Description<"Function argument hints: 0=off, N=only calls with >=N args", int>
            funcArgNames = 2;
        rfl::Description<"Macro argument hints: 0=off, N=only calls with >=N args", int>
            macroArgNames = 2;
    };

    rfl::Description<"Inline hints for things like ordered arguments, wildcard ports, and others",
                     InlayHints>
        inlayHints = InlayHints{};

    struct FlagSource {
        std::string filePath;
        std::string flags;
    };

    /// Load config from up to three sources:
    /// @param workspaceConf  .slang/server.json (tracked, shared)
    /// @param userConf       ~/.slang/server.json (user-wide)
    /// @param localConf      .slang/local/server.json (untracked, personal)
    /// Non-flag fields are merged (arrays appended, scalars overwritten by later files).
    /// For flags: workspace overrides user (last non-local wins), local always appends.
    static Config fromFiles(const std::optional<std::string>& workspaceConf,
                            const std::optional<std::string>& userConf,
                            const std::optional<std::string>& localConf, SlangLspClient& client);

    /// Per-file flags with correct precedence. Skipped during serialization.
    rfl::Skip<std::vector<FlagSource>> flagsByFile;
};
