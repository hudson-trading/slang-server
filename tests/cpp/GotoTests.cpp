// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"

using namespace slang;

TEST_CASE("FindSyntax") {
    /// Find the syntax at each location in the file
    ServerHarness server("");
    auto hdl = server.openFile("all.sv");

    SyntaxScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindSymbolRef") {
    /// Find the referenced symbol at each location in the file, if any.
    ServerHarness server("");
    auto hdl = server.openFile("all.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindSymbolRefHdl") {
    /// Find the referenced symbol at each location in the comms test file.
    ServerHarness server("");
    auto hdl = server.openFile("hdl_test.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindSymbolRefMacro") {
    /// Find the referenced symbol at each location in the macro test file.
    ServerHarness server("macro_test");
    auto hdl = server.openFile("macros.svh");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindMultiSymbolRef") {
    ServerHarness server("multi_symbol");
    auto first = server.openFile("first.sv");
    first.save();
    auto second = server.openFile("second.sv");
    second.save();
    auto hdl = server.openFile("use.sv");

    SymbolRefScanner scanner(
        {hdl.after("import features::").m_offset, hdl.after("export features::").m_offset,
         hdl.after("modport endpoint(output ").m_offset, hdl.after("import task ").m_offset,
         hdl.after("module top;\n    import ").m_offset, hdl.after("leaf u(.").m_offset,
         hdl.before("duplicate d").m_offset, hdl.after("genvar ").m_offset,
         hdl.before("VALUE =").m_offset, hdl.after("VALUE = ").m_offset,
         hdl.after("`ifdef ").m_offset});
    scanner.scanDocument(hdl);
}

TEST_CASE("HoverAndGotoHierarchicalInterfaceTypeParameter") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
typedef logic [7:0] payload_t;

interface stream_if #(
    parameter type data_type = logic
);
endinterface

module top;
    stream_if #(.data_type(payload_t)) decoded_event_fifo();
    localparam type resolved_t = type(decoded_event_fifo.data_type);
endmodule
)");

    auto declaration = doc.before("data_type = logic");
    auto use = doc.after("decoded_event_fifo.");
    auto definitions = use.getDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front().targetSelectionRange.start == declaration.getPosition());

    auto hover = doc.getHoverAt(use.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("Value: [`payload_t (aka logic[7:0])`](<file:") != std::string::npos);
}

TEST_CASE("GotoDefinition_UndefDirective") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define MY_MACRO 42
module top;
    localparam int x = `MY_MACRO;
endmodule
`undef MY_MACRO
)");

    // Goto definition on MY_MACRO in the `undef should go to the `define
    auto cursor = doc.after("`undef ");
    auto defs = cursor.getDefinitions();
    REQUIRE(!defs.empty());

    // Should point to the `define line
    CHECK(defs[0].targetRange.start.line == 1);
}

TEST_CASE("GotoDefinition_IfdefDirective") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define MY_MACRO 42
module top;
    localparam int x = `MY_MACRO;
endmodule
`ifdef MY_MACRO
`endif
)");

    // Goto definition on MY_MACRO in the `ifdef should go to the `define
    auto cursor = doc.after("`ifdef ");
    auto defs = cursor.getDefinitions();
    REQUIRE(!defs.empty());

    // Should point to the `define line
    CHECK(defs[0].targetRange.start.line == 1);
}

TEST_CASE("GotoDefinition_IfndefDirective") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`define GUARD 1
`ifndef GUARD
`endif
)");

    auto cursor = doc.after("`ifndef ");
    auto defs = cursor.getDefinitions();
    REQUIRE(!defs.empty());

    CHECK(defs[0].targetRange.start.line == 1);
}

TEST_CASE("GotoDefinition_IfdefUndefinedMacro") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
`ifdef NONEXISTENT
`endif
)");

    auto cursor = doc.after("`ifdef ");
    auto defs = cursor.getDefinitions();
    CHECK(defs.empty());
}

TEST_CASE("GotoDefinition_HonorsLinkSupport") {
    auto checkResultType = [](std::optional<bool> linkSupport) {
        lsp::InitializeParams params;
        params.capabilities.textDocument.emplace();
        params.capabilities.textDocument->definition = lsp::DefinitionClientCapabilities{
            .linkSupport = linkSupport};
        ServerHarness server(std::move(params));

        auto doc = server.openFile("test.sv", R"(
module top;
    int value;
    initial value = value;
endmodule
)");
        auto cursor = doc.after("initial value = ");
        auto result = server.getDocDefinition(lsp::DefinitionParams{
            .textDocument = {.uri = doc.m_uri}, .position = cursor.getPosition()});

        if (linkSupport.value_or(false)) {
            CHECK(rfl::holds_alternative<std::vector<lsp::DefinitionLink>>(result));
        }
        else {
            CHECK(rfl::holds_alternative<lsp::Definition>(result));
            const auto& definition = rfl::get<lsp::Definition>(result);
            CHECK(rfl::holds_alternative<std::vector<lsp::Location>>(definition));
        }
    };

    checkResultType(std::nullopt);
    checkResultType(true);
}

