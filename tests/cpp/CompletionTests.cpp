// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "completions/CompletionContext.h"
#include "completions/InstanceCompletions.h"
#include "completions/SystemTaskCompletions.h"
#include "lsp/LspTypes.h"
#include "util/Logging.h"
#include "utils/CompletionCoverageScanner.h"
#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <rfl/Variant.hpp>
#include <string>
#include <vector>

#include "slang/ast/Compilation.h"

using namespace server;

using namespace slang;

namespace {

bool isSystemVerilogFile(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    return ext == ".sv" || ext == ".v";
}

const lsp::TextEdit& getCompletionTextEdit(const lsp::CompletionItem& item) {
    REQUIRE(item.textEdit.has_value());
    REQUIRE(rfl::holds_alternative<lsp::TextEdit>(*item.textEdit));
    return rfl::get<lsp::TextEdit>(*item.textEdit);
}

lsp::InitializeParams makeCompletionResolveParams(std::vector<std::string> properties,
                                                  std::string_view repoRoot = "repo1") {
    lsp::InitializeParams params;
    params.capabilities.textDocument.emplace();
    params.capabilities.textDocument->completion.emplace();
    params.capabilities.textDocument->completion->completionItem.emplace();
    params.capabilities.textDocument->completion->completionItem->resolveSupport =
        lsp::ClientCompletionItemResolveOptions{.properties = std::move(properties)};
    auto repoDir = findSlangRoot() / "tests/data" / repoRoot;
    params.workspaceFolders = {
        {lsp::WorkspaceFolder{.uri = URI::fromFile(repoDir), .name = "test"}}};
    return params;
}

} // namespace

TEST_CASE("MacroCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("test1.svh", R"(
    `define TEST_MACRO(arg1, arg2) \
        $display("arg1: %s, arg2: %s", arg1, arg2);
    `
    )");
    // For simplicity we add all defines in the current file.
    CHECK(doc.before("define").getCompletions("`").size() == 2);
    CHECK(doc.after(";\n    `").getCompletions("`").size() == 2);

    // Only return the indexed one
    auto doc2 = server.openFile("test2.sv", "`");
    auto macroCursor = doc2.end();
    auto rawResult = server.getDocCompletion(lsp::CompletionParams{
        .context =
            lsp::CompletionContext{
                .triggerKind = lsp::CompletionTriggerKind::TriggerCharacter,
                .triggerCharacter = "`",
            },
        .textDocument = lsp::TextDocumentIdentifier{macroCursor.getUri()},
        .position = macroCursor.getPosition(),
    });
    REQUIRE(rfl::holds_alternative<lsp::CompletionList>(rawResult));
    CHECK(rfl::get<lsp::CompletionList>(rawResult).isIncomplete);
    CHECK(rfl::get<lsp::CompletionList>(rawResult).items.size() == 1);

    // Now that it's saved, it should be indexed
    doc.save();
    auto indexedCompletions = doc2.end().getCompletions("`");
    CHECK(indexedCompletions.size() == 2);
    auto testMacro = std::ranges::find(indexedCompletions, "`TEST_MACRO",
                                       [](const CompletionHandle& item) {
                                           return item.m_item.label;
                                       });
    REQUIRE(testMacro != indexedCompletions.end());
    CHECK(!testMacro->m_item.insertText);
    CHECK(getCompletionTextEdit(testMacro->m_item).newText == "`TEST_MACRO");
    CHECK(!testMacro->m_item.documentation);

    testMacro->resolve();
    CHECK(testMacro->m_item.insertText == "`TEST_MACRO(${1:arg1}, ${2:arg2})");
    CHECK(testMacro->m_item.insertTextFormat == lsp::InsertTextFormat::Snippet);
    CHECK(testMacro->m_item.documentation);
    CHECK(getCompletionTextEdit(testMacro->m_item).newText == "`TEST_MACRO(${1:arg1}, ${2:arg2})");
}

TEST_CASE("MacroArgumentCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("macro_arg_completion.sv", R"(
    `define ASSIGN(lhs, rhs) assign lhs = rhs

    module top;
        logic source_signal;
        logic target_signal;

        `ASSIGN(target_signal, sour)
    endmodule
    )");

    auto comps = doc.after("`ASSIGN(target_signal, sour").getCompletions();
    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "source_signal";
    });
    REQUIRE(it != comps.end());
}

TEST_CASE("CompletionCoverageCompRepo") {
    ServerHarness server(makeCompletionResolveParams(
        {"documentation", "insertText", "insertTextFormat", "textEdit"}, "comp_repo"));
    CompletionCoverageScanner scanner;

    auto root = findSlangRoot() / "tests" / "data" / "comp_repo";
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && isSystemVerilogFile(entry.path())) {
            files.push_back(std::filesystem::relative(entry.path(), root));
        }
    }

    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        scanner.scanDocument(server.openFile(file.string()), file);
    }
}

TEST_CASE("SystemTaskCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("system_task_completion.sv", R"(
    module top;
        initial begin
            $
        end
    endmodule
    )");

    auto cursor = doc.after("$");
    auto comps = cursor.getCompletions("$");

    auto findByLabel = [](std::vector<CompletionHandle>& items, std::string_view label) {
        return std::find_if(items.begin(), items.end(), [&](const CompletionHandle& item) {
            return item.m_item.label == label;
        });
    };
    auto checkSystemTaskTextEdit = [](const CompletionHandle& item, const Cursor& cursor,
                                      std::string_view newText, lsp::uint replaceWidth) {
        auto edit = getCompletionTextEdit(item.m_item);
        auto cursorPosition = cursor.getPosition();
        CHECK(edit.newText == newText);
        CHECK(edit.range.end.line == cursorPosition.line);
        CHECK(edit.range.end.character == cursorPosition.character);
        CHECK(edit.range.start.line == cursorPosition.line);
        CHECK(edit.range.start.character == cursorPosition.character - replaceWidth);
    };

    auto display = findByLabel(comps, "$display");
    REQUIRE(display != comps.end());
    CHECK(display->m_item.filterText == "$display");
    CHECK(display->m_item.insertText == "\\$display(\"${1:format}\", $0)");
    checkSystemTaskTextEdit(*display, cursor, "\\$display(\"${1:format}\", $0)", 1);
    CHECK(display->m_item.insertTextFormat == lsp::InsertTextFormat::Snippet);
    REQUIRE(display->m_item.labelDetails);
    CHECK(display->m_item.labelDetails->detail == " task $display(string format = \"\", ...)");
    CHECK(display->m_item.documentation.has_value());

    auto fdisplay = findByLabel(comps, "$fdisplay");
    REQUIRE(fdisplay != comps.end());
    CHECK(fdisplay->m_item.filterText == "$fdisplay");
    CHECK(fdisplay->m_item.insertText == "\\$fdisplay(${1:fd}, \"${2:format}\", $0)");
    CHECK(fdisplay->m_item.insertTextFormat == lsp::InsertTextFormat::Snippet);

    auto fatal = findByLabel(comps, "$fatal");
    REQUIRE(fatal != comps.end());
    CHECK(fatal->m_item.insertText == "\\$fatal()");
    REQUIRE(fatal->m_item.labelDetails);
    CHECK(fatal->m_item.labelDetails->detail ==
          " task $fatal(int finish_number = 1, string format = \"\", ...)");

    auto clog2 = findByLabel(comps, "$clog2");
    REQUIRE(clog2 != comps.end());
    CHECK(clog2->m_item.filterText == "$clog2");
    CHECK(clog2->m_item.insertText == "\\$clog2(${1:integer_value})");
    CHECK(clog2->m_item.insertTextFormat == lsp::InsertTextFormat::Snippet);
    REQUIRE(clog2->m_item.labelDetails);
    CHECK(clog2->m_item.labelDetails->detail == " function int $clog2(integer_value)");

    auto testPlusArgs = findByLabel(comps, "$test$plusargs");
    REQUIRE(testPlusArgs != comps.end());
    CHECK(testPlusArgs->m_item.filterText == "$test$plusargs");
    CHECK(testPlusArgs->m_item.insertText == "\\$test\\$plusargs(${1:user_string})");
    CHECK(testPlusArgs->m_item.insertTextFormat == lsp::InsertTextFormat::Snippet);

    auto fopen = findByLabel(comps, "$fopen");
    REQUIRE(fopen != comps.end());
    CHECK(fopen->m_item.insertText == "\\$fopen(${1:filename})");
    REQUIRE(fopen->m_item.labelDetails);
    CHECK(fopen->m_item.labelDetails->detail ==
          " function int $fopen(string filename[, string type])");

    auto urandomRange = findByLabel(comps, "$urandom_range");
    REQUIRE(urandomRange != comps.end());
    CHECK(urandomRange->m_item.insertText == "\\$urandom_range(${1:maxval})");
    REQUIRE(urandomRange->m_item.labelDetails);
    CHECK(urandomRange->m_item.labelDetails->detail ==
          " function bit [31:0] $urandom_range(bit [31:0] maxval[, bit [31:0] minval])");

    auto fflush = findByLabel(comps, "$fflush");
    REQUIRE(fflush != comps.end());
    CHECK(fflush->m_item.insertText == "\\$fflush()");

    CHECK(findByLabel(comps, "randomize") == comps.end());

    auto prefixDoc = server.openFile("system_task_prefix_completion.sv", R"(
    module top;
        initial begin
            $dis
        end
    endmodule
    )");

    auto prefixCursor = prefixDoc.after("$dis");
    auto prefixComps = prefixCursor.getCompletions();
    auto prefixDisplay = findByLabel(prefixComps, "$display");
    REQUIRE(prefixDisplay != prefixComps.end());
    CHECK(prefixDisplay->m_item.filterText == "$display");
    checkSystemTaskTextEdit(*prefixDisplay, prefixCursor, "\\$display(\"${1:format}\", $0)", 4);

    auto internalDollarDoc = server.openFile("system_task_internal_dollar_completion.sv", R"(
    module top;
        initial begin
            $test$p
        end
    endmodule
    )");

    auto internalDollarCursor = internalDollarDoc.after("$test$p");
    auto internalDollarComps = internalDollarCursor.getCompletions();
    auto internalDollarTestPlusArgs = findByLabel(internalDollarComps, "$test$plusargs");
    REQUIRE(internalDollarTestPlusArgs != internalDollarComps.end());
    CHECK(internalDollarTestPlusArgs->m_item.filterText == "$test$plusargs");
    checkSystemTaskTextEdit(*internalDollarTestPlusArgs, internalDollarCursor,
                            "\\$test\\$plusargs(${1:user_string})", 7);
}

