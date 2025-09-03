// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

struct HierResult {
    std::string name;
    uint line;
    uint character;

    auto operator<=>(const HierResult&) const = default;

    friend std::ostream& operator<<(std::ostream&, const HierResult&);
};

std::ostream& operator<<(std::ostream& os, const HierResult& result) {
    os << result.name << " L " << result.line << " C " << result.character;
    return os;
}

TEST_CASE("Cone Tracing") {
    ServerHarness server("");

    server.loadConfig(Config{.build = "test2.f"});

    // This will actually load the compilation
    server.onInitialized(lsp::InitializedParams{});

    auto uri = URI::fromFile(fs::absolute("test2.sv"));

    auto prepare = [&](uint line, uint character) {
        auto result = server.getDocPrepareCallHierarchy(lsp::CallHierarchyPrepareParams{
            .textDocument = {uri},
            .position = lsp::Position{.line = line, .character = character}});
        return result;
    };

    auto checkPreparation = [](const std::vector<lsp::CallHierarchyItem>& items,
                               const std::set<std::string>& expected) {
        std::set<std::string> got;
        for (const auto& item : items) {
            got.insert(item.name);
        }
        CHECK(got == expected);
    };

    // TODO -- prepare for interfaces -- list signals?

    SECTION("Prepare Multiple") {
        auto result = *prepare(51, 8);
        checkPreparation(result, {"test.the_sub_1.x", "test.the_sub_2.x"});
    }

    SECTION("Prepare Empty") {
        auto result = *prepare(0, 0);
        CHECK(result.empty());
    }

    SECTION("Prepare Single") {
        auto result = *prepare(16, 23);
        checkPreparation(result, {"test.a"});
    }

    auto incoming = [&](const std::string& path) {
        auto result = server.getCallHierarchyIncomingCalls(
            lsp::CallHierarchyIncomingCallsParams{.item = {.name = path}});
        return result;
    };

    auto checkIncoming = [&](const std::vector<lsp::CallHierarchyIncomingCall>& incomings,
                             const std::set<HierResult>& expected) {
        std::set<HierResult> got;
        for (const auto& incoming : incomings) {
            CHECK(incoming.from.uri == uri);
            CHECK(incoming.fromRanges.size() == 1);
            got.insert({.name = incoming.from.name,
                        .line = incoming.fromRanges[0].start.line,
                        .character = incoming.fromRanges[0].start.character});
        }
        CHECK(got == expected);
    };

    SECTION("Incoming Multiple") {
        auto result = *incoming("test.the_sub_2.x");
        checkIncoming(result, {{.name = "test.the_sub_2.a", .line = 51, .character = 13},
                               {.name = "test.the_sub_2.b", .line = 51, .character = 17}});
    }

    SECTION("Incoming Single") {
        auto result = *incoming("test.the_sub_2.b");
        checkIncoming(result, {{.name = "test.x1", .line = 34, .character = 12}});
    }

    SECTION("Incoming Single2") {
        auto result = *incoming("test.x1");
        // This points at the port declartion.  It would be more consistent to point at the
        // port map instead, but that location information doesn't appear to be attached to
        // PortSymbol
        checkIncoming(result, {{.name = "test.the_sub_1.x", .line = 44, .character = 24}});
    }

    SECTION("Incoming Constant") {
        auto result = *incoming("test.the_sub_2.the_sub_sub.result");
        checkIncoming(result,
                      {{.name = "test.the_sub_2.the_sub_sub.bar", .line = 76, .character = 22},
                       {.name = "test.the_sub_2.the_sub_sub.foo", .line = 75, .character = 12}});
    }

    SECTION("Incoming Switched") {
        auto result = *incoming("test.the_sub_2.the_sub_sub.switched_result");
        checkIncoming(result,
                      {{.name = "test.the_sub_2.the_sub_sub.bar", .line = 86, .character = 13},
                       {.name = "test.the_sub_2.the_sub_sub.foo", .line = 87, .character = 36}});
    }

    SECTION("Incoming Interface") {
        auto result = *incoming("test.the_intfs[2].qux");
        checkIncoming(result, {{.name = "test.the_intfs[1].qux", .line = 55, .character = 22},
                               {.name = "test.the_sub_2.b", .line = 55, .character = 35}});
    }

    SECTION("Incoming Interface Reference") {
        auto result = *incoming("test.the_sub_1.qux_out.qux");
        checkIncoming(result, {{.name = "test.the_intfs[0].qux", .line = 55, .character = 22},
                               {.name = "test.the_sub_1.b", .line = 55, .character = 35}});
    }

    auto outgoing = [&](const std::string& path) {
        auto result = server.getCallHierarchyOutgoingCalls(
            lsp::CallHierarchyOutgoingCallsParams{.item = {.name = path}});
        return result;
    };

    auto checkOutgoing = [&](const std::vector<lsp::CallHierarchyOutgoingCall>& outgoings,
                             const std::set<HierResult>& expected) {
        std::set<HierResult> got;
        for (const auto& outgoing : outgoings) {
            CHECK(outgoing.to.uri == uri);
            CHECK(outgoing.fromRanges.size() == 1);
            got.insert({.name = outgoing.to.name,
                        .line = outgoing.fromRanges[0].start.line,
                        .character = outgoing.fromRanges[0].start.character});
        }
        CHECK(got == expected);
    };

    SECTION("Outgoing Multiple") {
        auto result = *outgoing("test.a");
        checkOutgoing(result, {{.name = "test.the_sub_2.a", .line = 42, .character = 23},
                               {.name = "test.the_sub_1.a", .line = 42, .character = 23}});
    }

    SECTION("Outgoing Up Down") {
        auto result = *outgoing("test.the_sub_2.a");
        checkOutgoing(result,
                      {{.name = "test.the_sub_2.x", .line = 51, .character = 8},
                       {.name = "test.the_sub_2.the_sub_sub.foo", .line = 66, .character = 16}});
    }

    SECTION("Outgoing Single") {
        auto result = *outgoing("test.the_sub_2.x");
        checkOutgoing(result, {{.name = "test.x", .line = 35, .character = 12}});
    }

    SECTION("Outgoing Conditional") {
        auto result = *outgoing("test.the_sub_1.the_sub_sub.foo");
        checkOutgoing(
            result,
            {{.name = "test.the_sub_1.the_sub_sub.result", .line = 76, .character = 12},
             {.name = "test.the_sub_1.the_sub_sub.result", .line = 78, .character = 12},
             {.name = "test.the_sub_1.the_sub_sub.switched_result", .line = 87, .character = 18}});
    }

    SECTION("Outgoing Switched") {
        auto result = *outgoing("test.the_sub_2.the_sub_sub.bar");
        checkOutgoing(
            result,
            {{.name = "test.the_sub_2.the_sub_sub.result", .line = 76, .character = 12},
             {.name = "test.the_sub_2.the_sub_sub.switched_result", .line = 87, .character = 18},
             {.name = "test.the_sub_2.the_sub_sub.switched_result", .line = 88, .character = 18}});
    }

    SECTION("Outgoing Interface") {
        auto result = *outgoing("test.the_intfs[1].quz");
        checkOutgoing(result, {{.name = "test.the_intfs[0].quz", .line = 56, .character = 8}});
    }

    SECTION("Outgoing Interface Reference") {
        auto result = *outgoing("test.the_sub_1.qux_out.qux");
        checkOutgoing(result, {{.name = "test.the_intfs[2].qux", .line = 55, .character = 8}});
    }
}
