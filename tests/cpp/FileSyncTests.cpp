// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "lsp/LspTypes.h"
#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <rfl/Variant.hpp>

#include "slang/diagnostics/AnalysisDiags.h"

using namespace server;

TEST_CASE("BasicInsertion") {
    ServerHarness server;
    auto doc = server.openFile("top.sv", R"(module top;
    logic [3:0] a = 4'd?;
endmodule
)");

    doc.after("?;").write("    logic inserted\n");
    doc.publishChanges();

    doc.save(); // This validates the buffer
    doc.close();
}

TEST_CASE("FileLifeCycle") {
    ServerHarness server;
    auto hdl = server.openFile("tb.sv", R"(module top;
    logic [3:0] a = 4'd?;
endmodule
)");

    hdl.insert(26, "    logic inserted\n");
    hdl.publishChanges();
}

TEST_CASE("MultiChange") {
    ServerHarness server;
    auto doc = server.openFile("tb.sv", R"(module top;
    logic [3:0] a = 4'd?;
endmodule
)");
    doc.after("?;").write("    logic inserted;\n");
    doc.after("top;").write("    logic inserted2;\n");
    doc.after("endmodule").write("    module foo\n");
    doc.publishChanges();
    // This validates the buffer
    doc.save();
    doc.close();
}

TEST_CASE("MultiChangeWithAdditionsAndDeletions") {
    ServerHarness server;
    auto doc = server.openFile("test.sv", R"(module test;
    logic [7:0] data;
    logic clk;
    logic reset;
    logic enable;
endmodule
)");

    // Delete a line and add new content
    auto clkPos = doc.getText().find("    logic clk;\n");
    doc.erase(clkPos, clkPos + 15);
    doc.after("data;").write("\n    logic [31:0] counter;");
    doc.after("enable;").write("\n    logic valid;\n    logic ready;");

    doc.publishChanges();
    doc.save();
    doc.close();
}

TEST_CASE("DeletionFollowedByInsertionAtSameLocation") {
    ServerHarness server;
    auto doc = server.openFile("replace.sv", R"(module replace;
    logic old_signal;
    logic keep_this;
endmodule
)");

    // Find the position of "old_signal"
    auto oldSignalPos = doc.getText().find("old_signal");

    // Delete "old_signal" and replace with "new_signal"
    doc.erase(oldSignalPos, oldSignalPos + 10);
    doc.insert(oldSignalPos, "new_signal");

    // Add more content after the replacement
    doc.after("new_signal;").write("\n    logic [7:0] data_bus;");

    doc.publishChanges();
    doc.save();
    doc.close();
}

TEST_CASE("MultipleDeletionsAcrossDocument") {
    ServerHarness server;
    auto doc = server.openFile("deletions.sv", R"(module deletions;
    // Comment 1
    logic signal1;
    // Comment 2
    logic signal2;
    // Comment 3
    logic signal3;
    // Comment 4
    logic signal4;
endmodule
)");

    // Delete all comments (working backwards to maintain offsets)
    auto text = doc.getText();
    auto pos4 = text.find("    // Comment 4\n");
    auto pos3 = text.find("    // Comment 3\n");
    auto pos2 = text.find("    // Comment 2\n");
    auto pos1 = text.find("    // Comment 1\n");

    doc.erase(pos4, pos4 + 18);
    doc.erase(pos3, pos3 + 18);
    doc.erase(pos2, pos2 + 18);
    doc.erase(pos1, pos1 + 18);

    // Add some new content
    doc.after("signal2;")
        .write("\n    // New centralized comment\n    logic [31:0] combined_signal;");

    doc.publishChanges();
    doc.save();
    doc.close();
}

