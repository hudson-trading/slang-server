//------------------------------------------------------------------------------
// Config.h
// Provide singleton configuration class debug printing macro.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once
#include "rfl/Description.hpp"
#include <rfl/Result.hpp>

/// A singleton to hold global configuration options.
class SlangLspClient;
class Config {
    /// generate json schema from this by running with --config-schema
public:
    // all fields must be optional
    rfl::Description<"Flags to pass to slang", std::string> flags;
    rfl::Description<"Globs of what to index", std::vector<std::string>> indexGlobs =
        std::vector<std::string>{"./.../*.sv*"};
    rfl::Description<"Directories to exclude", std::vector<std::string>> excludeDirs;
    rfl::Description<"Thread count to use for indexing", int> indexingThreads = 0;
    rfl::Description<"Thread count to use for parsing", int> parsingThreads = 8;
    rfl::Description<"Build file to use", std::optional<std::string>> build;
    rfl::Description<"Build file pattern, ex builds/{}.f", std::optional<std::string>> buildPattern;
    rfl::Description<"Waveform viewer command ({} will be replaced with the WCP port)", std::string>
        wcpCommand;

    static Config fromFiles(std::vector<std::string> confPaths, SlangLspClient& m_client);
};
