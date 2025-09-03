// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

TEST_CASE("WCP Tests") {
    ServerHarness server("");

    server.loadConfig(Config{.build = "test3.f"});

    // This will actually load the compilation
    server.onInitialized(lsp::InitializedParams{});

    auto uri = URI::fromFile(fs::absolute("test3.sv"));

    auto getInstances = [&](uint line, uint character) {
        auto result = server.getInstances(lsp::TextDocumentPositionParams{
            .textDocument = {uri},
            .position = lsp::Position{.line = line, .character = character}});
        return result;
    };

    auto checkInstances = [](const std::vector<std::string>& items,
                             const std::set<std::string>& expected) {
        std::set<std::string> got;
        for (const auto& item : items) {
            got.insert(item);
        }
        CHECK(got == expected);
    };

    SECTION("Instances No Results") {
        auto result = getInstances(0, 0);
        CHECK(result.size() == 0);
    }

    SECTION("Instances Declaration") {
        auto result = getInstances(1, 10);
        checkInstances(result, {{"test.foo"}});
    }

    SECTION("Instances Reference") {
        auto result = getInstances(2, 16);
        checkInstances(result, {{"test.foo"}});
    }

    SECTION("Instances Multiple") {
        auto result = getInstances(10, 18);
        checkInstances(result, {{"test.the_sub_1.baz"}, {"test.the_sub_2.baz"}});
    }

    SECTION("Instances Interface Instance") {
        auto result = getInstances(12, 9);
        checkInstances(result, {{"test.the_sub_1.the_intf_1"}, {"test.the_sub_2.the_intf_1"}});
    }

    SECTION("Instances Interface Reference") {
        auto result = getInstances(56, 10);
        checkInstances(result, {{"test.the_other_sub.the_sub_w_intf.intf_port"}});
    }

    // TODO -- this works differently than member selects below, which way should this work?
    SECTION("Instances Interface Modport Reference Signal") {
        auto result = getInstances(59, 17);
        checkInstances(result, {{"test.the_other_sub.the_sub_w_intf.all_in_port.def"}});
    }

    SECTION("Instances Interface Reference Signal") {
        auto result = getInstances(60, 17);
        checkInstances(result, {{"test.the_other_sub.the_sub_w_intf.intf_port.abc"}});
    }

    SECTION("Instances Interface Modport Reference Signal Genscope") {
        auto result = getInstances(63, 28);
        checkInstances(result, {{"test.the_other_sub.the_sub_w_intf.all_in_port.abc"}});
    }

    SECTION("Instances Interface Signal") {
        auto result = getInstances(17, 10);
        checkInstances(result, {
                                   {"test.the_sub_1.the_intf_1.sig1"},
                                   {"test.the_sub_1.the_intf_2.sig1"},
                                   {"test.the_sub_2.the_intf_1.sig1"},
                                   {"test.the_sub_2.the_intf_2.sig1"},
                               });
    }

    SECTION("Instances Fields") {
        auto result = getInstances(34, 27);
        checkInstances(result, {{"test.the_other_sub.t1.t2.abc"}});
    }

    SECTION("Instances Aggregate Field") {
        auto result = getInstances(34, 24);
        checkInstances(result, {{"test.the_other_sub.t1.t2"}});
    }

    SECTION("Instances Aggregate Var") {
        auto result = getInstances(34, 21);
        checkInstances(result, {{"test.the_other_sub.t1"}});
    }

    // TODO -- slice (elements and ranges) vs whole array
    SECTION("Instances Whole Array") {
        auto result = getInstances(45, 21);
        checkInstances(result, {{"test.the_other_sub.the_array"}});
    }

    SECTION("Instances Enum Var") {
        auto result = getInstances(42, 16);
        checkInstances(result, {{"test.the_other_sub.the_enum"}});
    }

    auto pathToDeclaration =
        [&](const std::string& path) -> std::optional<lsp::ShowDocumentParams> {
        server.pathToDeclaration(path);

        if (server.client.m_showDocuments.empty()) {
            return std::nullopt;
        }

        CHECK(server.client.m_showDocuments.size() == 1);

        auto result = std::optional(server.client.m_showDocuments.front());
        server.client.m_showDocuments.pop_front();

        return result;
    };

    SECTION("Goto Hit") {
        auto result = *pathToDeclaration("test.the_sub_2.baz");
        CHECK(result.uri == uri);
        CHECK(*(result.selection) == lsp::Range{.start = {.line = 9, .character = 17},
                                                .end = {.line = 9, .character = 20}});
    }

    SECTION("Goto Miss") {
        auto result = pathToDeclaration("blargh.ack");
        CHECK(!result);
    }

    SECTION("Goto Interface Signal") {
        auto result = *pathToDeclaration("test.the_sub_2.the_intf_1.sig1");
        CHECK(*(result.selection) == lsp::Range{.start = {.line = 17, .character = 10},
                                                .end = {.line = 17, .character = 14}});
    }

    // TODO -- goto declaration vs definition
    SECTION("Goto Field") {
        auto result = *pathToDeclaration("test.the_other_sub.t1.t2.def");
        CHECK(*(result.selection) == lsp::Range{.start = {.line = 33, .character = 18},
                                                .end = {.line = 33, .character = 20}});
    }

    SECTION("Goto Enum Var") {
        auto result = *pathToDeclaration("test.the_other_sub.the_enum");
        CHECK(*(result.selection) == lsp::Range{.start = {.line = 41, .character = 11},
                                                .end = {.line = 41, .character = 19}});
    }

    SECTION("Goto Array Slice") {
        auto result = *pathToDeclaration("test.the_other_sub.the_array[4]");
        CHECK(*(result.selection) == lsp::Range{.start = {.line = 44, .character = 17},
                                                .end = {.line = 44, .character = 26}});
    }

    SECTION("Is Var Var") {
        CHECK(server.m_driver->comp->isWcpVariable("test.foo"));
    }

    SECTION("Is Var Mod") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_sub_1"));
    }

    SECTION("Is Var Whole Struct") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_other_sub.t1"));
    }

    SECTION("Is Var Sub Struct") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_other_sub.t1.t2"));
    }

    SECTION("Is Var Struct Field") {
        CHECK(server.m_driver->comp->isWcpVariable("test.the_other_sub.t1.t2.abc"));
    }

    SECTION("Is Var Array") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_other_sub.the_array"));
    }

    SECTION("Is Var Slice") {
        CHECK(server.m_driver->comp->isWcpVariable("test.the_other_sub.the_array[4]"));
    }
}