TEST_CASE("SystemMethodCompletionSnippets") {
    ast::Compilation compilation;

    auto checkMethod = [&](parsing::KnownSystemName name, ast::SymbolKind typeKind,
                           std::string_view methodName, std::string_view expectedSnippet,
                           std::string_view expectedDetail = {}) {
        auto* method = compilation.getSystemMethod(typeKind, methodName);
        REQUIRE(method != nullptr);

        auto item = completions::getSystemSubroutineCompletion(name, *method);
        CHECK(item.insertText == expectedSnippet);
        CHECK(item.insertTextFormat == lsp::InsertTextFormat::Snippet);
        if (!expectedDetail.empty()) {
            REQUIRE(item.labelDetails);
            CHECK(item.labelDetails->detail == expectedDetail);
        }
    };

    checkMethod(parsing::KnownSystemName::Len, ast::SymbolKind::StringType, "len", "len()");
    checkMethod(parsing::KnownSystemName::Putc, ast::SymbolKind::StringType, "putc",
                "putc(${1:i}, ${2:c})");
    checkMethod(parsing::KnownSystemName::PushBack, ast::SymbolKind::QueueType, "push_back",
                "push_back(${1:value})");
    checkMethod(parsing::KnownSystemName::RandMode, ast::SymbolKind::ClassProperty, "rand_mode",
                "rand_mode(${1:on_off})");
    checkMethod(parsing::KnownSystemName::Sort, ast::SymbolKind::QueueType, "sort", "sort()",
                " function void array.sort([with (item.expr)])");
    checkMethod(parsing::KnownSystemName::Find, ast::SymbolKind::QueueType, "find",
                "find with (${1:item.expr})");
}

TEST_CASE("ModuleCompletion") {

    ServerHarness server("repo1");

    auto doc = server.openFile("test1.sv", R"(
    module test1 #(
        parameter int PARAM = 42,
    )(
        input logic clk,
        input rst,
    );
        initial begin
            $display("Hello, World!");
        end
    endmodule
    )");

    auto doc2 = server.openFile("test2.sv", R"(
        module test2;
        //inmodule

        endmodule
    )");

    auto cursor = doc2.before("//inmodule");
    // CHECK(cursor.getCompletions().size() == 2);
    RFL_INFO(cursor.getResolvedCompletions());
    // Check that the module is indexed after saving
    doc.save();
    auto comps = cursor.getCompletions();
    // Other completions from the workspace

    auto it = std::find_if(comps.begin(), comps.end(),
                           [](const CompletionHandle& item) { return item.m_item.label == "Dut"; });

    REQUIRE(it != comps.end());
    auto comp = *it;
    comp.resolve();
    CHECK(comp.m_item.insertText == R"(Dut #(
    .a(${1:a /* default 0 */}),
    .b(${2:b /* default 1 */})
 ) ${3:dut} (
    .foo(${4:foo})
);)");
}

TEST_CASE("ModuleCompletionInvalidUtf8") {
    std::string text = "\n// ";
    text.append("\xb2\xe2\xca\xd4", 4);
    text += "\nmodule invalid_encoding(input logic value); endmodule";

    auto tree = syntax::SyntaxTree::fromText(text);
    auto item = completions::InstanceCompletionQuery::getCompletion(
        "invalid_encoding", syntax::SyntaxKind::ModuleDeclaration);
    completions::InstanceCompletionQuery::resolve(*tree, "invalid_encoding", item);

    REQUIRE(item.documentation);
    auto& documentation = rfl::get<lsp::MarkupContent>(*item.documentation);
    CHECK(documentation.value.find("\\xb2\\xe2\\xca\\xd4") != std::string::npos);
    CHECK_NOTHROW(rfl::json::write(item));
}

TEST_CASE("PackageCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("package_test.sv", R"(
    package test_pkg;
        parameter int PKG_PARAM = 10;

        typedef struct {
            int field1;
            logic field2;
        } my_struct_t;

        typedef enum {
            VALUE_A,
            VALUE_B
        } my_enum_t;

        function int get_value();
            return 42;
        endfunction

        int global_var = 5;

        logic [7:0] port_signal;

        // Generate block for Snippet completion kind
        generate
            genvar i;
            for (i = 0; i < 4; i++) begin : gen_block
                logic [7:0] gen_var;
            end
        endgenerate
    endpackage

    module test_module;
        import test_pkg::*;

        initial begin
            // package completion cursor
            int x = test_pkg::
        end
    endmodule
    )");

    // Test completions after test_pkg:: - automatically resolves all completions
    auto completionItems = doc.after("int x = test_pkg::").getResolvedCompletions(":");

    golden.record(completionItems);
}

