// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <chrono>
#include <fstream>

using namespace slang;

namespace {
struct TempWorkspace {
    fs::path base;
    fs::path workspace;
    fs::path vault;

    TempWorkspace() {
        auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        base = fs::temp_directory_path() / ("slang-server-symlink-" + suffix);
        workspace = base / "workspace";
        vault = base / "vault";
        fs::create_directories(workspace / "designs");
        fs::create_directories(vault);
    }

    ~TempWorkspace() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
};

void writeFile(const fs::path& path, std::string_view text) {
    std::ofstream out(path);
    out << text;
}
} // namespace

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

TEST_CASE("GotoDefinition_PrefersIndexedWorkspaceSymlink") {
    TempWorkspace temp;

    auto topTarget = temp.vault / "abcd12345_top";
    auto depTarget = temp.vault / "abcd12345_dep";
    auto topLink = temp.workspace / "designs" / "top.sv";
    auto depLink = temp.workspace / "designs" / "dep.sv";

    writeFile(topTarget, R"(module Top;
    Dep u_dep();
endmodule
)");
    writeFile(depTarget, R"(module Dep;
endmodule
)");

    std::error_code ec;
    fs::create_symlink(topTarget, topLink, ec);
    if (ec)
        SKIP("Unable to create symlink");
    fs::create_symlink(depTarget, depLink, ec);
    if (ec)
        SKIP("Unable to create symlink");

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{.uri = URI::fromFile(temp.workspace),
                                                   .name = "symlink-workspace"}}}});

    auto hdl = server.openFile("designs/top.sv");
    auto defs = hdl.after("    ").getDefinitions();

    REQUIRE(defs.size() == 1);
    CHECK(defs[0].targetUri == URI::fromFile(depLink));
    CHECK(defs[0].targetUri != URI::fromFile(depTarget));
}

TEST_CASE("GotoDefinition_PrefersBuildFileWorkspaceSymlink") {
    TempWorkspace temp;

    auto topTarget = temp.vault / "abcd12345_top";
    auto depTarget = temp.vault / "abcd12345_dep";
    auto topLink = temp.workspace / "designs" / "top.sv";
    auto depLink = temp.workspace / "designs" / "dep.sv";
    auto buildFile = temp.workspace / "build.f";

    writeFile(topTarget, R"(module Top;
    Dep u_dep();
endmodule
)");
    writeFile(depTarget, R"(module Dep;
endmodule
)");

    std::error_code ec;
    fs::create_symlink(topTarget, topLink, ec);
    if (ec)
        SKIP("Unable to create symlink");
    fs::create_symlink(depTarget, depLink, ec);
    if (ec)
        SKIP("Unable to create symlink");

    writeFile(buildFile, topLink.string() + "\n" + depLink.string() + "\n");

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{.uri = URI::fromFile(temp.workspace),
                                                   .name = "symlink-workspace"}}}});
    server.setBuildFile(buildFile.string());

    auto hdl = server.openFile("designs/top.sv");
    auto defs = hdl.after("    ").getDefinitions();

    REQUIRE(defs.size() == 1);
    CHECK(defs[0].targetUri == URI::fromFile(depLink));
    CHECK(defs[0].targetUri != URI::fromFile(depTarget));
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