TEST_CASE("GotoDefinition_IncludedFileModification") {
    /// Test that goto definition works correctly when the included file is modified
    ServerHarness server("macro_test");

    // Open the main file that includes common_macros.svh
    auto memoryModule = server.openFile("memory_module.sv");
    memoryModule.ensureSynced();

    // Find the usage of `WIDTH macro
    auto cursor = memoryModule.after("DATA_WIDTH = ").before("`WIDTH");

    // Get initial definition in common_macros.svh
    auto initialDefs = cursor.getDefinitions();
    CHECK(initialDefs.size() == 1);

    auto& initialDef = initialDefs[0];
    auto originalLine = initialDef.targetRange.start.line;
    CHECK(initialDef.targetUri.str().find("common_macros.svh") != std::string::npos);

    // Modify the included file by adding newlines at the top
    auto macrosFile = server.openFile("common_macros.svh");
    macrosFile.insert(0, "\n\n\n");
    macrosFile.ensureSynced();

    // Get definitions again WITHOUT modifying the main file
    // The old syntax tree still references the old BufferID from common_macros.svh
    // With proper buffer invalidation, the old BufferID should be invalid
    // and force re-reading the file, giving us the updated line numbers
    auto newDefs = cursor.getDefinitions();
    CHECK(newDefs.size() == 1);

    auto& newDef = newDefs[0];
    CHECK(newDef.targetUri.str().find("common_macros.svh") != std::string::npos);
    CHECK(newDef.targetRange.start.line == originalLine + 3);
}

TEST_CASE("GotoDefinition_TwoLayerIncludeModification") {
    /// Test that goto definition works correctly when a file two layers deep is modified
    /// top.sv -> intermediate.svh -> base_defs.svh
    ServerHarness server("two_layer_include");

    // Open the top file that includes intermediate.svh, which includes base_defs.svh
    auto topModule = server.openFile("top.sv");
    topModule.ensureSynced();

    // Find the usage of `BUS_WIDTH macro (defined in base_defs.svh)
    auto cursor = topModule.after("WIDTH = ").before("`BUS_WIDTH");

    // Get initial definition in base_defs.svh
    auto initialDefs = cursor.getDefinitions();
    CHECK(initialDefs.size() == 1);

    auto& initialDef = initialDefs[0];
    auto originalLine = initialDef.targetRange.start.line;
    CHECK(initialDef.targetUri.str().find("base_defs.svh") != std::string::npos);

    // Modify the base file (two layers deep) by adding newlines at the top
    auto baseFile = server.openFile("base_defs.svh");
    baseFile.insert(0, "\n\n");
    baseFile.ensureSynced();

    // Get definitions again WITHOUT modifying the top file or intermediate file
    // The old syntax trees still reference the old BufferID from base_defs.svh
    // With proper buffer invalidation through the include chain, the old BufferID
    // should be invalid and force re-reading the file, giving us the updated line numbers
    auto newDefs = cursor.getDefinitions();
    CHECK(newDefs.size() == 1);

    auto& newDef = newDefs[0];
    CHECK(newDef.targetUri.str().find("base_defs.svh") != std::string::npos);
    CHECK(newDef.targetRange.start.line == originalLine + 2);
}

TEST_CASE("ExternalFileChange_ReReadBuffer") {
    /// Test that external file changes are detected and the buffer is re-read from disk
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_external";
    std::filesystem::create_directories(tempDir);

    // Create a temporary file
    auto tempFile = tempDir / "external_test.sv";
    {
        std::ofstream out(tempFile);
        out << R"(module original;
    logic [7:0] data;
endmodule
)";
    }

    // Set up server with temp directory as workspace
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    // Open the file via LSP
    auto uri = URI::fromFile(tempFile);
    std::string originalText;
    {
        std::ifstream in(tempFile);
        originalText = std::string((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    }

    server.onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = uri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = originalText}});

    // Verify initial content
    auto doc = server.getDoc(uri);
    REQUIRE(doc != nullptr);
    CHECK(doc->getText().find("original") != std::string::npos);
    CHECK(doc->getText().find("modified") == std::string::npos);

    // Modify the file on disk (external change)
    {
        std::ofstream out(tempFile);
        out << R"(module modified;
    logic [15:0] wider_data;
    logic extra_signal;
endmodule
)";
    }

    // Trigger external file change notification
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = uri, .type = lsp::FileChangeType::Changed}}}});

    // Verify the buffer was re-read from disk
    doc = server.getDoc(uri);
    REQUIRE(doc != nullptr);
    CHECK(doc->getText().find("modified") != std::string::npos);
    CHECK(doc->getText().find("original") == std::string::npos);
    CHECK(doc->getText().find("wider_data") != std::string::npos);
    CHECK(doc->getText().find("extra_signal") != std::string::npos);

    // Clean up
    std::filesystem::remove_all(tempDir);
}

