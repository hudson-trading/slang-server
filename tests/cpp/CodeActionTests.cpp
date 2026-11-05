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

TEST_CASE("CodeAction_AddInclude_UndefinedMacro") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv", R"(// License comment

module top(output logic [`VALUE_WIDTH-1:0] value);
    assign value = '0;
endmodule
)");
    auto action = getCodeActionAt(server, doc, doc.before("VALUE_WIDTH").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"types/detail/width.svh\"");
    CHECK(action->kind == "quickfix");
    REQUIRE(action->edit);
    auto edits = action->edit->changes->at(doc.m_uri.str());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].range.start.line == 2);
    CHECK(edits[0].newText == "`include \"types/detail/width.svh\"\n");
    doc.replaceAll(doc.withTextEdits(edits));
    doc.publishChanges();
    CHECK(doc.getDiagnostics().empty());
    CHECK(doc.doc->getAnalysis()->macros.contains("VALUE_WIDTH"));

    action = getCodeActionAt(server, doc, doc.before("`VALUE_WIDTH").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Expand macro");
}

TEST_CASE("CodeAction_AddInclude_HeaderType") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv", R"(module top(output value_t value);
    assign value = '0;
endmodule
)");
    auto action = getCodeActionAt(server, doc, doc.before("value_t").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"types/value.svh\"");
    REQUIRE(action->edit);
    doc.replaceAll(doc.withTextEdits(action->edit->changes->at(doc.m_uri.str())));
    doc.publishChanges();
    CHECK(doc.getDiagnostics().empty());
    CHECK_FALSE(getCodeActionAt(server, doc, doc.before("value_t").m_offset));
}

TEST_CASE("CodeAction_AddInclude_HeaderClass") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv", "module top; header_item item; endmodule\n");
    auto action = getCodeActionAt(server, doc, doc.before("header_item").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"types/value.svh\"");
    REQUIRE(action->edit);
    doc.replaceAll(doc.withTextEdits(action->edit->changes->at(doc.m_uri.str())));
    doc.publishChanges();
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
    CHECK(doc.before("header_item").hasDefinition());
}

TEST_CASE("CodeAction_AddInclude_DeclarationScope") {
    ServerHarness server("include_index");
    std::string text;
    SECTION("Package member") {
        text = "package consumer;\n    value_t value;\nendpackage\n";
    }
    SECTION("Class in a package") {
        text = "package consumer;\n    class item; value_t value; endclass\nendpackage\n";
    }
    SECTION("Module port after an include in another module") {
        text = R"(module first;
`include "types/detail/width.svh"
endmodule
module second(output value_t value);
    assign value = '0;
endmodule
)";
    }
    SECTION("Module body after local declarations") {
        text = R"(module consumer(output logic [7:0] result);
    localparam int WIDTH = 8;
    value_t value;
    assign value = WIDTH;
    assign result = value;
endmodule
)";
    }
    auto doc = server.openFile("consumer.sv", text);
    auto action = getCodeActionAt(server, doc, doc.before("value_t").m_offset);
    REQUIRE(action);
    REQUIRE(action->edit);
    doc.replaceAll(doc.withTextEdits(action->edit->changes->at(doc.m_uri.str())));
    doc.publishChanges();
    INFO(doc.getText());
    CHECK(doc.getDiagnostics().empty());
    CHECK(doc.before("value_t").hasDefinition());
    CHECK_FALSE(getCodeActionAt(server, doc, doc.before("value_t").m_offset));
}

TEST_CASE("CodeAction_AddInclude_AmbiguousHeaderNames") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv",
                               "module top; localparam int v = `DUPLICATE_VALUE; endmodule\n");
    auto pos = doc.before("`DUPLICATE_VALUE").getPosition();
    auto actions = server.getDocCodeAction(
        {.textDocument = {doc.m_uri}, .range = {pos, pos}, .context = {.diagnostics = {}}});
    REQUIRE(actions);
    REQUIRE(actions->size() == 2);
    CHECK(rfl::get<lsp::CodeAction>((*actions)[0]).title == "Add `include \"a/duplicate.svh\"");
    CHECK(rfl::get<lsp::CodeAction>((*actions)[1]).title == "Add `include \"b/duplicate.svh\"");
    auto& action = rfl::get<lsp::CodeAction>((*actions)[1]);
    REQUIRE(action.edit);
    doc.replaceAll(doc.withTextEdits(action.edit->changes->at(doc.m_uri.str())));
    doc.publishChanges();
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
    CHECK(doc.doc->getAnalysis()->macros.contains("DUPLICATE_VALUE"));
}

TEST_CASE("CodeAction_AddInclude_ConditionalMacroAndLineEndings") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv", "/* License */\r\n`ifdef VALUE_WIDTH\r\n`endif\r\n");
    auto action = getCodeActionAt(server, doc, doc.before("VALUE_WIDTH").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"types/detail/width.svh\"");
    REQUIRE(action->edit);
    auto edits = action->edit->changes->at(doc.m_uri.str());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].range.start.line == 1);
    CHECK(edits[0].newText == "`include \"types/detail/width.svh\"\r\n");
}