TEST_CASE("MidIdentifierCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("mid_identifier_completion.sv", R"(
    `define MID_MACRO(arg) arg

    package completion_pkg;
        parameter int PKG_PARAM = 10;
        parameter int PKG_OTHER = 20;

        function int get_value();
            return PKG_PARAM;
        endfunction
    endpackage

    module source_module #(parameter int WIDTH = 1) (input logic clk);
    endmodule

    module mid_identifier_completion;
        typedef struct {
            logic valid;
            logic value;
        } data_t;

        data_t obj;
        int local_only;
        source_modULE existing_instance (.clk());
        source_modULE #(.WIDTH(2)) existing_parameterized_instance (.clk());

        initial begin
            int from_pkg = completion_pkg::PKG_PARAM;
            int from_call = completion_pkg::get_vaLUE();
            int from_member = obj.vaLID;
            int from_scope = local_onLY;
            $disPLAY("hello");
            `MID_MARCO(1);
        end
    endmodule
    )");
    doc.save();

    auto findByLabel = [](std::vector<CompletionHandle>& items, std::string_view label) {
        return std::find_if(items.begin(), items.end(), [&](const CompletionHandle& item) {
            return item.m_item.label == label;
        });
    };
    auto hasLabel = [&](std::vector<CompletionHandle>& items, std::string_view label) {
        return findByLabel(items, label) != items.end();
    };

    SECTION("scoped access") {
        auto cursor = doc.after("completion_pkg::PKG_");
        auto items = cursor.getCompletions();
        CHECK(hasLabel(items, "PKG_PARAM"));
        CHECK(hasLabel(items, "PKG_OTHER"));
        CHECK(!hasLabel(items, "local_only"));

        auto item = findByLabel(items, "PKG_PARAM");
        REQUIRE(item != items.end());
        item->insert();
        CHECK(doc.getText().find("completion_pkg::PKG_PARAM") != std::string::npos);
        CHECK(doc.getText().find("completion_pkg::PKG_PARAMPARAM") == std::string::npos);
    }

    SECTION("member access") {
        auto cursor = doc.after("obj.va");
        auto items = cursor.getCompletions();
        CHECK(hasLabel(items, "valid"));
        CHECK(hasLabel(items, "value"));
        CHECK(!hasLabel(items, "local_only"));

        auto item = findByLabel(items, "valid");
        REQUIRE(item != items.end());
        item->insert();
        CHECK(doc.getText().find("obj.valid") != std::string::npos);
        CHECK(doc.getText().find("obj.validLID") == std::string::npos);
    }

    SECTION("existing call suffix") {
        auto cursor = doc.after("completion_pkg::get_va");
        auto items = cursor.getCompletions();
        auto item = findByLabel(items, "get_value");
        REQUIRE(item != items.end());
        CHECK(!item->m_item.documentation);
        REQUIRE(item->m_item.insertText);
        CHECK(*item->m_item.insertText == "get_value()");
        CHECK(item->m_item.data);
        CHECK(getCompletionTextEdit(item->m_item).newText == "get_value");
        item->resolve();
        CHECK(item->m_item.documentation);
        REQUIRE(item->m_item.insertText);
        CHECK(*item->m_item.insertText == "get_value()");
        CHECK(!item->m_item.data);
        CHECK(getCompletionTextEdit(item->m_item).newText == "get_value");
        item->insert();
        CHECK(doc.getText().find("completion_pkg::get_value();") != std::string::npos);
        CHECK(doc.getText().find("get_value()();") == std::string::npos);
    }

    SECTION("resolve after document buffer changes") {
        auto cursor = doc.after("completion_pkg::get_va");
        auto items = cursor.getCompletions();
        auto item = findByLabel(items, "get_value");
        REQUIRE(item != items.end());

        doc.append("\n");
        doc.publishChanges();
        item->resolve();

        CHECK(item->m_item.documentation);
        CHECK(!item->m_item.data);
    }

    SECTION("lexical access") {
        auto cursor = doc.after("from_scope = local_on");
        auto items = cursor.getCompletions();
        auto item = findByLabel(items, "local_only");
        REQUIRE(item != items.end());
        item->insert();
        CHECK(doc.getText().find("from_scope = local_only;") != std::string::npos);
    }

    SECTION("existing module instantiation") {
        auto moduleBody = doc.getText().find("module mid_identifier_completion");
        REQUIRE(moduleBody != std::string::npos);
        auto cursor = doc.after("source_mod", moduleBody);
        auto items = cursor.getCompletions();
        std::vector<std::string> labels;
        std::ranges::transform(items, std::back_inserter(labels),
                               [](const CompletionHandle& entry) { return entry.m_item.label; });
        CAPTURE(labels);
        auto item = findByLabel(items, "source_module");
        REQUIRE(item != items.end());
        CHECK(item->m_item.insertTextFormat == lsp::InsertTextFormat::PlainText);
        CHECK(getCompletionTextEdit(item->m_item).newText == "source_module");
        item->insert();
        CHECK(doc.getText().find("source_module existing_instance") != std::string::npos);
        CHECK(doc.getText().find("source_moduleULE existing_instance") == std::string::npos);
    }

    SECTION("existing parameterized module instantiation") {
        auto parameterized = doc.getText().find("source_modULE #");
        REQUIRE(parameterized != std::string::npos);
        auto cursor = doc.after("source_mod", static_cast<lsp::uint>(parameterized));
        auto items = cursor.getCompletions();
        auto item = findByLabel(items, "source_module");
        REQUIRE(item != items.end());
        CHECK(item->m_item.insertTextFormat == lsp::InsertTextFormat::PlainText);
        item->resolve();
        CHECK(getCompletionTextEdit(item->m_item).newText == "source_module");
        item->insert();
        CHECK(doc.getText().find("source_module #(.WIDTH(2)) existing_parameterized_instance") !=
              std::string::npos);
        CHECK(doc.getText().find("existing_parameterized_instance") ==
              doc.getText().rfind("existing_parameterized_instance"));
    }

    SECTION("system subroutine") {
        auto cursor = doc.after("$dis");
        auto items = cursor.getCompletions();
        auto item = findByLabel(items, "$display");
        REQUIRE(item != items.end());
        CHECK(getCompletionTextEdit(item->m_item).newText == "\\$display");
        CHECK(!hasLabel(items, "local_only"));
    }

    SECTION("macro") {
        auto cursor = doc.after("`MID_MA");
        auto items = cursor.getCompletions();
        CHECK(std::ranges::all_of(items, [](const CompletionHandle& item) {
            return item.m_item.label.starts_with("`MID_MA");
        }));
        auto item = findByLabel(items, "`MID_MACRO");
        REQUIRE(item != items.end());
        CHECK(getCompletionTextEdit(item->m_item).newText == "`MID_MACRO");
        item->insert();
        CHECK(doc.getText().find("`MID_MACRO(1)") != std::string::npos);
        CHECK(doc.getText().find("`MID_MACRORCO(1)") == std::string::npos);
    }
}

TEST_CASE("DeferredMemberCompletionResolution") {
    ServerHarness server(
        makeCompletionResolveParams({"documentation", "detail", "additionalTextEdits"}));

    auto doc = server.openFile("deferred_member_completion.sv", R"(
    class completion_class #(parameter int WIDTH);
        function void run(input logic [WIDTH-1:0] arg);
        endfunction
    endclass

    module deferred_member_completion;
        completion_class #(8) wide;
        completion_class #(2) narrow;
        struct {
            logic field_a;
        } value;

        initial begin
            wide.;
            narrow.;
            value.;
        end
    endmodule
    )");

    auto findByLabel = [](std::vector<CompletionHandle>& items, std::string_view label) {
        return std::find_if(items.begin(), items.end(), [&](const CompletionHandle& item) {
            return item.m_item.label == label;
        });
    };

    auto wideItems = doc.after("wide.").getCompletions(".");
    auto wideRun = findByLabel(wideItems, "run");
    REQUIRE(wideRun != wideItems.end());
    REQUIRE(wideRun->m_item.insertText);
    CHECK(*wideRun->m_item.insertText == "run(${1:arg /* logic[7:0] */})");
    CHECK(getCompletionTextEdit(wideRun->m_item).newText == "run(${1:arg /* logic[7:0] */})");
    wideRun->resolve();

    auto narrowItems = doc.after("narrow.").getCompletions(".");
    auto narrowRun = findByLabel(narrowItems, "run");
    REQUIRE(narrowRun != narrowItems.end());
    REQUIRE(narrowRun->m_item.insertText);
    CHECK(*narrowRun->m_item.insertText == "run(${1:arg /* logic[1:0] */})");
    narrowRun->resolve();
    REQUIRE(narrowRun->m_item.insertText);
    CHECK(*narrowRun->m_item.insertText == "run(${1:arg /* logic[1:0] */})");
    CHECK(getCompletionTextEdit(narrowRun->m_item).newText == "run(${1:arg /* logic[1:0] */})");
    CHECK(narrowRun->m_item.documentation);

    auto fieldItems = doc.after("value.").getCompletions(".");
    auto field = findByLabel(fieldItems, "field_a");
    REQUIRE(field != fieldItems.end());
    CHECK(!field->m_item.documentation);
    field->resolve();
    REQUIRE(field->m_item.documentation);
    auto documentation = rfl::get<lsp::MarkupContent>(*field->m_item.documentation);
    CHECK(documentation.value.find("logic field_a") != std::string::npos);
}

TEST_CASE("AdvertisedMemberEditResolution") {
    ServerHarness server(makeCompletionResolveParams(
        {"documentation", "insertText", "insertTextFormat", "textEdit"}));

    auto doc = server.openFile("advertised_member_edit_resolution.sv", R"(
    module advertised_member_edit_resolution;
        function void run(input logic [1:0] arg);
        endfunction

        initial begin
            run;
        end
    endmodule
    )");

    auto items = doc.before("run;").getCompletions();
    auto run = std::ranges::find(items, "run",
                                 [](const CompletionHandle& item) { return item.m_item.label; });
    REQUIRE(run != items.end());
    CHECK(!run->m_item.insertText);
    CHECK(!run->m_item.insertTextFormat);
    CHECK(getCompletionTextEdit(run->m_item).newText == "run");

    run->resolve();
    REQUIRE(run->m_item.insertText);
    CHECK(*run->m_item.insertText == "run(${1:arg /* logic[1:0] */})");
    CHECK(run->m_item.insertTextFormat == lsp::InsertTextFormat::Snippet);
    CHECK(getCompletionTextEdit(run->m_item).newText == "run(${1:arg /* logic[1:0] */})");
}

TEST_CASE("MultidimensionalInstanceArrayCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("multidimensional_instance_array_completion.sv", R"(
    module completion_leaf;
    endmodule

    module completion_top;
        completion_leaf instances[1:0][2:0] ();
        int array_data[4];

        initial begin
            int result = array_data[0];
        end
    endmodule
    )");

    auto items = doc.after("int result = array_data[").getCompletions("[");
    auto instance = std::ranges::find(items, "instances", [](const CompletionHandle& item) {
        return item.m_item.label;
    });

    REQUIRE(instance != items.end());
    REQUIRE(instance->m_item.labelDetails);
    CHECK(instance->m_item.labelDetails->detail == " completion_leaf[2][3]");
}