TEST_CASE("ExternalFileChange_DiagnosticsUpdate") {
    /// Test that diagnostics are updated after an external file change
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_diag";
    std::filesystem::create_directories(tempDir);

    // Create a file with an error
    auto tempFile = tempDir / "diag_test.sv";
    {
        std::ofstream out(tempFile);
        out << R"(module with_error;
    logic [7:0] data;
    assign data = undefined_signal; // Error: undefined
endmodule
)";
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto uri = URI::fromFile(tempFile);
    std::string text;
    {
        std::ifstream in(tempFile);
        text = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    server.onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = uri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = text}});

    // Should have diagnostics for undefined signal
    auto initialDiags = server.client.getDiagnostics(uri);
    auto hasUndefinedError = [](const std::vector<lsp::Diagnostic>& diags) {
        for (const auto& d : diags) {
            if (d.message.find("undefined_signal") != std::string::npos)
                return true;
        }
        return false;
    };
    CHECK(hasUndefinedError(initialDiags));

    // Fix the error on disk
    {
        std::ofstream out(tempFile);
        out << R"(module fixed;
    logic [7:0] data;
    assign data = 8'hFF; // Fixed
endmodule
)";
    }

    // Trigger external file change
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = uri, .type = lsp::FileChangeType::Changed}}}});

    // The undefined_signal error should be gone after the fix
    auto newDiags = server.client.getDiagnostics(uri);
    CHECK(!hasUndefinedError(newDiags));

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("ExternalFileChange_SyntaxTreeInvalidation") {
    /// Test that the syntax tree is invalidated and reparsed after external changes
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_syntax";
    std::filesystem::create_directories(tempDir);

    auto tempFile = tempDir / "syntax_test.sv";
    {
        std::ofstream out(tempFile);
        out << R"(module one_module;
endmodule
)";
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto uri = URI::fromFile(tempFile);
    std::string text;
    {
        std::ifstream in(tempFile);
        text = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    server.onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = uri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = text}});

    // Get initial symbols
    auto doc = server.getDoc(uri);
    REQUIRE(doc != nullptr);
    auto initialSymbols = doc->getSymbols();
    CHECK(initialSymbols.size() == 1);
    CHECK(initialSymbols[0].name == "one_module");

    // Add another module on disk
    {
        std::ofstream out(tempFile);
        out << R"(module first_module;
endmodule

module second_module;
endmodule
)";
    }

    // Trigger external change
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = uri, .type = lsp::FileChangeType::Changed}}}});

    // Should now have two modules
    doc = server.getDoc(uri);
    REQUIRE(doc != nullptr);
    auto newSymbols = doc->getSymbols();
    CHECK(newSymbols.size() == 2);

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("ExternalFileChange_MultipleFilesUpdatedAtomically") {
    /// Test that when multiple files are changed externally, all buffers are updated
    /// before diagnostics are computed. This prevents transient errors when related
    /// files are modified together (e.g., adding a port to a child and parent).
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_atomic";
    std::filesystem::create_directories(tempDir);

    // Create child module
    auto childFile = tempDir / "child.sv";
    {
        std::ofstream out(childFile);
        out << R"(module child(
    input logic clk
);
endmodule
)";
    }

    // Create parent module that instantiates child
    auto parentFile = tempDir / "parent.sv";
    {
        std::ofstream out(parentFile);
        out << R"(module parent(
    input logic clk
);
    child u_child(
        .clk(clk)
    );
