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
/// A singleton to hold global configuration options.
class SlangLspClient;
class Config {
    /// generate json schema from this by running with --config-schema
public:
    // all fields must be optional
    rfl::Description<"Flags to pass to slang", std::string> flags;
    rfl::Description<
        "Globs of what to index. By default will index all sv and svh files in the workspace.",
        std::vector<std::string>>
        indexGlobs;

    // This can't be defaulted because of how we paint configs over each other and append lists
    // together.
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

    rfl::Description<"Directories to exclude", std::vector<std::string>> excludeDirs;
    rfl::Description<"Thread count to use for indexing", int> indexingThreads = 0;
    rfl::Description<"Thread count to use for parsing", int> parsingThreads = 8;
    rfl::Description<"Build file to use", std::optional<std::string>> build;
    rfl::Description<"Build file glob pattern, e.g. `builds/{}.f`. Used for selecting build files.",
                     std::optional<std::string>>
        buildPattern;
    rfl::Description<"Waveform file glob to open given a build. Name and top variables can be "
                     "passed with {name}, {top})",
                     std::optional<std::string>>
        wavesPattern;
    rfl::Description<"Waveform viewer command ({} will be replaced with the WCP port), used for "
                     "direct wcp connection with neovim and surfer.",
                     std::optional<std::string>>
        wcpCommand;

    static Config fromFiles(std::vector<std::string> confPaths, SlangLspClient& m_client);
};
