// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "Indexer.h"
#include "catch2/catch_test_macros.hpp"
#include "utils/ServerHarness.h"
#include "utils/Utils.h"
#include <filesystem>

using namespace server;

namespace {
std::string getTestDataPath() {
    return (findSlangRoot() / "tests" / "data" / "indexer_test").string();
}
} // namespace

TEST_CASE("Index module declarations") {
    Indexer indexer;
    auto testPath = getTestDataPath();
    indexer.startIndexing({testPath + "/modules.sv"}, {});

    auto files = indexer.getRelevantFilesForName("m1");
    CHECK(files.size() == 1);

    files = indexer.getRelevantFilesForName("m2");
    CHECK(files.size() == 1);

    files = indexer.getRelevantFilesForName("Iface");
    CHECK(files.size() == 1);
}

TEST_CASE("Don't index nested modules (they're private)") {
    Indexer indexer;
    auto testPath = getTestDataPath();
    indexer.startIndexing({testPath + "/nested.sv"}, {});

    // Should only have outer module, not inner (it's private)
    CHECK(indexer.symbolToFiles.size() == 1);

    // Check we have "outer"
    auto files = indexer.getRelevantFilesForName("outer");
    CHECK(files.size() == 1);

    // Verify inner module is not in the index
    files = indexer.getRelevantFilesForName("inner");
    CHECK(files.empty());
}

TEST_CASE("Index classes") {
    Indexer indexer;
    auto testPath = getTestDataPath();
    indexer.startIndexing({testPath + "/classes.sv"}, {});

    auto files = indexer.getRelevantFilesForName("MyClass");
    CHECK(files.size() == 1);

    CHECK(indexer.symbolToFiles.size() == 2);

    // Check both are classes
    for (const auto& [name, entries] : indexer.symbolToFiles) {
        for (const auto& entry : entries) {
            CHECK(entry.kind == slang::syntax::SyntaxKind::ClassDeclaration);
        }
    }
}

TEST_CASE("Index macros when no modules present") {
    Indexer indexer;
    auto testPath = getTestDataPath();
    indexer.startIndexing({testPath + "/macros.sv"}, {});

    auto files = indexer.getFilesForMacro("MY_MACRO");
    CHECK(files.size() == 1);

    files = indexer.getFilesForMacro("ANOTHER_MACRO");
    CHECK(files.size() == 1);
}

TEST_CASE("Don't index macros when modules present") {
    Indexer indexer;
    auto testPath = getTestDataPath();
    indexer.startIndexing({testPath + "/macros_with_module.sv"}, {});

    // Macros should not be indexed when modules are present
    auto files = indexer.getFilesForMacro("MY_MACRO");
    CHECK(files.empty());

    // But modules should be indexed
    files = indexer.getRelevantFilesForName("m");
    CHECK(files.size() == 1);
}

TEST_CASE("Index directory directly (no glob patterns)") {
    ServerHarness server("indexer_test");
    auto& indexer = server.m_indexer;

    // Should find all symbols in the directory
    auto files = indexer.getRelevantFilesForName("m1");
    CHECK(files.size() == 1);

    files = indexer.getRelevantFilesForName("m2");
    CHECK(files.size() == 1);

    files = indexer.getRelevantFilesForName("MyClass");
    CHECK(files.size() == 1);

    files = indexer.getRelevantFilesForName("outer");
    CHECK(files.size() == 1);

    // Should have found macros from macro-only files
    files = indexer.getFilesForMacro("MY_MACRO");
    CHECK(files.size() == 1);
}

// Document lifecycle tests using ServerHarness
TEST_CASE("Index document lifecycle - open does not add to global index") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open a document with a module
    auto doc = server.openFile("test.sv", R"(
module TestModule;
    logic a;
endmodule
)");

    // Module should NOT be in the global index yet (only in open documents)
    auto files = indexer.getRelevantFilesForName("TestModule");
    CHECK(files.empty());

    doc.close();
}

TEST_CASE("Index document lifecycle - save adds to global index") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open a document with a module
    auto doc = server.openFile("test.sv", R"(
module TestModule;
    logic a;
endmodule
)");

    // Save the document - this should add symbols to the global index
    doc.save();

    // Now module should be in the global index
    auto files = indexer.getRelevantFilesForName("TestModule");
    CHECK(files.size() == 1);

    doc.close();
}