endmodule
)";
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto childUri = URI::fromFile(childFile);
    auto parentUri = URI::fromFile(parentFile);

    // Open both files
    auto readFile = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };

    server.onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = childUri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = readFile(childFile)}});

    server.onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = parentUri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = readFile(parentFile)}});

    // Verify both files are loaded correctly and no errors
    auto childDoc = server.getDoc(childUri);
    auto parentDoc = server.getDoc(parentUri);
    CHECK(server.client.getDiagnostics(childUri).size() == 1); // Unused port
    REQUIRE(childDoc != nullptr);
    REQUIRE(parentDoc != nullptr);

    // Now externally modify BOTH files to add a new port
    // Child gets a new 'reset' port
    {
        std::ofstream out(childFile);
        out << R"(module child(
    input logic clk,
    input logic reset
);
endmodule
)";
    }

    // Parent connects the new 'reset' port
    {
        std::ofstream out(parentFile);
        out << R"(module parent(
    input logic clk,
    input logic reset
);
    child u_child(
        .clk(clk),
        .reset(reset)
    );
endmodule
)";
    }

    // Trigger external change for BOTH files in a single notification
    // This simulates what happens when git checkout or a formatter modifies multiple files
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = childUri, .type = lsp::FileChangeType::Changed}},
                    {lsp::FileEvent{.uri = parentUri, .type = lsp::FileChangeType::Changed}}}});

    // Verify both files were updated
    childDoc = server.getDoc(childUri);
    parentDoc = server.getDoc(parentUri);
    REQUIRE(childDoc != nullptr);
    REQUIRE(parentDoc != nullptr);

    CHECK(childDoc->getText().find("reset") != std::string::npos);
    CHECK(parentDoc->getText().find("reset") != std::string::npos);

    // There should be no diagnostics about mismatched ports
    // If buffers were updated one at a time with diagnostics computed between,
    // we might see transient errors about missing/extra ports
    auto childDiags = server.client.getDiagnostics(childUri);
    CHECK(childDiags.size() == 2);
    for (const auto& diag : childDiags) {
        CHECK(rfl::get<std::string>(*diag.code) == "unused-port");
    }
    auto parentDiags = server.client.getDiagnostics(parentUri);
    CHECK(parentDiags.empty());

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("WatchedFiles_CreatedFileAddedToIndex") {
    /// Test that newly created files are added to the indexer
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_created";
    std::filesystem::create_directories(tempDir);
    tempDir = std::filesystem::canonical(tempDir); // Normalize path for Windows

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto& indexer = server.m_indexer;

    // Initially the indexer should have no entries for our module
    auto files = indexer.getFilesForSymbol("NewModule");
    CHECK(files.empty());

    // Create a new file on disk
    auto newFile = tempDir / "new_module.sv";
    {
        std::ofstream out(newFile);
        out << R"(module NewModule;
    logic data;
endmodule
)";
        out.flush();
    }

    // Notify the server about the new file
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = URI::fromFile(newFile),
                                    .type = lsp::FileChangeType::Created}}}});

    // Now the indexer should have an entry for NewModule
    files = indexer.getFilesForSymbol("NewModule");
    CHECK(files.size() == 1);
    CHECK(files[0] == newFile);

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("WatchedFiles_ChangedFileReindexed") {
    /// Test that changed files are re-indexed with new symbols
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_changed";
    std::filesystem::create_directories(tempDir);
    tempDir = std::filesystem::canonical(tempDir); // Normalize path for Windows

    // Create initial file
    auto testFile = tempDir / "changing.sv";
    {
        std::ofstream out(testFile);
        out << R"(module OldName;
endmodule
)";
        out.flush();
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto& indexer = server.m_indexer;

    // Server auto-indexes workspace, so the file should already be indexed
    CHECK(indexer.getFilesForSymbol("OldName").size() == 1);
    CHECK(indexer.getFilesForSymbol("NewName").empty());

    // Change the file on disk
    {
        std::ofstream out(testFile);
        out << R"(module NewName;
endmodule
)";
        out.flush();
    }

    // Notify the server about the change
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = URI::fromFile(testFile),
                                    .type = lsp::FileChangeType::Changed}}}});

    // OldName should be gone, NewName should be present
    CHECK(indexer.getFilesForSymbol("OldName").empty());
    CHECK(indexer.getFilesForSymbol("NewName").size() == 1);

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("WatchedFiles_DeletedFileRemovedFromIndex") {
    /// Test that deleted files are removed from the indexer
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_deleted";
    std::filesystem::create_directories(tempDir);
    tempDir = std::filesystem::canonical(tempDir); // Normalize path for Windows

    // Create file
    auto testFile = tempDir / "to_delete.sv";
    {
        std::ofstream out(testFile);
        out << R"(module ToBeDeleted;
endmodule
)";
        out.flush();
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto& indexer = server.m_indexer;

    // Server auto-indexes workspace
    CHECK(indexer.getFilesForSymbol("ToBeDeleted").size() == 1);

    // Delete the file on disk
    std::filesystem::remove(testFile);

    // Notify the server about the deletion
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {{lsp::FileEvent{.uri = URI::fromFile(testFile),
                                    .type = lsp::FileChangeType::Deleted}}}});

    // ToBeDeleted should be gone
    CHECK(indexer.getFilesForSymbol("ToBeDeleted").empty());

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("WatchedFiles_MultipleChangesProcessed") {
    /// Test that multiple file changes are processed correctly
    auto tempDir = std::filesystem::temp_directory_path() / "slang_test_multi";
    std::filesystem::create_directories(tempDir);
    tempDir = std::filesystem::canonical(tempDir); // Normalize path for Windows

    // Create initial files
    auto file1 = tempDir / "module1.sv";
    auto file2 = tempDir / "module2.sv";
    {
        std::ofstream out(file1);
        out << "module Module1; endmodule\n";
        out.flush();
    }
    {
        std::ofstream out(file2);
        out << "module Module2; endmodule\n";
        out.flush();
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {
            {lsp::WorkspaceFolder{.uri = URI::fromFile(tempDir), .name = "test"}}}});

    auto& indexer = server.m_indexer;

    // Server auto-indexes workspace
    CHECK(indexer.getFilesForSymbol("Module1").size() == 1);
    CHECK(indexer.getFilesForSymbol("Module2").size() == 1);

    // Delete file1, change file2, create file3
    std::filesystem::remove(file1);

    {
        std::ofstream out(file2);
        out << "module Module2Renamed; endmodule\n";
        out.flush();
    }

    auto file3 = tempDir / "module3.sv";
    {
        std::ofstream out(file3);
        out << "module Module3; endmodule\n";
        out.flush();
    }

    // Send all changes in one notification
    server.onWorkspaceDidChangeWatchedFiles(lsp::DidChangeWatchedFilesParams{
        .changes = {
            {lsp::FileEvent{.uri = URI::fromFile(file1), .type = lsp::FileChangeType::Deleted}},
            {lsp::FileEvent{.uri = URI::fromFile(file2), .type = lsp::FileChangeType::Changed}},
            {lsp::FileEvent{.uri = URI::fromFile(file3), .type = lsp::FileChangeType::Created}}}});

    // Verify final state
    CHECK(indexer.getFilesForSymbol("Module1").empty());
    CHECK(indexer.getFilesForSymbol("Module2").empty());
    CHECK(indexer.getFilesForSymbol("Module2Renamed").size() == 1);
    CHECK(indexer.getFilesForSymbol("Module3").size() == 1);

    std::filesystem::remove_all(tempDir);
}