namespace {
void recordTypeDefinition(DocumentHandle& doc, JsonGoldenTest& golden,
                          std::vector<lsp::LocationLink> links) {
    REQUIRE(links.size() == 1);
    const auto& start = links[0].targetSelectionRange.start;
    auto line = doc.getLine(start.line + 1);
    if (line.ends_with('\n'))
        line.remove_suffix(1);
    golden.record(std::string(line), links[0]);
};
} // namespace

TEST_CASE("GotoTypeDefinition_TypedefStuct") {
    ServerHarness server;
    JsonGoldenTest golden;
    auto doc = server.openFile("test.sv", R"(
package p;
    typedef struct packed { logic [7:0] data; } payload_t;
endpackage
module top;
    p::payload_t payload;
endmodule
)");

    recordTypeDefinition(doc, golden, doc.before("payload;").getTypeDefinitions());
}

TEST_CASE("GotoTypeDefinition_ArrayTypedefStruct") {
    ServerHarness server;
    JsonGoldenTest golden;
    auto doc = server.openFile("test.sv", R"(
package p;
    typedef struct packed { logic [7:0] data; } payload_t;
endpackage
module top;
    p::payload_t payloads [2];
endmodule
)");
    recordTypeDefinition(doc, golden, doc.before("payloads [2]").getTypeDefinitions());
}

TEST_CASE("GotoTypeDefinition_Typedef") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
package p;
    typedef struct packed { logic [7:0] data; } payload_t;
endpackage
)");

    const auto links = doc.before("payload_t").getTypeDefinitions();

    // Typedefs use same target as goto-definition: typedef name itself.
    REQUIRE(links.size() == 1);
    CHECK(links[0].targetSelectionRange.start.line == 2);
}

TEST_CASE("GotoTypeDefinition_Instance") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module leaf;
endmodule
module top;
    leaf u_leaf();
endmodule
)");

    const auto links = doc.before("u_leaf").getTypeDefinitions();

    REQUIRE(links.size() == 1);
    CHECK(links[0].targetSelectionRange.start.line == 1);
    CHECK(links[0].targetSelectionRange.start.character == 7);
}

TEST_CASE("GotoTypeDefinition_Enum") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module top;
    enum { IDLE, BUSY } state;
    initial state = IDLE;
endmodule
)");

    const auto links = doc.before("IDLE;").getTypeDefinitions();

    REQUIRE(links.size() == 1);
    CHECK(links[0].targetSelectionRange.start.line == 2);
    CHECK(links[0].targetSelectionRange.start.character == 4);
}

TEST_CASE("GotoTypeDefinition_BuiltinType") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module m;
    logic subject;
endmodule
)");

    const auto links = doc.before("subject").getTypeDefinitions();

    // builtins don't have a type we can go to
    CHECK(links.size() == 0);
}

TEST_CASE("GotoTypeDefinition_BuiltinTypeBitArray") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module m;
    bit [7:0] subject;
endmodule
)");

    const auto links = doc.before("subject").getTypeDefinitions();

    // builtins don't have a type we can go to
    CHECK(links.size() == 0);
}

TEST_CASE("GotoDefinition_AllIndexedModuleDefinitions") {
    ServerHarness server;

    auto first = server.openFile("first.sv", R"(
module duplicate #(parameter int FIRST_VALUE = 1);
endmodule
)");
    first.save();
    auto second = server.openFile("second.sv", R"(
module duplicate #(parameter int SECOND_VALUE = 2);
endmodule
)");
    second.save();

    auto use = server.openFile("use.sv", R"(
module top;
    duplicate u();
endmodule
)");
    auto cursor = use.before("duplicate u");

    auto defs = cursor.getDefinitions();
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].targetUri != defs[1].targetUri);

    auto hover = use.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("FIRST_VALUE") != std::string::npos);
    CHECK(content.find("SECOND_VALUE") != std::string::npos);
}