TEST_CASE("WildcardImportCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("wildcard_test.sv", R"(
    package math_pkg;
        parameter int PI_VALUE = 314;
        parameter int E_VALUE = 271;

        typedef struct {
            real x;
            real y;
        } point_t;

        typedef enum {
            ADD,
            SUBTRACT,
            MULTIPLY
        } operation_t;

        function real calculate(real a, real b, operation_t op);
            case (op)
                ADD: return a + b;
                SUBTRACT: return a - b;
                MULTIPLY: return a * b;
                default: return 0.0;
            endcase
        endfunction

        task print_result(real value);
            $display("Result: %f", value);
        endtask
    endpackage

    package utils_pkg;
        parameter int MAX_SIZE = 1024;

        typedef logic [7:0] byte_t;

        function int find_max(int array[], int size);
            int max_val = array[0];
            for (int i = 1; i < size; i++) begin
                if (array[i] > max_val)
                    max_val = array[i];
            end
            return max_val;
        endfunction
    endpackage

    module test_wildcard_imports;
        import math_pkg::*;
        import utils_pkg::*;

        initial begin
            point_t my_point;
            operation_t op = ADD;
            byte_t data = 8'hFF;

            // Test completions with wildcard imports
            real result = calculate(PI_VALUE, E_VALUE, op);
            print_result(x, );
            int max_val = find_max();

            // Test block member completions with wildcard imports
        end
    endmodule
    )");

    // Test completions after wildcard imports
    auto afterPrintResult = doc.after("print_result(x, ").getResolvedCompletions();
    golden.record("after_print_result", afterPrintResult);

    auto lhsCompletion = doc.before("// Test block member completions").getResolvedCompletions();
    golden.record("block_completions", lhsCompletion);
}

TEST_CASE("ModuleMemberCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("module_test.sv", R"(
    interface bus_if;
        logic valid;
        modport master(output valid);
    endinterface

    module test_module (
        input  logic        clk,
        input  logic        rst,
        output logic [7:0]  data_out,
        bus_if.master       bus_port
    );
        // Local variables of different types
        logic internal_signal;
        logic [15:0] wide_signal;

        // Parameters
        parameter int PARAM_INT = 42;
        parameter logic [7:0] PARAM_LOGIC = 8'hAA;

        // Type definitions
        typedef struct {
            logic [7:0] addr;
            logic [31:0] data;
        } bus_transaction_t;

        typedef enum logic [1:0] {
            IDLE = 2'b00,
            ACTIVE = 2'b01,
            WAIT = 2'b10
        } state_t;

        // Local functions
        function logic [7:0] calc_parity(input logic [7:0] data);
            return ^data;
        endfunction

        // Task
        task reset_signals();
            internal_signal <= 1'b0;
            wide_signal <= 16'h0;
        endtask

        // Instance of another module
        sub_module u_sub (
            .clk(clk),
            .rst(rst),
            .enable(internal_signal)
        );

        // Generate blocks
        generate
            genvar i;
            for (i = 0; i < 4; i++) begin : gen_array
                logic [7:0] gen_signal;
            end
        endgenerate

        // Interface port example
        simple_interface intf();

        initial begin
            // Test member completions in module scope
            internal_signal =
            wide_signal =
        end
    endmodule

    // Sub-module for instantiation
    module sub_module (
        input logic clk,
        input logic rst,
        input logic enable
    );
    endmodule

    // Simple interface for interface port testing
    interface simple_interface;
        logic valid;
        logic ready;
    endinterface
    )");

    // Test completions for module members - automatically resolves all completions
    auto lhs = doc.before("// Instance of another module").getResolvedCompletions();
    auto rhs = doc.after("wide_signal =").getResolvedCompletions();

    golden.record("lhs", lhs);
    golden.record("rhs", rhs);

    // Test other RHS locations - they should all return the same completions
    auto rhsClk = doc.after(".clk(").getResolvedCompletions();
    auto rhsRst = doc.after(".rst(").getResolvedCompletions();
    auto rhsEnable = doc.after(".enable(").getResolvedCompletions();

    // All RHS completions should be identical
    CHECK(rhs.size() == rhsClk.size());
    CHECK(rhs.size() == rhsRst.size());
    CHECK(rhs.size() == rhsEnable.size());
}

TEST_CASE("HierarchicalInstanceCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("hierarchical_test.sv", R"(
    module sub_module (
        input logic clk,
        input logic rst,
        output logic [7:0] data_out,
        output logic valid
    );
        logic internal_state;

        always_ff @(posedge clk) begin
            if (rst) begin
                data_out <= 8'h0;
                valid <= 1'b0;
                internal_state <= 1'b0;
            end else begin
                data_out <= data_out + 1;
                valid <= ~valid;
                internal_state <= ~internal_state;
            end
        end
    endmodule

    module parent_module;
        logic clk, rst;
        logic [7:0] data;
        logic valid;

        sub_module inst (
            .clk(clk),
            .rst(rst),
            .data_out(data),
            .valid(valid)
        );

        initial begin
            // Test hierarchical instance completions
            inst.
        end
    endmodule
    )");

    // Test completions after "inst."
    auto instCompletions = doc.after("inst.").getResolvedCompletions(".");
    golden.record("instance_completions", instCompletions);
}

TEST_CASE("HierarchicalInterfacePortCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("iface_port_completion.sv", R"(
    interface test_if;
        logic valid;
        logic ready;
        logic hidden;

        modport producer(output valid, input ready);
    endinterface

    module iface_port_completion (
        test_if raw_if,
        test_if.producer producer_if
    );
        initial begin
            raw_if.;
            producer_if.;
        end
    endmodule
    )");

    auto hasCompletion = [](const std::vector<CompletionHandle>& items, std::string_view label) {
        return std::any_of(items.begin(), items.end(), [&](const CompletionHandle& item) {
            return item.m_item.label == label;
        });
    };

    auto rawCompletions = doc.after("raw_if.").getCompletions(".");
    CHECK(hasCompletion(rawCompletions, "valid"));
    CHECK(hasCompletion(rawCompletions, "ready"));
    CHECK(hasCompletion(rawCompletions, "hidden"));
    CHECK(hasCompletion(rawCompletions, "producer"));

    auto producerCompletions = doc.after("producer_if.").getCompletions(".");
    CHECK(hasCompletion(producerCompletions, "valid"));
    CHECK(hasCompletion(producerCompletions, "ready"));
    CHECK(!hasCompletion(producerCompletions, "hidden"));
}

TEST_CASE("HierarchicalInterfaceInstanceCompletionUsesResolvedParameterType") {
    ServerHarness server("repo1");

    auto doc = server.openFile("parameterized_iface_completion.sv", R"(
    typedef logic [3:0] nibble_t;

    interface stream_if #(
        parameter type element_t = logic
    );
        element_t data;
    endinterface

    module parameterized_iface_completion;
        stream_if #(.element_t(logic [7:0])) stream();
        stream_if #(.element_t(nibble_t)) narrow_stream();

        initial begin
            stream.data;
            narrow_stream.data;
        end
    endmodule
    )");

    auto completions = doc.after("stream.").getCompletions(".");
    auto data = std::ranges::find(completions, "data",
                                  [](const CompletionHandle& item) { return item.m_item.label; });
    REQUIRE(data != completions.end());
    REQUIRE(data->m_item.labelDetails);
    CHECK(data->m_item.labelDetails->detail == " logic[7:0]");
    auto typeParameter = std::ranges::find(
        completions, "element_t", [](const CompletionHandle& item) { return item.m_item.label; });
    REQUIRE(typeParameter != completions.end());
    REQUIRE(typeParameter->m_item.labelDetails);
    CHECK(typeParameter->m_item.labelDetails->detail == " type");

    auto narrowCompletions = doc.after("narrow_stream.").getCompletions(".");
    auto narrowData = std::ranges::find(
        narrowCompletions, "data", [](const CompletionHandle& item) { return item.m_item.label; });
    REQUIRE(narrowData != narrowCompletions.end());
    REQUIRE(narrowData->m_item.labelDetails);
    CHECK(narrowData->m_item.labelDetails->detail == " nibble_t");
}

TEST_CASE("HierarchicalInterfacePortCompletionUsesPinnedParameterType") {
    ServerHarness server("repo1");

    auto doc = server.openFile("pinned_parameter_iface_completion.sv", R"(
    package types_pkg;
        typedef logic [15:0] event_t;
    endpackage

    interface event_if #(
        parameter type data_type = logic
    );
        data_type data;
        modport source(output data);
    endinterface

    module pinned_parameter_iface_completion (
        event_if.source channel
    );
        $static_assert(type(channel.data_type) == type(types_pkg::event_t));

        initial begin
            channel.; // completion target
        end
    endmodule
    )");

    auto completions = doc.before("; // completion target").getCompletions(".");
    auto data = std::ranges::find(completions, "data",
                                  [](const CompletionHandle& item) { return item.m_item.label; });
    REQUIRE(data != completions.end());
    REQUIRE(data->m_item.labelDetails);
    CHECK(data->m_item.labelDetails->detail == " event_t");
}

