// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

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
         hdl.before("duplicate d").m_offset, hdl.before("VALUE =").m_offset,
         hdl.after("VALUE = ").m_offset, hdl.after("`ifdef ").m_offset});
    scanner.scanDocument(hdl);
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

TEST_CASE("HoverDeduplicatesIdenticalIndexedDefinitions") {
    ServerHarness server;

    auto first = server.openFile("first.sv", R"(
interface duplicate_iface;
endinterface
)");
    first.save();
    auto second = server.openFile("second.sv", R"(
interface duplicate_iface;
endinterface
)");
    second.save();

    auto use = server.openFile("use.sv", R"(
module top;
    duplicate_iface instance();
endmodule
)");
    auto cursor = use.before("duplicate_iface instance");

    CHECK(cursor.getDefinitions().size() == 2);

    auto hover = use.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
    auto declaration = content.find("interface duplicate_iface;");
    REQUIRE(declaration != std::string::npos);
    CHECK(content.find("interface duplicate_iface;", declaration + 1) == std::string::npos);
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
    /// Tests the queue-based cycle detection in getDependentDocs.
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