TEST_CASE("GotoDefinition_SemanticDefinitionExcludesWorkspaceNamesake") {
    ServerHarness server;

    auto indexed = server.openFile("indexed.sv", R"(
interface chosen_if #(parameter int INDEXED_VALUE = 1);
endinterface
)");
    indexed.save();

    auto use = server.openFile("use.sv", R"(
interface chosen_if #(parameter int LOCAL_VALUE = 2);
endinterface

module top(chosen_if port);
endmodule
)");
    auto cursor = use.after("module top(");

    auto defs = cursor.getDefinitions();
    REQUIRE(defs.size() == 1);
    CHECK(defs[0].targetUri == use.m_uri);

    auto hover = use.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("LOCAL_VALUE") != std::string::npos);
    CHECK(content.find("INDEXED_VALUE") == std::string::npos);
}

TEST_CASE("GotoDefinition_MacroGeneratedModuleDefinition") {
    ServerHarness server;

    auto generated = server.openFile("generated.sv", R"(
`define DECLARE_DUPLICATE module duplicate #(parameter int MACRO_VALUE = 1); endmodule
`DECLARE_DUPLICATE
)");
    generated.save();
    auto declared = server.openFile("declared.sv", R"(
module duplicate #(parameter int DECLARED_VALUE = 2);
endmodule
)");
    declared.save();

    auto use = server.openFile("use.sv", R"(
module top;
    duplicate u();
endmodule
)");
    auto cursor = use.before("duplicate u");

    auto defs = cursor.getDefinitions();
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].targetUri != defs[1].targetUri);

    auto hover = use.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("MACRO_VALUE") != std::string::npos);
    CHECK(content.find("DECLARED_VALUE") != std::string::npos);
}

TEST_CASE("GotoDefinition_AllIndexedPackageDefinitions") {
    ServerHarness server;

    auto first = server.openFile("first_pkg.sv", R"(
package automatic duplicate_pkg;
    localparam int FIRST_VALUE = 1;
endpackage
)");
    first.save();
    auto second = server.openFile("second_pkg.sv", R"(
package static duplicate_pkg;
    localparam int SECOND_VALUE = 2;
endpackage
)");
    second.save();

    auto use = server.openFile("use_pkg.sv", R"(
module top;
    import duplicate_pkg::*;
endmodule
)");
    auto cursor = use.before("duplicate_pkg");

    auto defs = cursor.getDefinitions();
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].targetUri != defs[1].targetUri);

    auto hover = use.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("package automatic duplicate_pkg") != std::string::npos);
    CHECK(content.find("package static duplicate_pkg") != std::string::npos);
}

TEST_CASE("GotoDefinition_AllIndexedMacroDefinitions") {
    ServerHarness server;

    auto first = server.openFile("first_macro.sv", R"(
`define DUPLICATE_MACRO FIRST_VALUE
)");
    first.save();
    auto second = server.openFile("second_macro.sv", R"(
`define DUPLICATE_MACRO SECOND_VALUE
)");
    second.save();

    auto use = server.openFile("use_macro.sv", R"(
`ifdef DUPLICATE_MACRO
`endif
)");
    auto cursor = use.after("`ifdef ");

    auto defs = cursor.getDefinitions();
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].targetUri != defs[1].targetUri);

    auto hover = use.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(content.find("FIRST_VALUE") != std::string::npos);
    CHECK(content.find("SECOND_VALUE") != std::string::npos);
}

TEST_CASE("LoadTransitivePackages") {
    /// Find the referenced symbol at each location in files with circular package dependencies.
    /// Tests the queue-based cycle detection while loading dependent trees.
    ServerHarness server("repo1"); // Use repo1 as workspace folder
    auto hdl = server.openFile("cycle_test.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_all.sv") {
    /// Find references at each location in all.sv
    ServerHarness server("");
    auto hdl = server.openFile("all.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_references_test.sv") {
    /// Find references at each location in references_test.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_modules.sv") {
    /// Find references at each location in modules.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("modules.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_classes.sv") {
    /// Find references at each location in classes.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("classes.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesModuleCrossfile.sv") {
    /// Find references at each location in classes.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("macro_crossfile.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_struct_enum_refs.sv") {
    /// Find references at each location in struct_enum_refs.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_crossfile_pkg.sv") {
    /// Find references at each location in crossfile_pkg.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("crossfile_pkg.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}

TEST_CASE("FindReferencesAllTokens_crossfile_module.sv") {
    /// Find references at each location in crossfile_module.sv
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("crossfile_module.sv");

    ReferencesScanner scanner(server);
    scanner.scanDocument(hdl);
}