TEST_CASE("HierarchicalInterfacePortArrayCompletionWithUnresolvedBounds") {
    ServerHarness server("repo1");

    auto doc = server.openFile("invalid_iface_port_completion.sv", R"(
    interface valid_data_if #(
        parameter type data_type,
        parameter int data_width = 8
    );
        data_type data;
        logic [data_width - 1:0] payload;
        logic valid;

        modport source(output data, payload, valid);
    endinterface

    module iface_port_completion #(
        parameter int num_ports
    ) (
        valid_data_if.source requests[num_ports],
        valid_data_if.source matrix[num_ports][num_ports]
    );
        for (genvar index = 0; index < num_ports; index++) begin
            always_comb begin
                requests[index].valid;
                requests[index].payload;
                requests[index][index].valid;
                requests[index:index].valid;
                matrix[index].valid;
                matrix[index][index].valid;
            end
        end
    endmodule
    )");

    auto findCompletion = [](auto& items, std::string_view label) {
        return std::find_if(items.begin(), items.end(), [label](const CompletionHandle& item) {
            return item.m_item.label == label;
        });
    };

    auto members = doc.after("requests[index].").getCompletions(".");
    CHECK(findCompletion(members, "data") != members.end());
    CHECK(findCompletion(members, "payload") != members.end());
    CHECK(findCompletion(members, "valid") != members.end());

    auto matrixMembers = doc.after("matrix[index][index].").getCompletions(".");
    CHECK(findCompletion(matrixMembers, "valid") != matrixMembers.end());

    for (auto invalidAccess :
         {"requests[index][index].", "requests[index:index].", "matrix[index]."}) {
        auto invalidMembers = doc.after(invalidAccess).getCompletions(".");
        CHECK(findCompletion(invalidMembers, "valid") == invalidMembers.end());
    }

    auto lexical = doc.before("requests[index].valid").getCompletions();
    auto requests = findCompletion(lexical, "requests");
    REQUIRE(requests != lexical.end());
    REQUIRE(requests->m_item.labelDetails);
    CHECK(requests->m_item.labelDetails->detail == " valid_data_if.source[num_ports]");

    auto matrix = findCompletion(lexical, "matrix");
    REQUIRE(matrix != lexical.end());
    REQUIRE(matrix->m_item.labelDetails);
    CHECK(matrix->m_item.labelDetails->detail == " valid_data_if.source[num_ports][num_ports]");

    auto prefixed = doc.after("requests[index].valid").getCompletions();
    auto valid = findCompletion(prefixed, "valid");
    REQUIRE(valid != prefixed.end());
    valid->resolve();
    CHECK(valid->m_item.documentation);

    auto validUse = doc.after("requests[index].");
    auto hover = doc.getHoverAt(validUse.m_offset);
    REQUIRE(hover);
    auto hoverContent = rfl::get<lsp::MarkupContent>(hover->contents);
    CHECK(hoverContent.value.find("output data, payload, valid") != std::string::npos);

    auto payloadUse = doc.before("requests[index].payload");
    auto payloadHover = doc.getHoverAt(payloadUse.m_offset +
                                       std::string_view("requests[index].").size());
    REQUIRE(payloadHover);
    auto payloadContent = rfl::get<lsp::MarkupContent>(payloadHover->contents);
    CHECK(payloadContent.value.find("Type: `logic[7:0]`") != std::string::npos);

    auto definitions = validUse.getDefinitions();
    REQUIRE(definitions.size() == 2);
    CHECK(definitions[0].targetSelectionRange.start.line == 9);
    CHECK(definitions[1].targetSelectionRange.start.line == 7);
}

TEST_CASE("HierarchicalStructCompletionWithUnresolvedWidth") {
    ServerHarness server("comp_repo");
    auto doc = server.openFile("unused_mod.sv");

    auto findCompletion = [](auto& items, std::string_view label) {
        return std::ranges::find(items, label,
                                 [](const CompletionHandle& item) { return item.m_item.label; });
    };

    auto rootMembers = doc.after("partial_value.").getCompletions(".");
    CHECK(findCompletion(rootMembers, "middle") != rootMembers.end());
    CHECK(findCompletion(rootMembers, "middle.leaf") != rootMembers.end());
    CHECK(findCompletion(rootMembers, "middle.leaf.known") != rootMembers.end());
    CHECK(findCompletion(rootMembers, "middle.leaf.variable_width") != rootMembers.end());
    CHECK(findCompletion(rootMembers, "middle.middle_known") != rootMembers.end());
    CHECK(findCompletion(rootMembers, "root_known") != rootMembers.end());

    auto middleMembers = doc.after("partial_value.middle.").getCompletions(".");
    CHECK(findCompletion(middleMembers, "leaf") != middleMembers.end());
    CHECK(findCompletion(middleMembers, "middle_known") != middleMembers.end());

    auto leafMembers = doc.after("partial_value.middle.leaf.").getCompletions(".");
    CHECK(findCompletion(leafMembers, "variable_width") != leafMembers.end());
    auto knownCompletion = findCompletion(leafMembers, "known");
    REQUIRE(knownCompletion != leafMembers.end());
    knownCompletion->resolve();
    CHECK(knownCompletion->m_item.documentation);

    auto middleUse = doc.before("middle.leaf.known");
    auto middleHover = doc.getHoverAt(middleUse.m_offset);
    REQUIRE(middleHover);
    auto middleContent = rfl::get<lsp::MarkupContent>(middleHover->contents).value;
    CHECK(middleContent.find("**Field** `middle`") != std::string::npos);
    CHECK(middleContent.find("Declared Type:") != std::string::npos);
    CHECK(middleContent.find("middle_t") != std::string::npos);
    CHECK(middleContent.find("Incomplete subtypes:") == std::string::npos);
    CHECK(middleContent.find("Incomplete type") == std::string::npos);

    auto rootTypeUse = doc.before("root_t partial_value");
    auto rootTypeHover = doc.getHoverAt(rootTypeUse.m_offset);
    REQUIRE(rootTypeHover);
    auto rootTypeContent = rfl::get<lsp::MarkupContent>(rootTypeHover->contents).value;
    CHECK(rootTypeContent.find("**TypeAlias** `root_t`") != std::string::npos);
    CHECK(rootTypeContent.find("Declared Type: [`PackedStruct root_t`]") != std::string::npos);
    CHECK(rootTypeContent.find("Incomplete subtypes: [`variable_width_t`](<file:") !=
          std::string::npos);
    CHECK(rootTypeContent.find("unused_mod.sv#L72,45") != std::string::npos);
    auto subtypeLineStart = rootTypeContent.find("Incomplete subtypes:");
    REQUIRE(subtypeLineStart != std::string::npos);
    auto subtypeLineEnd = rootTypeContent.find('\n', subtypeLineStart);
    auto subtypeLine = rootTypeContent.substr(subtypeLineStart, subtypeLineEnd - subtypeLineStart);
    auto firstSubtype = subtypeLine.find("variable_width_t");
    REQUIRE(firstSubtype != std::string::npos);
    CHECK(subtypeLine.find("variable_width_t", firstSubtype + 1) == std::string::npos);
    CHECK(rootTypeContent.find("middle.leaf.variable_width") == std::string::npos);

    auto knownUse = doc.before("leaf.known").after("leaf.");
    auto knownHover = doc.getHoverAt(knownUse.m_offset);
    REQUIRE(knownHover);
    auto knownContent = rfl::get<lsp::MarkupContent>(knownHover->contents).value;
    CHECK(knownContent.find("**Field** `known`") != std::string::npos);
    CHECK(knownContent.find("Type: `logic`") != std::string::npos);
}