TEST_CASE("CodeAction_AddInclude_DoesNotDuplicateIncludesOrExposeClassMembers") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv", R"(`include "types/detail/width.svh"
`undef VALUE_WIDTH
module top;
    localparam int v = `VALUE_WIDTH;
    private_member item;
endmodule
)");
    CHECK_FALSE(getCodeActionAt(server, doc, doc.before("`VALUE_WIDTH").m_offset));
    CHECK_FALSE(getCodeActionAt(server, doc, doc.before("private_member").m_offset));
}

TEST_CASE("CodeAction_AddInclude_PreservesConfiguredSearchRootAfterRemoval") {
    ServerHarness server("include_paths");
    SECTION("Configured incdirs") {
        auto config = server.getConfig();
        config.incdirs = std::vector<std::string>{"."};
        server.loadConfig(config);
    }
    SECTION("Configured user directories") {
        server.sourceManager().addUserDirectories(fs::current_path().string());
    }
    SECTION("Configured additional directories") {
        auto options = server.m_driver->options.getOrDefault<slang::parsing::PreprocessorOptions>();
        options.additionalIncludePaths.push_back(fs::current_path());
        server.m_driver->options.set(options);
    }
    auto doc = server.openFile("design/top.sv");
    CHECK(doc.getDiagnostics().empty());
    auto directories = server.m_indexer.getIncludeDirectories();
    doc.replaceAll("module top(output int width); assign width = `COMMON_WIDTH; endmodule\n");
    doc.save();

    auto action = getCodeActionAt(server, doc, doc.before("`COMMON_WIDTH").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"library/common/common.svh\"");
    REQUIRE(action->edit);
    doc.replaceAll(doc.withTextEdits(action->edit->changes->at(doc.m_uri.str())));
    doc.publishChanges();
    CHECK(doc.getDiagnostics().empty());
    CHECK(server.m_indexer.getIncludeDirectories() == directories);
}

TEST_CASE("CodeAction_AddInclude_UsesConfiguredSpelling") {
    ServerHarness server("include_paths");
    server.sourceManager().addUserDirectories(fs::current_path().parent_path().string());
    auto doc = server.openFile(
        "design/consumer.sv", "module consumer; localparam int width = `COMMON_WIDTH; endmodule\n");
    auto action = getCodeActionAt(server, doc, doc.before("`COMMON_WIDTH").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"include_paths/library/common/common.svh\"");
}

