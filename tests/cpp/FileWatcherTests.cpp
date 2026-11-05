// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "lsp/FileWatchers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

TEST_CASE("File watchers register patterns and dispatch by extension") {
    const auto root = std::filesystem::temp_directory_path() / "slang_watcher_patterns";
    const auto workspace = URI::fromFile(root);
    lsp::FileWatchers watchers;
    const std::vector<std::string> patterns{
        "**/*.sv", "**/*.svh",           "**/*.v",
        "**/*.vh", ".slang/server.json", ".slang/local/server.json",
        "**/*.f"};
    std::vector<std::vector<lsp::FileEvent>> batches(3);
    auto handler = [&](size_t i) {
        return [&, i](const auto& params) {
            batches[i] = params.changes;
            return false;
        };
    };
    watchers.addExtensions({".sv", ".svh", ".v", ".vh"}, handler(0));
    watchers.addExtensions({".json"}, handler(1),
                           {".slang/server.json", ".slang/local/server.json"});
    watchers.addExtensions({".f"}, handler(2));

    const auto options = watchers.registrationOptions(workspace);
    REQUIRE(options.watchers.size() == patterns.size());
    for (size_t i = 0; i < patterns.size(); ++i) {
        const auto& pattern = rfl::get<lsp::RelativePattern>(options.watchers[i].globPattern);
        CHECK(rfl::get<URI>(pattern.baseUri) == workspace);
        CHECK(pattern.pattern == patterns[i]);
        CHECK_FALSE(options.watchers[i].kind);
    }

    lsp::DidChangeWatchedFilesParams changes{
        .changes = {
            {URI::fromFile(root / "top.sv"), lsp::FileChangeType::Created},
            {URI::fromFile(root / ".slang/server.json"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / "sub/header.svh"), lsp::FileChangeType::Deleted},
            {URI::fromFile(root / "flags.f"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / "sub/../top.v"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / ".slang/local/server.json"), lsp::FileChangeType::Created},
            {URI::fromFile(root / "sub/header.vh"), lsp::FileChangeType::Deleted},
            {URI::fromFile(root / "sub/flags.f"), lsp::FileChangeType::Deleted},
            {URI::fromFile(root / "unrelated.json"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / "nested/.slang/server.json"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / "folder.sv/other.txt"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / "../outside.sv"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / ".slang/unrelated.json"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / ".slang/local/unrelated.json"), lsp::FileChangeType::Changed},
            {URI::fromFile(root / ".slang/nested/server.json"), lsp::FileChangeType::Changed},
        }};
    watchers.dispatch(changes);

    const std::vector<std::vector<size_t>> expected{{0, 2, 4, 6, 11},
                                                    {1, 5, 8, 9, 12, 13, 14},
                                                    {3, 7}};
    for (size_t i = 0; i < batches.size(); ++i) {
        REQUIRE(batches[i].size() == expected[i].size());
        for (size_t j = 0; j < batches[i].size(); ++j) {
            CHECK(batches[i][j].uri == changes.changes[expected[i][j]].uri);
            CHECK(batches[i][j].type == changes.changes[expected[i][j]].type);
        }
    }
}

TEST_CASE("File watchers match the final file extension") {
    const auto [path, matches] = GENERATE(table<std::string, bool>({
        {"top.sv", true},
        {"sub/top.sv", true},
        {"sub/header.svh", true},
        {"top.sv.bak", false},
        {"folder.sv/other.txt", false},
        {"top.SV", false},
        {"top", false},
    }));
    CAPTURE(path);
    const auto root = std::filesystem::temp_directory_path() / "slang_watcher_extensions";
    lsp::FileWatchers watchers;
    bool called = false;
    watchers.addExtensions({".sv", ".svh"}, [&](const auto&) {
        called = true;
        return false;
    });
    watchers.dispatch({.changes = {{URI::fromFile(root / path), lsp::FileChangeType::Changed}}});
    CHECK(called == matches);
}

TEST_CASE("File watchers can consume a notification before later handlers") {
    const auto root = std::filesystem::temp_directory_path() / "slang_watcher_dispatch";
    const bool consume = GENERATE(false, true);
    lsp::FileWatchers watchers;
    size_t configCalls = 0;
    size_t sourceCalls = 0;
    watchers.addExtensions({".json"},
                           [&](const auto&) {
                               ++configCalls;
                               return consume;
                           },
                           {".slang/server.json", ".slang/local/server.json"});
    watchers.addExtensions({".sv"}, [&](const auto&) {
        ++sourceCalls;
        return false;
    });
    watchers.dispatch({});
    CHECK(configCalls == 0);
    CHECK(sourceCalls == 0);

    watchers.dispatch(
        {.changes = {{URI::fromFile(root / "top.sv"), lsp::FileChangeType::Changed},
                     {URI::fromFile(root / ".slang/server.json"), lsp::FileChangeType::Changed},
                     {URI::fromFile(root / ".slang/local/server.json"),
                      lsp::FileChangeType::Changed}}});
    CHECK(configCalls == 1);
    CHECK(sourceCalls == (consume ? 0 : 1));
}

TEST_CASE("File watchers accept absolute source paths without a workspace") {
    lsp::FileWatchers watchers;
    size_t calls = 0;
    watchers.addExtensions({".sv"}, [&](const auto&) {
        ++calls;
        return false;
    });
    watchers.dispatch(
        {.changes = {{URI::fromFile(std::filesystem::temp_directory_path() / "top.sv"),
                      lsp::FileChangeType::Changed}}});
    CHECK(calls == 1);
}