TEST_CASE("Index document lifecycle - update changes symbols in global index") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open and save a document with one module
    auto doc = server.openFile("test.sv", R"(
module OldModule;
    logic a;
endmodule
)");
    doc.save();

    // Verify the old module is indexed
    auto files = indexer.getRelevantFilesForName("OldModule");
    CHECK(files.size() == 1);

    // Modify the document to have a different module
    auto text = doc.getText();
    auto pos = text.find("OldModule");
    doc.erase(pos, pos + 9);
    doc.insert(pos, "NewModule");
    doc.publishChanges();
    doc.save();

    // Old module should be removed from index
    files = indexer.getRelevantFilesForName("OldModule");
    CHECK(files.empty());

    // New module should be in index
    files = indexer.getRelevantFilesForName("NewModule");
    CHECK(files.size() == 1);

    doc.close();
}

TEST_CASE("Index document lifecycle - close keeps saved content in index") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open, save, and close a document
    auto doc = server.openFile("test.sv", R"(
module TestModule;
    logic a;
endmodule
)");
    doc.save();
    doc.close();

    // Module should still be in the global index after close
    auto files = indexer.getRelevantFilesForName("TestModule");
    CHECK(files.size() == 1);
}

TEST_CASE("Index document lifecycle - adding symbols") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open with one module
    auto doc = server.openFile("test.sv", R"(
module Module1;
    logic a;
endmodule
)");
    doc.save();

    // Verify first module is indexed
    auto files = indexer.getRelevantFilesForName("Module1");
    CHECK(files.size() == 1);

    // Add another module
    doc.after("endmodule").write(R"(

module Module2;
    logic b;
endmodule
)");
    doc.publishChanges();
    doc.save();

    // Both modules should be in index
    files = indexer.getRelevantFilesForName("Module1");
    CHECK(files.size() == 1);

    files = indexer.getRelevantFilesForName("Module2");
    CHECK(files.size() == 1);

    doc.close();
}

TEST_CASE("Index document lifecycle - removing symbols") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open with two modules
    auto doc = server.openFile("test.sv", R"(
module Module1;
    logic a;
endmodule

module Module2;
    logic b;
endmodule
)");
    doc.save();

    // Both should be indexed
    auto files = indexer.getRelevantFilesForName("Module1");
    CHECK(files.size() == 1);
    files = indexer.getRelevantFilesForName("Module2");
    CHECK(files.size() == 1);

    // Remove Module2
    auto text = doc.getText();
    auto pos = text.find("module Module2");
    auto endPos = text.find("endmodule", pos) + 9;
    doc.erase(pos, endPos + 1); // +1 for newline
    doc.publishChanges();
    doc.save();

    // Module1 should still be there
    files = indexer.getRelevantFilesForName("Module1");
    CHECK(files.size() == 1);

    // Module2 should be removed
    files = indexer.getRelevantFilesForName("Module2");
    CHECK(files.empty());

    doc.close();
}

TEST_CASE("Index document lifecycle - macros") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open file with only macros (no modules)
    auto doc = server.openFile("test.sv", R"(
`define MY_MACRO 42
`define ANOTHER_MACRO "hello"
)");
    doc.save();

    // Macros should be indexed
    auto files = indexer.getFilesForMacro("MY_MACRO");
    CHECK(files.size() == 1);

    files = indexer.getFilesForMacro("ANOTHER_MACRO");
    CHECK(files.size() == 1);

    // Remove one macro
    auto text = doc.getText();
    auto pos = text.find("`define ANOTHER_MACRO");
    auto endPos = text.find("\n", pos);
    doc.erase(pos, endPos + 1);
    doc.publishChanges();
    doc.save();

    // MY_MACRO should still be there
    files = indexer.getFilesForMacro("MY_MACRO");
    CHECK(files.size() == 1);

    // ANOTHER_MACRO should be removed
    files = indexer.getFilesForMacro("ANOTHER_MACRO");
    CHECK(files.empty());

    doc.close();
}

TEST_CASE("Index document lifecycle - URI interning") {
    ServerHarness server;
    auto& indexer = server.m_indexer;

    // Open and save multiple documents
    auto doc1 = server.openFile("test1.sv", "module M1; endmodule");
    doc1.save();

    auto doc2 = server.openFile("test2.sv", "module M2; endmodule");
    doc2.save();

    auto doc3 = server.openFile("test3.sv", "module M3; endmodule");
    doc3.save();

    // Verify all modules are indexed
    CHECK(indexer.getRelevantFilesForName("M1").size() == 1);
    CHECK(indexer.getRelevantFilesForName("M2").size() == 1);
    CHECK(indexer.getRelevantFilesForName("M3").size() == 1);

    // Verify we have 3 unique URIs
    CHECK(indexer.uniqueUris.size() == 3);

    doc1.close();
    doc2.close();
    doc3.close();
}
