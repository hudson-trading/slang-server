//------------------------------------------------------------------------------
// Config.cpp
// Configuration management for the language server
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "Config.h"

#include "SlangLspClient.h"
#include "lsp/LspClient.h"
#include "rfl/Result.hpp"
#include "rfl/from_generic.hpp"
#include "util/Logging.h"
#include <rfl/DefaultIfMissing.hpp>

static int CONFIG_READ_FLAGS = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;

namespace fs = std::filesystem;

Config Config::fromFiles(const std::optional<std::string>& workspaceConf,
                         const std::optional<std::string>& userConf,
                         const std::optional<std::string>& localConf, SlangLspClient& client) {
    rfl::Generic::Object config = *rfl::to_generic(Config()).to_object();

    // Layer a single config file onto the merged config object
    auto layerFile = [&](const std::string& confPath) -> std::optional<rfl::Generic::Object> {
        if (!fs::exists(confPath)) {
            WARN("Config file {} does not exist, skipping", confPath);
            return std::nullopt;
        }

        INFO("Layering config from {}", confPath);
        auto file = std::ifstream(confPath);
        auto jsonstr = std::string(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());

        // Validate against Config schema first
        auto fileConfig = rfl::json::read<Config, rfl::DefaultIfMissing>(jsonstr,
                                                                         CONFIG_READ_FLAGS);
        if (!fileConfig) {
            client.showError(fmt::format("Failed to read config from {}: {}", confPath,
                                         fileConfig.error().what()));
            return std::nullopt;
        }

        auto generic = rfl::json::read<rfl::Generic>(jsonstr, CONFIG_READ_FLAGS);
        if (!generic) {
            client.showError(fmt::format("Failed to read generic config from {}: {}", confPath,
                                         generic.error().what()));
            return std::nullopt;
        }

        auto object = generic->to_object();
        if (!object) {
            client.showError(fmt::format("Failed to convert config from {} to object: {}", confPath,
                                         object.error().what()));
            return std::nullopt;
        }

        for (const auto& [k, v] : *object) {
            if (auto existingArray = config[k].to_array()) {
                auto arr = existingArray.value();
                auto newArr = v.to_array().value();
                arr.reserve(arr.size() + newArr.size());
                arr.insert(arr.end(), newArr.begin(), newArr.end());
                config[k] = arr;
            }
            else {
                config[k] = v;
            }
        }
        return *object;
    };

    // Layer configs: workspace and user override each other, local is additive (for flags)
    std::optional<rfl::Generic::Object> workspaceObj, userObj, localObj;
    if (workspaceConf)
        workspaceObj = layerFile(*workspaceConf);
    if (userConf)
        userObj = layerFile(*userConf);
    if (localConf)
        localObj = layerFile(*localConf);

    auto finalConfig = rfl::from_generic<Config, rfl::DefaultIfMissing>(config);
    if (!finalConfig) {
        client.showError(
            fmt::format("Failed to convert final config: {}", finalConfig.error().what()));
        return Config{};
    }

    auto hasField = [](const std::optional<rfl::Generic::Object>& obj,
                       std::string_view fieldName) -> bool {
        return obj && obj->count(std::string(fieldName)) > 0;
    };

    const bool buildPatternExplicit = hasField(workspaceObj, "buildPattern") ||
                                      hasField(userObj, "buildPattern") ||
                                      hasField(localObj, "buildPattern");
    if (!buildPatternExplicit && finalConfig->builds.value().empty()) {
        finalConfig->buildPattern = "**/*.f";
    }

    // Build flagsByFile: workspace overrides user (last non-local wins), local appends
    auto extractFlags = [](std::optional<rfl::Generic::Object>& obj) -> std::string {
        if (!obj)
            return {};
        auto flags = (*obj)["flags"].to_string();
        return flags.value_or("");
    };

    std::vector<FlagSource> flagsByFile;
    // Base flags: workspace wins over user
    auto wsFlags = extractFlags(workspaceObj);
    auto userFlags = extractFlags(userObj);
    if (!wsFlags.empty())
        flagsByFile.push_back({*workspaceConf, wsFlags});
    else if (!userFlags.empty())
        flagsByFile.push_back({*userConf, userFlags});
    // Local always appends
    auto lcFlags = extractFlags(localObj);
    if (!lcFlags.empty())
        flagsByFile.push_back({*localConf, lcFlags});

    finalConfig->flagsByFile = std::move(flagsByFile);

    // Reconstruct merged flags for client config
    std::string merged;
    for (auto& src : finalConfig->flagsByFile.value()) {
        if (!merged.empty())
            merged += " ";
        merged += src.flags;
    }
    finalConfig->flags = merged;

    return *finalConfig;
}
