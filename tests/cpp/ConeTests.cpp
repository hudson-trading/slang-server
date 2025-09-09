// SPDX-FileCopyrightText: Hudson River Trading
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

    const std::string file("test2.sv");
    auto uri = URI::fromFile(fs::absolute(file));
    auto doc = server.openFile(file);

    // TODO -- prepare for interfaces -- list signals?

    SECTION("Prepare Multiple") {
        auto cursor = doc.before("x <= a + b;");
        server.checkPrepareCallHierarchy(cursor, {"test.the_sub_1.x", "test.the_sub_2.x"});
    }

    SECTION("Prepare Empty") {
        auto cursor = doc.begin();
        server.checkPrepareCallHierarchy(cursor, {});
    }

    SECTION("Prepare Single") {
        auto cursor = doc.before("a,");
        server.checkPrepareCallHierarchy(cursor, {"test.a"});
    }

    SECTION("Incoming Multiple") {
        auto cursor_a = doc.before("a + b;");
        auto cursor_b = doc.before("b;");
        server.checkIncomingCalls("test.the_sub_2.x", {{"test.the_sub_2.a", &cursor_a},
                                                       {"test.the_sub_2.b", &cursor_b}});
    }

    SECTION("Incoming Single") {
        // Temporarily revert to test method
        auto incoming = [&](const std::string& path) {
            auto result = server.getCallHierarchyIncomingCalls(
                lsp::CallHierarchyIncomingCallsParams{.item = {.name = path}});
            return result;
        };
        auto checkIncoming = [&](const std::vector<lsp::CallHierarchyIncomingCall>& incomings,
                                 const std::set<HierResult>& expected) {
            std::set<HierResult> got;
            for (const auto& incoming : incomings) {
                CHECK(incoming.fromRanges.size() == 1);
                got.insert({.name = incoming.from.name,
                            .line = incoming.fromRanges[0].start.line,
                            .character = incoming.fromRanges[0].start.character});
            }
            CHECK(got == expected);
        };
        auto result = *incoming("test.the_sub_2.b");
        checkIncoming(result, {{.name = "test.x1", .line = 34, .character = 12}});
    }

    SECTION("Incoming Single2") {
        // This points at the port declartion.  It would be more consistent to point at the
        // port map instead, but that location information doesn't appear to be attached to
        // PortSymbol
        auto cursor = doc.after("module sub").after("output logic [31:0] ");
        server.checkIncomingCalls("test.x1", {{"test.the_sub_1.x", &cursor}});
    }

    SECTION("Incoming Constant") {
        auto cursor_foo = doc.before("foo) begin");
        auto cursor_bar = doc.before("bar;");
        server.checkIncomingCalls("test.the_sub_2.the_sub_sub.result",
                                  {{"test.the_sub_2.the_sub_sub.foo", &cursor_foo},
                                   {"test.the_sub_2.the_sub_sub.bar", &cursor_bar}});
    }

    SECTION("Incoming Switched") {
        auto cursor_bar = doc.before("bar)");
        auto cursor_foo = doc.before("foo;");
        server.checkIncomingCalls("test.the_sub_2.the_sub_sub.switched_result",
                                  {{"test.the_sub_2.the_sub_sub.bar", &cursor_bar},
                                   {"test.the_sub_2.the_sub_sub.foo", &cursor_foo}});
    }

    SECTION("Incoming Interface") {
        auto cursor_qux = doc.before("qux_in.qux + b;");
        auto cursor_b = doc.after("qux_out.qux = ").before("b;");
        server.checkIncomingCalls("test.the_intfs[2].qux", {{"test.the_intfs[1].qux", &cursor_qux},
                                                            {"test.the_sub_2.b", &cursor_b}});
    }

    SECTION("Incoming Interface Reference") {
        auto cursor_qux = doc.before("qux_in.qux + b;");
        auto cursor_b = doc.after("qux_out.qux = ").before("b;");
        server.checkIncomingCalls("test.the_sub_1.qux_out.qux",
                                  {{"test.the_intfs[0].qux", &cursor_qux},
                                   {"test.the_sub_1.b", &cursor_b}});
    }

    SECTION("Outgoing Multiple") {
        auto cursor = doc.after("module sub(").before("a,");
        server.checkOutgoingCalls("test.a",
                                  {{"test.the_sub_2.a", &cursor}, {"test.the_sub_1.a", &cursor}});
    }

    SECTION("Outgoing Up Down") {
        auto cursor_x = doc.before("x <= a + b;");
        auto cursor_foo = doc.before("foo,");
        server.checkOutgoingCalls("test.the_sub_2.a",
                                  {{"test.the_sub_2.x", &cursor_x},
                                   {"test.the_sub_2.the_sub_sub.foo", &cursor_foo}});
    }

    SECTION("Outgoing Single") {
        auto cursor = doc.before("x),");
        server.checkOutgoingCalls("test.the_sub_2.x", {{"test.x", &cursor}});
    }

    SECTION("Outgoing Conditional") {
        auto cursor_result1 = doc.before("result <= bar;");
        auto cursor_result2 = doc.before("result <= '1;");
        auto cursor_switched = doc.before("switched_result = foo;");
        server.checkOutgoingCalls("test.the_sub_1.the_sub_sub.foo",
                                  {{"test.the_sub_1.the_sub_sub.result", &cursor_result1},
                                   {"test.the_sub_1.the_sub_sub.result", &cursor_result2},
                                   {"test.the_sub_1.the_sub_sub.switched_result",
                                    &cursor_switched}});
    }

    SECTION("Outgoing Switched") {
        auto cursor_result = doc.before("result <= bar;");
        auto cursor_switched1 = doc.before("switched_result = foo;");
        auto cursor_switched2 = doc.before("switched_result = 1'b0;");
        server.checkOutgoingCalls(
            "test.the_sub_2.the_sub_sub.bar",
            {{"test.the_sub_2.the_sub_sub.result", &cursor_result},
             {"test.the_sub_2.the_sub_sub.switched_result", &cursor_switched1},
             {"test.the_sub_2.the_sub_sub.switched_result", &cursor_switched2}});
    }

    SECTION("Outgoing Interface") {
        auto cursor = doc.before("qux_in.quz = qux_out.quz;");
        server.checkOutgoingCalls("test.the_intfs[1].quz", {{"test.the_intfs[0].quz", &cursor}});
    }

    SECTION("Outgoing Interface Reference") {
        auto cursor = doc.before("qux_out.qux = qux_in.qux + b;");
        server.checkOutgoingCalls("test.the_sub_1.qux_out.qux",
                                  {{"test.the_intfs[2].qux", &cursor}});
    }
}