TEST_CASE("AssignmentPatternStructFieldsWithUnresolvedWidth") {
    ServerHarness server("repo1");
    auto doc = server.openFile("assignment_pattern_fields.sv", R"(
    typedef logic [missing_width - 1:0] incomplete_t;

    typedef struct packed {
        incomplete_t payload;
        logic valid;
    } packet_t;

    typedef struct packed {
        packet_t packet;
        logic ready;
    } wrapper_t;

    module assignment_patterns;
        packet_t packet;
        wrapper_t wrapper;
        wire packet_t packet_wire;
        logic source;
        logic payload;
        logic valid;

        function automatic void consume(packet_t value);
        endfunction

        assign packet_wire = '{
        };

        initial begin
            packet = '{payload: source, valid: source};
            packet = '{payload, valid};
            packet = {source, source};
            packet = '{ };
            packet = '{pay: source, valid: source};
            packet = '{payload: source, };
            packet = '{default: source, };
            wrapper = '{packet: '{payload: source, valid: source}, ready: source};
            consume('{});
        end
    endmodule
    )");

    auto findCompletion = [](auto& items, std::string_view label) {
        return std::ranges::find(items, label,
                                 [](const CompletionHandle& item) { return item.m_item.label; });
    };
    constexpr std::string_view allFieldsLabel = "'{payload, valid}";

    auto& triggerCharacters = completions::completionTriggerCharacters();
    CHECK(std::ranges::find(triggerCharacters, "{") != triggerCharacters.end());

    auto emptyPattern = doc.before("packet = '{ }").after("'{").getCompletions("{");
    REQUIRE(emptyPattern.size() == 1);
    auto triggeredAllFields = findCompletion(emptyPattern, allFieldsLabel);
    REQUIRE(triggeredAllFields != emptyPattern.end());
    CHECK(triggeredAllFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n");
    CHECK(!triggeredAllFields->m_item.additionalTextEdits);

    auto ordinaryBrace = doc.after("packet = {").getCompletions("{");
    CHECK(ordinaryBrace.empty());

    auto invokedPattern = doc.after("assign packet_wire = '{\n        ").getCompletions();
    REQUIRE(invokedPattern.size() == 3);
    CHECK(findCompletion(invokedPattern, allFieldsLabel) == invokedPattern.end());
    auto invokedPayload = findCompletion(invokedPattern, "payload");
    REQUIRE(invokedPayload != invokedPattern.end());
    CHECK(invokedPayload->m_item.insertText == "payload: $1");
    CHECK(findCompletion(invokedPattern, "valid") != invokedPattern.end());
    CHECK(findCompletion(invokedPattern, "default") != invokedPattern.end());

    auto unfinished = server.openFile("unfinished_assignment_pattern.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    module unfinished_assignment_pattern;
        wire packet_t packet_wire;
        assign packet_wire = '{)");
    auto unfinishedCompletions = unfinished.end().getCompletions("{");
    REQUIRE(unfinishedCompletions.size() == 1);
    auto allFields = findCompletion(unfinishedCompletions, allFieldsLabel);
    REQUIRE(allFields != unfinishedCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n\\};");
    CHECK(!allFields->m_item.additionalTextEdits);

    auto initializer = server.openFile("unfinished_assignment_pattern_initializer.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    module unfinished_assignment_pattern_initializer;
        packet_t initialized = '{)");
    auto initializerCompletions = initializer.end().getCompletions("{");
    REQUIRE(initializerCompletions.size() == 1);
    allFields = findCompletion(initializerCompletions, allFieldsLabel);
    REQUIRE(allFields != initializerCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n\\};");

    auto paired = server.openFile("paired_assignment_pattern.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    module paired_assignment_pattern;
        wire packet_t packet_wire;
        assign packet_wire = '{}
    endmodule
    )");
    auto pairedCompletions = paired.after("assign packet_wire = '{").getCompletions("{");
    REQUIRE(pairedCompletions.size() == 1);
    allFields = findCompletion(pairedCompletions, allFieldsLabel);
    REQUIRE(allFields != pairedCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n");
    REQUIRE(allFields->m_item.additionalTextEdits);
    REQUIRE(allFields->m_item.additionalTextEdits->size() == 1);
    CHECK(allFields->m_item.additionalTextEdits->front().newText == ";");

    auto alwaysComb = server.openFile("always_comb_assignment_pattern.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    module always_comb_assignment_pattern;
        packet_t packet;
        always_comb packet = '{}
    endmodule
    )");
    auto alwaysCombCompletions = alwaysComb.after("always_comb packet = '{").getCompletions("{");
    REQUIRE(alwaysCombCompletions.size() == 1);
    allFields = findCompletion(alwaysCombCompletions, allFieldsLabel);
    REQUIRE(allFields != alwaysCombCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n");
    CHECK(allFields->m_item.insertTextMode == lsp::InsertTextMode::adjustIndentation);
    REQUIRE(allFields->m_item.additionalTextEdits);

    auto nestedPatternDoc = server.openFile("nested_assignment_pattern.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    typedef struct packed {
        packet_t packet;
        logic ready;
    } wrapper_t;
    module nested_unfinished_assignment_pattern;
        wrapper_t wrapper;
        initial begin
            wrapper = '{ready: 1'b0, packet: '{}};
        end
    endmodule
    )");
    auto nestedPatternCompletions = nestedPatternDoc.after("packet: '{").getCompletions("{");
    REQUIRE(nestedPatternCompletions.size() == 1);
    allFields = findCompletion(nestedPatternCompletions, allFieldsLabel);
    REQUIRE(allFields != nestedPatternCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n");
    CHECK(!allFields->m_item.additionalTextEdits);

    auto nestedUnfinished = server.openFile("nested_unfinished_assignment_pattern.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    typedef struct packed {
        packet_t packet;
        logic ready;
    } wrapper_t;
    module nested_unfinished_assignment_pattern;
        wrapper_t wrapper;
        initial begin
            wrapper = '{packet: '{)");
    auto nestedUnfinishedCompletions = nestedUnfinished.end().getCompletions("{");
    REQUIRE(nestedUnfinishedCompletions.size() == 1);
    allFields = findCompletion(nestedUnfinishedCompletions, allFieldsLabel);
    REQUIRE(allFields != nestedUnfinishedCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n\\},");
    CHECK(!allFields->m_item.additionalTextEdits);

    auto nestedPairedUnfinished = server.openFile("nested_paired_unfinished_assignment_pattern.sv",
                                                  R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    typedef struct packed {
        packet_t packet;
        logic ready;
    } wrapper_t;
    module nested_paired_unfinished_assignment_pattern;
        wrapper_t wrapper;
        initial begin
            wrapper = '{packet: '{})");
    auto nestedPairedUnfinishedCompletions =
        nestedPairedUnfinished.after("wrapper = '{packet: '{").getCompletions("{");
    REQUIRE(nestedPairedUnfinishedCompletions.size() == 1);
    allFields = findCompletion(nestedPairedUnfinishedCompletions, allFieldsLabel);
    REQUIRE(allFields != nestedPairedUnfinishedCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n");
    REQUIRE(allFields->m_item.additionalTextEdits);
    REQUIRE(allFields->m_item.additionalTextEdits->size() == 1);
    CHECK(allFields->m_item.additionalTextEdits->front().newText == ",");

    auto nestedWithComma = server.openFile("nested_assignment_pattern_with_comma.sv", R"(
    typedef struct packed {
        logic [7:0] payload;
        logic valid;
    } packet_t;
    typedef struct packed {
        packet_t packet;
        logic ready;
    } wrapper_t;
    module nested_assignment_pattern_with_comma;
        wrapper_t wrapper;
        initial begin
            wrapper = '{packet: '{}, ready: 1'b0};
        end
    endmodule
    )");
    auto nestedWithCommaCompletions =
        nestedWithComma.after("wrapper = '{packet: '{").getCompletions("{");
    REQUIRE(nestedWithCommaCompletions.size() == 1);
    allFields = findCompletion(nestedWithCommaCompletions, allFieldsLabel);
    REQUIRE(allFields != nestedWithCommaCompletions.end());
    CHECK(!allFields->m_item.additionalTextEdits);

    auto callPatternCompletions = doc.after("consume('{").getCompletions("{");
    REQUIRE(callPatternCompletions.size() == 1);
    allFields = findCompletion(callPatternCompletions, allFieldsLabel);
    REQUIRE(allFields != callPatternCompletions.end());
    CHECK(allFields->m_item.insertText == "\n\tpayload: $1,\n\tvalid: $2\n");
    CHECK(!allFields->m_item.additionalTextEdits);

    auto partialPattern = doc.after("packet = '{ }").after("packet = '{pay").getCompletions();
    auto payload = findCompletion(partialPattern, "payload");
    REQUIRE(payload != partialPattern.end());
    CHECK(findCompletion(partialPattern, "valid") == partialPattern.end());
    auto defaultKey = findCompletion(partialPattern, "default");
    REQUIRE(defaultKey != partialPattern.end());
    CHECK(defaultKey->m_item.kind == lsp::CompletionItemKind::Keyword);
    CHECK(!defaultKey->m_item.insertText);
    CHECK(!payload->m_item.insertText);
    CHECK(getCompletionTextEdit(payload->m_item).newText == "payload");

    auto nextKey = doc.after("packet = '{pay: source, valid: source};")
                       .after("packet = '{payload: source, ")
                       .getCompletions();
    REQUIRE(nextKey.size() == 2);
    auto valid = findCompletion(nextKey, "valid");
    REQUIRE(valid != nextKey.end());
    CHECK(findCompletion(nextKey, "payload") == nextKey.end());
    defaultKey = findCompletion(nextKey, "default");
    REQUIRE(defaultKey != nextKey.end());
    CHECK(defaultKey->m_item.insertText == "default: $1");
    CHECK(valid->m_item.insertText == "valid: $1");

    auto afterDefault = doc.after("packet = '{default: source, ").getCompletions();
    CHECK(findCompletion(afterDefault, "default") == afterDefault.end());
    CHECK(findCompletion(afterDefault, "payload") != afterDefault.end());
    CHECK(findCompletion(afterDefault, "valid") != afterDefault.end());

    auto nestedPattern = doc.after("wrapper = '{packet: '{").getCompletions();
    CHECK(findCompletion(nestedPattern, "payload") != nestedPattern.end());
    CHECK(findCompletion(nestedPattern, "valid") == nestedPattern.end());
    CHECK(findCompletion(nestedPattern, "ready") == nestedPattern.end());

    auto positionalPayload = doc.before("payload, valid};");
    auto positionalHover = doc.getHoverAt(positionalPayload.m_offset);
    REQUIRE(positionalHover);
    auto positionalContent = rfl::get<lsp::MarkupContent>(positionalHover->contents).value;
    CHECK(positionalContent.find("**Variable** `payload`") != std::string::npos);

    auto positionalDefinitions = positionalPayload.getDefinitions();
    REQUIRE(positionalDefinitions.size() == 1);
    auto localPayload = doc.after("module assignment_patterns;").before("payload;");
    CHECK(positionalDefinitions[0].targetSelectionRange.start == localPayload.getPosition());

    auto payloadKey = doc.before("payload: source");
    auto hover = doc.getHoverAt(payloadKey.m_offset);
    REQUIRE(hover);
    auto hoverContent = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(hoverContent.find("**Field** `payload`") != std::string::npos);
    CHECK(hoverContent.find("incomplete_t") != std::string::npos);

    auto definitions = payloadKey.getDefinitions();
    REQUIRE(definitions.size() == 1);
    auto declaration = doc.before("payload;").getPosition();
    CHECK(definitions[0].targetSelectionRange.start.line == declaration.line);
    CHECK(definitions[0].targetSelectionRange.start.character == declaration.character);
}

TEST_CASE("StructAssignmentCompletionForUnpackedArrayElement") {
    ServerHarness server("repo1");
    auto doc = server.openFile("unpacked_array_struct_assignment.sv", R"(
    typedef struct packed {
        logic value;
        logic valid;
    } entry_t;

    module unpacked_array_struct_assignment;
        entry_t entries [2];
        initial entries = '{0: '{}};
    endmodule
    )");

    auto completions = doc.after("entries = '{0: '{").getCompletions("{");
    REQUIRE(completions.size() == 1);
    CHECK(completions.front().m_item.label == "'{value, valid}");
    CHECK(completions.front().m_item.insertText == "\n\tvalue: $1,\n\tvalid: $2\n");
}

TEST_CASE("IncompleteTypeCompletionAndHoverUseDeclaredSyntax") {
    ServerHarness server;
    auto doc = server.openFile("incomplete_type.sv", R"(
    module top;
        typedef struct packed {
            logic [UNKNOWN_FIELD_WIDTH-1:0] field [UNKNOWN_FIELD_DEPTH-1:0];
        } incomplete_t;

        incomplete_t value;
        logic [UNKNOWN_VALUE_WIDTH-1:0] direct [UNKNOWN_VALUE_DEPTH-1:0];

        function void consume(
            input logic [UNKNOWN_ARG_WIDTH-1:0] arg [UNKNOWN_ARG_DEPTH-1:0]
        );
        endfunction

        initial begin
            value.;
            value.field; // field use
            consume;
            direct; // direct use
        end
    endmodule
    )");

    auto findByLabel = [](auto& items, std::string_view label) {
        return std::ranges::find(items, label,
                                 [](const CompletionHandle& item) { return item.m_item.label; });
    };

    auto fieldItems = doc.after("value.").getCompletions(".");
    auto field = findByLabel(fieldItems, "field");
    REQUIRE(field != fieldItems.end());
    REQUIRE(field->m_item.labelDetails);
    CHECK(field->m_item.labelDetails->detail ==
          " logic [UNKNOWN_FIELD_WIDTH-1:0] [UNKNOWN_FIELD_DEPTH-1:0]");

    auto callableItems = doc.before("consume;").getCompletions();
    auto callable = findByLabel(callableItems, "consume");
    REQUIRE(callable != callableItems.end());
    REQUIRE(callable->m_item.insertText);
    CHECK(*callable->m_item.insertText ==
          "consume(${1:arg /* logic [UNKNOWN_ARG_WIDTH-1:0] [UNKNOWN_ARG_DEPTH-1:0] */})");

    auto fieldHover = doc.getHoverAt(doc.before("field; // field use").m_offset);
    REQUIRE(fieldHover);
    auto fieldContent = rfl::get<lsp::MarkupContent>(fieldHover->contents).value;
    CHECK(fieldContent.find("Declared Type: `logic [UNKNOWN_FIELD_WIDTH-1:0] "
                            "[UNKNOWN_FIELD_DEPTH-1:0]`") != std::string::npos);
    CHECK(fieldContent.find("Incomplete type") == std::string::npos);

    auto directHover = doc.getHoverAt(doc.before("direct; // direct use").m_offset);
    REQUIRE(directHover);
    auto directContent = rfl::get<lsp::MarkupContent>(directHover->contents).value;
    CHECK(directContent.find("Declared Type: `logic [UNKNOWN_VALUE_WIDTH-1:0] "
                             "[UNKNOWN_VALUE_DEPTH-1:0]`") != std::string::npos);
    CHECK(directContent.find("Incomplete type") == std::string::npos);
}

TEST_CASE("HierarchicalStructCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("struct_hierarchical_test.sv", R"(
    typedef struct {
        logic [7:0] addr;
        logic [31:0] data;
        logic valid;
    } simple_struct_t;

    typedef struct {
        simple_struct_t inner;
        logic [15:0] tag;
        logic ready;
    } nested_struct_t;

    typedef struct {
        nested_struct_t level1;
        logic [3:0] id;
        logic enable;
    } deep_nested_struct_t;

    module struct_test_module;
        simple_struct_t my_struct;
        nested_struct_t complex_struct;
        deep_nested_struct_t very_complex_struct;

        initial begin
            my_struct.;

            complex_struct.;

            very_complex_struct.;

            complex_struct.inner.;

            very_complex_struct.level1.;

            very_complex_struct.level1.inner.;
        end
    endmodule
    )");

    auto testCompletion = [&](std::string s) {
        auto completions = doc.after(s).getResolvedCompletions(".");
        CHECK(!completions.empty());
        golden.record(s, completions);
    };

    testCompletion("my_struct.");
    testCompletion("complex_struct.");
    testCompletion("very_complex_struct.");
    testCompletion("complex_struct.inner.");
    testCompletion("very_complex_struct.level1.");
    testCompletion("very_complex_struct.level1.inner.");
}

TEST_CASE("HierarchicalStructCompletionDoesNotFlattenUnions") {
    ServerHarness server("repo1");

    auto doc = server.openFile("struct_union_completion.sv", R"(
    typedef struct {
        logic leaf;
    } inner_t;

    typedef union {
        inner_t inner;
        logic raw;
    } choice_t;

    typedef struct {
        inner_t nested;
        choice_t choice;
    } outer_t;

    module struct_union_completion;
        outer_t value;
        choice_t choice;

        initial begin
            value.;
            choice.;
        end
    endmodule
    )");

    auto hasLabel = [](const auto& items, std::string_view label) {
        return std::ranges::any_of(items, [&](const CompletionHandle& item) {
            return item.m_item.label == label;
        });
    };

    auto structCompletions = doc.after("value.").getCompletions(".");
    CHECK(hasLabel(structCompletions, "nested.leaf"));
    CHECK_FALSE(hasLabel(structCompletions, "choice.inner"));

    auto unionCompletions = doc.after("choice.").getCompletions(".");
    CHECK(hasLabel(unionCompletions, "inner"));
    CHECK_FALSE(hasLabel(unionCompletions, "inner.leaf"));
}

TEST_CASE("ArrayOfStructsCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("array_struct_test.sv", R"(
    typedef struct {
        logic [7:0] addr;
        logic [31:0] data;
        logic valid;
    } transaction_t;

    typedef struct {
        transaction_t txn;
        logic [15:0] id;
    } nested_transaction_t;

    module array_struct_module;
        transaction_t transactions[4];
        transaction_t transactions_2d[2][3];
        nested_transaction_t nested_arr[8];

        initial begin
            // Test completion on array element
            transactions[0].;

            // Test completion on 2D array element
            transactions_2d[0][1].;

            // Test completion on nested struct in array
            nested_arr[3].;

            // Test nested field access in array element
            nested_arr[5].txn.;
        end
    endmodule
    )");

    auto testCompletion = [&](std::string s) {
        auto completions = doc.after(s).getResolvedCompletions(".");
        golden.record(s, completions);
    };

    testCompletion("transactions[0].");
    testCompletion("transactions_2d[0][1].");
    testCompletion("nested_arr[3].");
    testCompletion("nested_arr[5].txn.");
}

TEST_CASE("PortListCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    // Create and save an interface with modports so it gets indexed
    auto intfDoc = server.openFile("test_intf.sv", R"(
    interface test_intf;
        logic valid;
        logic ready;
        logic [7:0] data;

        modport leader(output valid, output data, input ready);
        modport follower(input valid, input data, output ready);
    endinterface
    )");
    intfDoc.save();

    auto doc = server.openFile("port_list_test.sv", R"(
    module test_port_completion (
        input logic clk,
        // cursor in port list
    );

    endmodule


    module test_modpor_comps (
        test_intf.
    );
    endmodule
    )");

    // Test completions in port list - should have interfaces but NOT modules
    auto portListCursor = doc.before("// cursor in port list");
    auto portListLoc = doc.getLocation(portListCursor.m_offset);
    REQUIRE(portListLoc);
    CHECK(CompletionContext::fromLocation(*doc.doc, *portListLoc).kind ==
          CompletionContextKind::PortList);
    auto portListCompletions = portListCursor.getResolvedCompletions();

    // Test completions after "intf_inst." - should show interface members/modports
    auto modportCompletions = doc.after("test_intf.").getResolvedCompletions(".");

    // Helper to find completion by label
    auto findByLabel = [](const std::vector<lsp::CompletionItem>& items,
                          const std::string& label) -> const lsp::CompletionItem* {
        auto it = std::find_if(items.begin(), items.end(), [&](const lsp::CompletionItem& item) {
            return item.label == label;
        });
        return it != items.end() ? &(*it) : nullptr;
    };

    // Port list should NOT have Dut (module instantiation not valid in ports)
    CHECK(findByLabel(portListCompletions, "Dut") == nullptr);

    // Port list SHOULD have interface
    CHECK(findByLabel(portListCompletions, "test_intf") != nullptr);

    // Port list SHOULD have packages
    CHECK(findByLabel(portListCompletions, "base_pkg") != nullptr);

    auto dimensionDoc = server.openFile("port_dimension_test.sv", R"(
    module port_dimension_test #(
        parameter int WIDTH = 8
    ) (
        input logic [WIDTH-1:0] data
    );
    endmodule
    )");
    auto dimensionCursor = dimensionDoc.after("input logic [");
    auto dimensionLoc = dimensionDoc.getLocation(dimensionCursor.m_offset);
    REQUIRE(dimensionLoc);
    CHECK(CompletionContext::fromLocation(*dimensionDoc.doc, *dimensionLoc).kind ==
          CompletionContextKind::Expression);
    CHECK(findByLabel(dimensionCursor.getResolvedCompletions(), "WIDTH") != nullptr);

    // Interface member completions should have signals and modports

    golden.record("port_list", portListCompletions);
    golden.record("modports", modportCompletions);
}

TEST_CASE("NonProceduralSignalCompletion") {
    ServerHarness server("repo1");
    JsonGoldenTest golden;

    auto doc = server.openFile("assign_test.sv", R"(
    module assign_test (
        input  logic       clk,
        input  logic       a,
        output logic       b
    );
        logic internal_sig;
        wire  my_wire;

        assign
    endmodule
    )");

    auto assignCursor = doc.after("assign\n");
    auto assignLoc = doc.getLocation(assignCursor.m_offset);
    REQUIRE(assignLoc);
    CHECK(CompletionContext::fromLocation(*doc.doc, *assignLoc).kind ==
          CompletionContextKind::Expression);
    auto comps = assignCursor.getResolvedCompletions();
    golden.record("assign_lhs", comps);

    // An unfinished continuous assign still needs signals from the surrounding module body.
    auto findByLabel = [&](const std::string& label) {
        return std::find_if(comps.begin(), comps.end(),
                            [&](const lsp::CompletionItem& item) { return item.label == label; });
    };
    CHECK(findByLabel("internal_sig") != comps.end());
    CHECK(findByLabel("my_wire") != comps.end());
    CHECK(findByLabel("a") != comps.end());
    CHECK(findByLabel("b") != comps.end());
    CHECK(findByLabel("mailbox") == comps.end());
}

TEST_CASE("ProceduralBlockSignalCompletion") {
    ServerHarness server("repo1");

    auto doc = server.openFile("procedural_test.sv", R"(
    module procedural_test (
        input  logic       clk,
        input  logic       rst,
        output logic [7:0] data_out
    );
        logic internal_sig;

        always_ff @(posedge ) begin
            // cursor_here
        end
    endmodule
    )");

    auto findByLabel = [](const auto& items, const std::string& label) {
        return std::find_if(items.begin(), items.end(),
                            [&](const auto& item) { return item.m_item.label == label; });
    };

    // Inside procedural block body should include local signals
    auto bodyComps = doc.before("// cursor_here").getCompletions();
    CHECK(findByLabel(bodyComps, "internal_sig") != bodyComps.end());
    CHECK(findByLabel(bodyComps, "clk") != bodyComps.end());
    CHECK(findByLabel(bodyComps, "rst") != bodyComps.end());
    CHECK(findByLabel(bodyComps, "data_out") != bodyComps.end());

    // After posedge should also include signals (sensitivity list)
    auto sensComps = doc.after("posedge ").getCompletions();
    CHECK(findByLabel(sensComps, "internal_sig") != sensComps.end());
    CHECK(findByLabel(sensComps, "clk") != sensComps.end());
    CHECK(findByLabel(sensComps, "rst") != sensComps.end());
}

TEST_CASE("LocalparamExcludedFromCompletion") {
    // IEEE-1800 23.2.3: localparams in module header cannot be overwritten,
    // so they should be excluded from parameter completions when instantiating a module

    ServerHarness server("repo1");

    // Create a module with both parameter and localparam in header
    auto moduleDoc = server.openFile("module_with_localparam.sv", R"(
    module module_with_localparam #(
        parameter int normal_param = 0,
        localparam int local_param = 1,
        parameter int another_param = 2
    ) (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("test_localparam.sv", R"(
    module test_localparam;
        //inmodule

    endmodule
    )");

    auto cursor = doc.before("//inmodule");
    auto comps = cursor.getCompletions();

    // Find the module_with_localparam completion
    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "module_with_localparam";
    });

    REQUIRE(it != comps.end());
    auto comp = *it;
    comp.resolve();

    // The completion should include normal_param and another_param, but NOT local_param
    auto insertText = comp.m_item.insertText.value_or("");

    CHECK(insertText.find("normal_param") != std::string::npos);
    CHECK(insertText.find("another_param") != std::string::npos);
    CHECK(insertText.find("local_param") == std::string::npos);
}

TEST_CASE("LocalparamKeywordInheritance") {
    // IEEE-1800: When the keyword is omitted in a parameter port list,
    // it inherits from the previous entry. This tests that behavior.

    ServerHarness server("repo1");

    // Create a module where localparams inherit the keyword from previous entry
    auto moduleDoc = server.openFile("module_inherited_localparam.sv", R"(
    module module_inherited_localparam #(
        parameter int p1 = 0,
        int p2 = 1,              // inherits 'parameter' from p1
        localparam int lp1 = 2,
        int lp2 = 3,             // inherits 'localparam' from lp1
        parameter int p3 = 4     // explicit parameter again
    ) (
        input logic clk
    );
    endmodule
    )");
    moduleDoc.save();

    auto doc = server.openFile("test_inherited_localparam.sv", R"(
    module test_inherited_localparam;
        //inmodule

    endmodule
    )");

    auto cursor = doc.before("//inmodule");
    auto comps = cursor.getCompletions();

    auto it = std::find_if(comps.begin(), comps.end(), [](const CompletionHandle& item) {
        return item.m_item.label == "module_inherited_localparam";
    });

    REQUIRE(it != comps.end());
    auto comp = *it;
    comp.resolve();

    auto insertText = comp.m_item.insertText.value_or("");

    // p1, p2, and p3 should be included (they're parameters)
    CHECK(insertText.find("p1") != std::string::npos);
    CHECK(insertText.find("p2") != std::string::npos);
    CHECK(insertText.find("p3") != std::string::npos);

    // lp1 and lp2 should NOT be included (they're localparams)
    CHECK(insertText.find("lp1") == std::string::npos);
    CHECK(insertText.find("lp2") == std::string::npos);
}
