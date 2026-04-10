// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

static std::optional<lsp::CodeAction> getCodeActionAt(ServerHarness& server, DocumentHandle& doc,
                                                      lsp::uint offset) {
    auto pos = doc.getPosition(offset);
    auto result = server.getDocCodeAction(lsp::CodeActionParams{
        .textDocument = {.uri = doc.m_uri},
        .range = {.start = pos, .end = pos},
        .context = {.diagnostics = {}},
    });
    if (!result || result->empty())
        return std::nullopt;
    return rfl::get<lsp::CodeAction>(result->front());
}

TEST_CASE("CodeAction_ExpandSimpleMacro") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define WIDTH 8
module top;
    logic [`WIDTH-1:0] data;
endmodule
)");

    auto action = getCodeActionAt(server, doc, doc.before("`WIDTH").m_offset);
    REQUIRE(action.has_value());
    CHECK(action->title == "Expand macro");
    REQUIRE(action->edit.has_value());

    auto& changes = action->edit->changes.value();
    auto it = changes.find(doc.m_uri.str());
    REQUIRE(it != changes.end());
    CHECK(it->second[0].newText == "8");
}

TEST_CASE("CodeAction_ExpandFunctionMacro") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define ADD(a, b) a + b
module top;
    localparam int x = `ADD(3, 4);
endmodule
)");

    auto action = getCodeActionAt(server, doc, doc.before("`ADD").m_offset);
    REQUIRE(action.has_value());

    auto& changes = action->edit->changes.value();
    auto& edits = changes[doc.m_uri.str()];
    CHECK(edits[0].newText.find("3 + 4") != std::string::npos);
}

TEST_CASE("CodeAction_ExpandMacroInMacroArg") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define VAL 42
`define USE(x) x
module top;
    localparam int a = `USE(`VAL);
endmodule
)");

    // Code action on the outer `USE
    auto outerAction = getCodeActionAt(server, doc, doc.before("`USE").m_offset);
    REQUIRE(outerAction.has_value());

    auto& outerChanges = outerAction->edit->changes.value();
    auto& outerEdits = outerChanges[doc.m_uri.str()];
    // The outer expansion should include the inner macro as-is
    CHECK(outerEdits[0].newText.find("42") != std::string::npos);
}

TEST_CASE("CodeAction_NoActionOnNonMacro") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    logic x;
endmodule
)");

    auto action = getCodeActionAt(server, doc, doc.before("logic").m_offset);
    CHECK(!action.has_value());
}

TEST_CASE("CodeAction_AddDefine_UndefinedMacro") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`ifdef UNDEFINED_MACRO
`endif
)");

    auto action = getCodeActionAt(server, doc, doc.after("`ifdef ").m_offset);
    REQUIRE(action.has_value());
    CHECK(action->title == "Add define 'UNDEFINED_MACRO' to local flags");
    CHECK(action->kind == lsp::CodeActionKind::from_name<"quickfix">());
    REQUIRE(action->command.has_value());
    CHECK(action->command->command == "slang.addDefine");
}

TEST_CASE("CodeAction_AddDefine_DefinedMacro") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define MY_MACRO 1
`ifdef MY_MACRO
`endif
)");

    // Should NOT show add-define action when macro is already defined
    auto action = getCodeActionAt(server, doc, doc.after("`ifdef ").m_offset);
    CHECK(!action.has_value());
}

TEST_CASE("CodeAction_ExpandConcatenation") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define MAKE_SIG(name) sig_``name
module top;
    logic `MAKE_SIG(foo);
endmodule
)");

    auto action = getCodeActionAt(server, doc, doc.before("`MAKE_SIG").m_offset);
    REQUIRE(action.has_value());

    auto& changes = action->edit->changes.value();
    auto& edits = changes[doc.m_uri.str()];
    CHECK(edits[0].newText.find("sig_foo") != std::string::npos);
}
