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