TEST_CASE("CodeAction_AddInclude_AllowsBasenameWhenConfigured") {
    ServerHarness server("include_paths");
    server.sourceManager().addUserDirectories((fs::current_path() / "library/common").string());
    auto doc = server.openFile(
        "design/consumer.sv", "module consumer; localparam int width = `COMMON_WIDTH; endmodule\n");
    auto action = getCodeActionAt(server, doc, doc.before("`COMMON_WIDTH").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Add `include \"common.svh\"");
}

TEST_CASE("CodeAction_AddInclude_InsertsAfterEarlierIncludesBeforeMacroUse") {
    ServerHarness server("include_paths");
    auto earlier = server.openFile("design/earlier.svh", R"(`ifdef COMMON_WIDTH
`define EARLIER_VALUE 2
`else
`define EARLIER_VALUE 1
`endif
)");
    auto doc = server.openFile("design/consumer.sv", R"(// License
`include "earlier.svh" // Keep this comment
module consumer(output int width);
    assign width = `COMMON_WIDTH;
endmodule
`include "../local/defs.svh"
)");
    auto action = getCodeActionAt(server, doc, doc.before("`COMMON_WIDTH").m_offset);
    REQUIRE(action);
    REQUIRE(action->edit);
    auto edits = action->edit->changes->at(doc.m_uri.str());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].range.start == lsp::Position{2, 0});
    doc.replaceAll(doc.withTextEdits(edits));
    doc.publishChanges();
    CHECK(doc.getDiagnostics().empty());
    auto macro = doc.doc->getAnalysis()->macros.at("EARLIER_VALUE");
    REQUIRE(macro->body.size() == 1);
    CHECK(macro->body.front().valueText() == "1");
}

TEST_CASE("CodeAction_AddInclude_RespectsRequestedActionKind") {
    ServerHarness server("include_index");
    auto doc = server.openFile("top.sv",
                               "module top; localparam int v = `VALUE_WIDTH; endmodule\n");
    auto pos = doc.before("`VALUE_WIDTH").getPosition();
    auto actions = server.getDocCodeAction(
        {.textDocument = {doc.m_uri},
         .range = {pos, pos},
         .context = {.diagnostics = {}, .only = {{"refactor"}}}});
    CHECK((!actions || actions->empty()));
}

TEST_CASE("LspExtensibleStringEnums_AllowExtensionKinds") {
    auto params = rfl::json::read<lsp::InitializeParams>(R"(
{
  "capabilities": {
    "textDocument": {
      "codeAction": {
        "codeActionLiteralSupport": {
          "codeActionKind": {
            "valueSet": ["quickfix", "refactor.move"]
          }
        }
      },
      "foldingRange": {
        "foldingRangeKind": {
          "valueSet": ["comment", "custom.region"]
        }
      }
    }
  }
}
)");

    REQUIRE(params);

    const auto& valueSet = params.value()
                               .capabilities.textDocument->codeAction->codeActionLiteralSupport
                               ->codeActionKind.valueSet;
    REQUIRE(valueSet.size() == 2);
    CHECK(valueSet[0] == lsp::CodeActionKindOptions::from_name<"quickfix">().str());
    CHECK(valueSet[1] == "refactor.move");

    const auto& foldingValueSet =
        params.value().capabilities.textDocument->foldingRange->foldingRangeKind->valueSet.value();
    REQUIRE(foldingValueSet.size() == 2);
    CHECK(foldingValueSet[0] == lsp::FoldingRangeKindOptions::from_name<"comment">().str());
    CHECK(foldingValueSet[1] == "custom.region");
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

TEST_CASE("CodeAction_ExpandTrailingHeaderMacro") {
    ServerHarness server("header_context");
    std::string text;
    std::string expansion = "payload_t payload;";
    SECTION("Direct expansion") {
        text = "`define MEMBER(n) payload_t n;\n`MEMBER(payload)\n";
    }
    SECTION("Nested expansion") {
        text = "`define INNER(n) payload_t n;\n`define MEMBER(n) `INNER(n)\n`MEMBER(payload)\n";
    }
    SECTION("Builtin type with optional syntax tokens") {
        text = "`define MEMBER(n) bit [7:0] n;\n`MEMBER(payload)\n";
        expansion = "bit [7:0] payload;";
    }
    auto doc = server.openFile("src/fields.svh", text);
    auto action = getCodeActionAt(server, doc, doc.before("`MEMBER(payload)").m_offset);
    REQUIRE(action);
    CHECK(action->title == "Expand macro");
    REQUIRE(action->edit);
    const auto& edits = action->edit->changes->at(doc.m_uri.str());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].newText == expansion);
    doc.replaceAll(doc.withTextEdits(edits));
    doc.publishChanges();
    CHECK(doc.getDiagnostics().empty());
    auto package = server.openFile("device_pkg.sv");
    CHECK(package.getDiagnostics().empty());
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
    CHECK(action->kind == lsp::CodeActionKindOptions::from_name<"quickfix">().str());
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
