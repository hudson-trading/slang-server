//------------------------------------------------------------------------------
// Config.cpp
// Configuration management for the language server
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "Config.h"

#include "Logging.h"
#include "SlangLspClient.h"
#include "lsp/LspClient.h"
#include "rfl/Result.hpp"
#include "rfl/from_generic.hpp"

static int CONFIG_READ_FLAGS = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;

namespace fs = std::filesystem;

Config Config::fromFiles(std::vector<std::string> confPaths, SlangLspClient& m_client) {
    rfl::Generic::Object config = *rfl::to_generic(Config()).to_object();

    // Paint over options coming from configs
    for (const auto& confPath : confPaths) {
        if (!fs::exists(confPath)) {
            continue;
        }

        INFO("Loading config from {}", confPath);
        auto file = std::ifstream(confPath);
        auto jsonstr = std::string(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
        {
            auto fileConfig = rfl::json::read<Config, rfl::DefaultIfMissing>(jsonstr,
                                                                             CONFIG_READ_FLAGS);
            if (!fileConfig) {
                m_client.showError(fmt::format("Failed to read config from {}: {}", confPath,
                                               fileConfig.error().what()));
                continue;
            }
        }

        // Reread as generic to get only given fields
        auto generic = rfl::json::read<rfl::Generic>(jsonstr, CONFIG_READ_FLAGS);
        if (!generic) {
            m_client.showError(fmt::format("Failed to read generic config from {}: {}", confPath,
                                           generic.error().what()));
            continue;
        }

        INFO("Doing config merge with {}", confPath);
        auto object = generic->to_object();
        if (!object) {
            m_client.showError(fmt::format("Failed to convert config from {} to object: {}",
                                           confPath, object.error().what()));
            continue;
        }
        for (const auto& [k, v] : *object) {
            config[k] = v;
        }
    }

    auto finalConfig = rfl::from_generic<Config>(config);
    if (!finalConfig) {
        m_client.showError(fmt::format("Failed to convert final config to Config: {}",
                                       finalConfig.error().what()));
        return Config{};
    }
    return *finalConfig;
}
