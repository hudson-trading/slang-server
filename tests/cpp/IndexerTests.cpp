// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "Indexer.h"
#include "catch2/catch_test_macros.hpp"
#include "utils/ServerHarness.h"
#include "utils/Utils.h"
#include <filesystem>
#include <fstream>

#include "slang/util/OS.h"
#include "slang/util/ScopeGuard.h"

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

    auto files = indexer.getFilesForSymbol("m1");
    CHECK(files.size() == 1);

    files = indexer.getFilesForSymbol("m2");
    CHECK(files.size() == 1);

    files = indexer.getFilesForSymbol("Iface");
    CHECK(files.size() == 1);
}

TEST_CASE("Index include filenames and complete relative paths") {
    ServerHarness server("include_index");
    auto& indexer = server.m_indexer;
    auto root = fs::current_path();

    CHECK(indexer.getFilesForInclude("members.svh") ==
          std::vector<fs::path>{root / "types/members.svh"});
    CHECK(indexer.getFilesForInclude("detail/width.svh") ==
          std::vector<fs::path>{root / "types/detail/width.svh"});
    CHECK(indexer.getFilesForInclude("./detail/width.svh") ==
          indexer.getFilesForInclude("detail/width.svh"));
    CHECK(indexer.getFilesForInclude("pkg.sv") == std::vector<fs::path>{root / "pkg.sv"});
    CHECK(indexer.getFilesForInclude("duplicate.svh") ==
          std::vector<fs::path>{root / "a/duplicate.svh", root / "b/duplicate.svh"});
    CHECK(indexer.getFilesForInclude("a/duplicate.svh") ==
          std::vector<fs::path>{root / "a/duplicate.svh"});
    CHECK(indexer.getFilesForInclude("wrong/detail/width.svh").empty());
    CHECK(indexer.getFilesForInclude("../members.svh").empty());
    CHECK(indexer.getFilesForInclude("missing.svh").empty());
    CHECK(indexer.getFilesForInclude("").empty());
    CHECK(indexer.getFilesForInclude((root / "types/members.svh").string()).empty());
    CHECK(indexer.getHeadersForSymbol("get_value") ==
          std::vector<fs::path>{root / "types/members.svh"});
    CHECK(indexer.getHeadersForSymbol("header_item") ==
          std::vector<fs::path>{root / "types/value.svh"});
    CHECK(indexer.getHeadersForSymbol("value_t") ==
          std::vector<fs::path>{root / "types/value.svh"});
    CHECK(indexer.getHeadersForSymbol("private_member").empty());
}

TEST_CASE("Index outermost class declarations for include quick fixes") {
    ServerHarness server("class_include_context");
    auto root = fs::current_path();
    auto& indexer = server.m_indexer;
    CHECK(indexer.getHeadersForSymbol("base_sequence") ==
          std::vector<fs::path>{root / "src/base_sequence.sv"});

    auto temporary = fs::weakly_canonical(fs::temp_directory_path()) /
                     fmt::format("slang_class_file_{}", slang::OS::getpid());
    fs::create_directories(temporary);
    slang::ScopeGuard cleanup([&] { fs::remove_all(temporary); });
    auto path = temporary / "item.sv";
    const auto uri = URI::fromFile(path);
    std::ofstream(path) << "virtual class item; endclass\n";
    indexer.onWorkspaceDidChangeWatchedFiles({.changes = {{uri, lsp::FileChangeType::Created}}});
    CHECK(indexer.getHeadersForSymbol("item") == std::vector<fs::path>{path});
    for (const auto& text :
         {"package p; class item; endclass endpackage\n",
          "module m; class item; endclass endmodule\n", "typedef class item;\n"}) {
        std::ofstream(path) << text;
        indexer.onWorkspaceDidChangeWatchedFiles(
            {.changes = {{uri, lsp::FileChangeType::Changed}}});
        CHECK(indexer.getHeadersForSymbol("item").empty());
    }

    slang::SourceManager sm;
    auto buffer = sm.assignText(path.string(), "class item; endclass\n");
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, sm);
    indexer.updateDocument(path, *tree);
    CHECK(indexer.getHeadersForSymbol("item") == std::vector<fs::path>{path});
    auto wrapper = temporary / "wrapper.sv";
    auto wrapperBuffer = sm.assignText(wrapper.string(), "`include \"item.sv\"\n");
    auto wrapperTree = slang::syntax::SyntaxTree::fromBuffer(wrapperBuffer, sm);
    REQUIRE(wrapperTree->diagnostics().empty());
    indexer.updateDocument(wrapper, *wrapperTree);
    CHECK(indexer.getHeadersForSymbol("item") == std::vector<fs::path>{path});
    indexer.onWorkspaceDidChangeWatchedFiles({.changes = {{uri, lsp::FileChangeType::Deleted}}});
    CHECK(indexer.getHeadersForSymbol("item").empty());
}

TEST_CASE("Index include candidates stay fixed until a full reindex") {
    ServerHarness server("include_index");
    auto& indexer = server.m_indexer;
    auto path = fs::current_path() / "types/members.svh";
    auto uri = URI::fromFile(path);

    indexer.addDocuments({path, path});
    CHECK(indexer.getFilesForInclude("members.svh") == std::vector<fs::path>{path});
    indexer.onWorkspaceDidChangeWatchedFiles({.changes = {{uri, lsp::FileChangeType::Changed}}});
    CHECK(indexer.getFilesForInclude("members.svh") == std::vector<fs::path>{path});
    indexer.onWorkspaceDidChangeWatchedFiles({.changes = {{uri, lsp::FileChangeType::Deleted}}});
    CHECK(indexer.getFilesForInclude("members.svh") == std::vector<fs::path>{path});
    CHECK(indexer.getHeadersForSymbol("get_value").empty());
    indexer.onWorkspaceDidChangeWatchedFiles({.changes = {{uri, lsp::FileChangeType::Created}}});
    CHECK(indexer.getFilesForInclude("members.svh") == std::vector<fs::path>{path});

    auto doc = server.openFile("saved.svh", "class saved; endclass\n");
    CHECK(indexer.getFilesForInclude("saved.svh").empty());
    doc.save();
    doc.save();
    CHECK(indexer.getFilesForInclude("saved.svh").empty());
    CHECK(indexer.getHeadersForSymbol("saved") ==
          std::vector<fs::path>{fs::current_path() / "saved.svh"});
    doc.replaceAll("class renamed; endclass\n");
    doc.save();
    CHECK(indexer.getHeadersForSymbol("saved").empty());
    CHECK(indexer.getHeadersForSymbol("renamed") ==
          std::vector<fs::path>{fs::current_path() / "saved.svh"});
    CHECK(indexer.getFilesForInclude("saved.svh").empty());

    indexer.startIndexing(std::vector<std::string>{path.string()}, {});
    CHECK(indexer.getFilesForInclude("members.svh") == std::vector<fs::path>{path});
    CHECK(indexer.getFilesForInclude("entry.svh").empty());
}

TEST_CASE("Index records direct relative and nested includers") {
    ServerHarness server("header_context");
    auto root = fs::current_path();
    CHECK(server.m_indexer.getFilesIncluding(root / "src/transaction.svh") ==
          std::vector<fs::path>{root / "device_pkg.sv"});
    CHECK(server.m_indexer.getFilesIncluding(root / "src/fields.svh") ==
          std::vector<fs::path>{root / "src/transaction.svh"});
    CHECK(server.m_indexer.getFilesIncluding(root / "device_pkg.sv").empty());
}

TEST_CASE("Includer lookup resolves repeated filenames separately for each source directory") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_repeated_includes_{}", slang::OS::getpid());
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    for (const auto* directory : {"a/src", "a/inc", "b/src", "b/inc", "local", "shared"})
        fs::create_directories(root / directory);
    std::vector<fs::path> sources{root / "a/src/first.sv", root / "a/src/second.sv",
                                  root / "b/src/source.sv", root / "local/source.sv"};
    for (const auto& source : sources)
        std::ofstream(source) << "`include \"types.svh\"\n`include \"./types.svh\"\n"
                                 "`include \"unique.svh\"\n";
    for (const auto* header :
         {"a/inc/types.svh", "b/inc/types.svh", "local/types.svh", "shared/unique.svh"})
        std::ofstream(root / header) << "typedef int value_t;\n";

    Indexer indexer;
    std::vector<std::string> paths;
    for (const auto* directory : {"a/src", "a/inc", "b/src", "b/inc", "local", "shared"})
        paths.push_back((root / directory / "*.sv*").string());
    for (bool reverse : {false, true}) {
        CAPTURE(reverse);
        if (reverse)
            std::ranges::reverse(paths);
        indexer.startIndexing(paths, {});
        auto firstParents = indexer.getFilesIncluding(root / "a/inc/types.svh");
        std::ranges::sort(firstParents);
        CHECK(firstParents == std::vector<fs::path>{sources[0], sources[1]});
        CHECK(indexer.getFilesIncluding(root / "b/inc/types.svh") ==
              std::vector<fs::path>{sources[2]});
        CHECK(indexer.getFilesIncluding(root / "local/types.svh") ==
              std::vector<fs::path>{sources[3]});
        auto sharedParents = indexer.getFilesIncluding(root / "shared/unique.svh");
        std::ranges::sort(sharedParents);
        CHECK(sharedParents == sources);
    }
}

TEST_CASE("Includer lookup resolves only the requested gathered filename") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_lazy_includers_{}", slang::OS::getpid());
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    for (const auto* directory : {"first", "second", "src"})
        fs::create_directories(root / directory);
    for (const auto* directory : {"first", "second"})
        std::ofstream(root / directory / "types.svh") << "typedef int value_t;\n";
    auto alias = root / "headers";
    fs::create_directory_symlink(root / "first", alias);
    auto source = root / "src/source.sv";
    auto ready = root / "src/ready.svh";
    auto late = root / "src/late.svh";
    std::ofstream(ready) << "typedef int ready_t;\n";
    std::ofstream(source) << "`include \"ready.svh\"\n`include \"late.svh\"\n"
                             "`include \"types.svh\"\n";
    Indexer indexer;
    indexer.startIndexing(
        std::vector<Config::IndexConfig>{{.dirs = std::vector<std::string>{"headers", "src"}}},
        root.string());
    CHECK(indexer.getFilesIncluding(ready) == std::vector<fs::path>{source});

    std::ofstream(late) << "typedef int late_t;\n";
    fs::remove(alias);
    fs::create_directory_symlink(root / "second", alias);
    CHECK(indexer.getFilesIncluding(late) == std::vector<fs::path>{source});
    CHECK(indexer.getFilesIncluding(root / "first/types.svh").empty());
    CHECK(indexer.getFilesIncluding(root / "second/types.svh") == std::vector<fs::path>{source});

    indexer.startIndexing(std::vector<std::string>{ready.string()}, {});
    CHECK(indexer.getFilesIncluding(ready).empty());
    CHECK(indexer.getFilesIncluding(late).empty());
    CHECK(indexer.getFilesIncluding(root / "second/types.svh").empty());
}

TEST_CASE("Parsed include results override gathered filename candidates") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_parsed_includers_{}", slang::OS::getpid());
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    for (const auto* directory : {"a", "b", "src"})
        fs::create_directories(root / directory);
    for (const auto* directory : {"a", "b"})
        std::ofstream(root / directory / "types.svh") << "typedef int value_t;\n";
    auto source = root / "src/source.sv";
    std::ofstream(source) << "`include \"../a/types.svh\"\n";
    Indexer indexer;
    indexer.startIndexing(std::vector<Config::IndexConfig>{}, root.string());
    CHECK(indexer.getFilesIncluding(root / "a/types.svh") == std::vector<fs::path>{source});
    CHECK(indexer.getFilesIncluding(root / "b/types.svh").empty());

    slang::SourceManager sm;
    auto buffer = sm.assignText(source.string(), "`include \"../b/types.svh\"\n");
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, sm);
    REQUIRE(tree->diagnostics().empty());
    indexer.updateIncludes(*tree);
    CHECK(indexer.getFilesIncluding(root / "a/types.svh").empty());
    CHECK(indexer.getFilesIncluding(root / "b/types.svh") == std::vector<fs::path>{source});

    slang::SourceManager inactive;
    auto inactiveBuffer = inactive.assignText(
        source.string(), "`ifdef DISABLED\n`include \"../a/types.svh\"\n`endif\n");
    auto inactiveTree = slang::syntax::SyntaxTree::fromBuffer(inactiveBuffer, inactive);
    REQUIRE(inactiveTree->diagnostics().empty());
    indexer.updateIncludes(*inactiveTree);
    CHECK(indexer.getFilesIncluding(root / "a/types.svh").empty());
    CHECK(indexer.getFilesIncluding(root / "b/types.svh").empty());
}

TEST_CASE("Include lookup caches refresh after reindexing and file changes") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_include_cache_refresh_{}", slang::OS::getpid());
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    for (const auto* directory : {"first", "second", "src"})
        fs::create_directories(root / directory);
    for (const auto* directory : {"first", "second"})
        std::ofstream(root / directory / "types.svh") << "typedef int value_t;\n";
    auto source = root / "src/source.sv";
    std::ofstream(source) << "`include \"types.svh\"\n`include \"late.svh\"\n";
    auto alias = root / "headers";
    fs::create_directory_symlink(root / "first", alias);
    std::vector<Config::IndexConfig> configs{{.dirs = std::vector<std::string>{"headers", "src"}}};
    Indexer indexer;
    indexer.startIndexing(configs, root.string());
    CHECK(indexer.getFilesIncluding(root / "first/types.svh") == std::vector<fs::path>{source});
    CHECK(indexer.getFilesIncluding(root / "src/late.svh").empty());

    fs::remove(alias);
    fs::create_directory_symlink(root / "second", alias);
    std::ofstream(root / "src/late.svh") << "typedef int late_t;\n";
    indexer.startIndexing(configs, root.string());
    CHECK(indexer.getFilesIncluding(root / "first/types.svh").empty());
    CHECK(indexer.getFilesIncluding(root / "second/types.svh") == std::vector<fs::path>{source});
    CHECK(indexer.getFilesIncluding(root / "src/late.svh") == std::vector<fs::path>{source});

    auto local = root / "src/types.svh";
    std::ofstream(local) << "typedef int local_t;\n";
    fs::remove(root / "src/late.svh");
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(source), lsp::FileChangeType::Changed}}});
    CHECK(indexer.getFilesIncluding(root / "second/types.svh").empty());
    CHECK(indexer.getFilesIncluding(root / "src/late.svh").empty());
    CHECK(indexer.getFilesIncluding(local) == std::vector<fs::path>{source});
    fs::remove(local);
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(source), lsp::FileChangeType::Changed}}});
    CHECK(indexer.getFilesIncluding(local).empty());
    CHECK(indexer.getFilesIncluding(root / "second/types.svh") == std::vector<fs::path>{source});
}

TEST_CASE("File notifications update include relationships") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_include_updates_{}", slang::OS::getpid());
    fs::create_directories(root / "inc");
    fs::create_directories(root / "extra");
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    auto source = root / "source.sv";
    auto first = root / "inc/first.svh";
    auto second = root / "inc/second.svh";
    auto extra = root / "extra/new.svh";
    std::ofstream(source) << "`include \"first.svh\"\n";
    std::ofstream(first) << "typedef int first_t;\n";
    std::ofstream(second) << "typedef int second_t;\n";
    std::ofstream(extra) << "typedef int new_t;\n";
    Indexer indexer;
    indexer.startIndexing(std::vector<std::string>{source.string(), first.string(), second.string(),
                                                   extra.string()},
                          {});
    CHECK(indexer.getFilesIncluding(first) == std::vector<fs::path>{source});

    std::ofstream(source) << "`include \"second.svh\"\n";
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(source), lsp::FileChangeType::Changed}}});
    CHECK(indexer.getFilesIncluding(first).empty());
    CHECK(indexer.getFilesIncluding(second) == std::vector<fs::path>{source});

    std::ofstream(source) << "`include \"new.svh\"\n";
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(source), lsp::FileChangeType::Changed}}});
    CHECK(indexer.getFilesIncluding(second).empty());
    CHECK(indexer.getFilesIncluding(extra) == std::vector<fs::path>{source});

    std::ofstream(source) << "`include \"extra/new.svh\"\n";
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(source), lsp::FileChangeType::Changed}}});
    CHECK(indexer.getFilesIncluding(extra) == std::vector<fs::path>{source});

    auto added = root / "added.sv";
    std::ofstream(added) << "`include \"second.svh\"\n";
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(added), lsp::FileChangeType::Created}}});
    CHECK(indexer.getFilesIncluding(second) == std::vector<fs::path>{added});
    CHECK(indexer.getFilesForInclude("added.sv").empty());

    auto renamed = root / "renamed.sv";
    fs::rename(added, renamed);
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(added), lsp::FileChangeType::Deleted},
                     {URI::fromFile(renamed), lsp::FileChangeType::Created}}});
    CHECK(indexer.getFilesIncluding(second) == std::vector<fs::path>{renamed});
    fs::remove(renamed);
    indexer.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(renamed), lsp::FileChangeType::Deleted}}});
    CHECK(indexer.getFilesIncluding(second).empty());
}

TEST_CASE("Index distinguishes package-relative includes from search-root-relative includes") {
    auto root = findSlangRoot() / "tests/data/relative_includes";
    Indexer indexer;
    std::vector<std::string> paths{(root / "device_pkg.sv").string(),
                                   (root / "src/types.svh").string(),
                                   (root / "src/macros.svh").string()};
    indexer.startIndexing(paths, {});
    CHECK(indexer.getFilesIncluding(root / "src/types.svh") ==
          std::vector<fs::path>{root / "device_pkg.sv"});
    CHECK(indexer.getFilesIncluding(root / "src/macros.svh") ==
          std::vector<fs::path>{root / "src/types.svh"});

    paths.push_back((root / "src/worker.sv").string());
    indexer.startIndexing(paths, {});
    auto parents = indexer.getFilesIncluding(root / "src/macros.svh");
    std::ranges::sort(parents);
    CHECK(parents == std::vector<fs::path>{root / "src/types.svh", root / "src/worker.sv"});
}

TEST_CASE("Package-relative includes resolve without inferred search directories") {
    ServerHarness server("header_context");
    auto header = server.openFile("src/driver.svh");
    auto package = server.openFile("device_pkg.sv");
    CHECK(header.doc->getCompilation() == package.doc->getCompilation());
    CHECK(header.getDiagnostics().empty());
    CHECK(package.getDiagnostics().empty());
}

TEST_CASE("Don't index nested modules (they're private)") {
    Indexer indexer;
    auto testPath = getTestDataPath();
    indexer.startIndexing({testPath + "/nested.sv"}, {});

    // Should only have outer module, not inner (it's private)
    CHECK(indexer.getSymbolCount() == 1);

    // Check we have "outer"
    auto files = indexer.getFilesForSymbol("outer");
    CHECK(files.size() == 1);

    // Verify inner module is not in the index
    files = indexer.getFilesForSymbol("inner");
    CHECK(files.empty());
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
    files = indexer.getFilesForSymbol("m");
    CHECK(files.size() == 1);
}

TEST_CASE("Index directory directly (no glob patterns)") {
    ServerHarness server("indexer_test");
    auto& indexer = server.m_indexer;

    // Should find all symbols in the directory
    auto files = indexer.getFilesForSymbol("m1");
    CHECK(files.size() == 1);

    files = indexer.getFilesForSymbol("m2");
    CHECK(files.size() == 1);

    files = indexer.getFilesForSymbol("outer");
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
    auto files = indexer.getFilesForSymbol("TestModule");
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
    auto files = indexer.getFilesForSymbol("TestModule");
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
    auto files = indexer.getFilesForSymbol("OldModule");
    CHECK(files.size() == 1);

    // Modify the document to have a different module
    auto text = doc.getText();
    auto pos = text.find("OldModule");
    doc.erase(pos, pos + 9);
    doc.insert(pos, "NewModule");
    doc.publishChanges();
    doc.save();

    // Old module should be removed from index
    files = indexer.getFilesForSymbol("OldModule");
    CHECK(files.empty());

    // New module should be in index
    files = indexer.getFilesForSymbol("NewModule");
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
    auto files = indexer.getFilesForSymbol("TestModule");
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
    auto files = indexer.getFilesForSymbol("Module1");
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
    files = indexer.getFilesForSymbol("Module1");
    CHECK(files.size() == 1);

    files = indexer.getFilesForSymbol("Module2");
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
    auto files = indexer.getFilesForSymbol("Module1");
    CHECK(files.size() == 1);
    files = indexer.getFilesForSymbol("Module2");
    CHECK(files.size() == 1);

    // Remove Module2
    auto text = doc.getText();
    auto pos = text.find("module Module2");
    auto endPos = text.find("endmodule", pos) + 9;
    doc.erase(pos, endPos + 1); // +1 for newline
    doc.publishChanges();
    doc.save();

    // Module1 should still be there
    files = indexer.getFilesForSymbol("Module1");
    CHECK(files.size() == 1);

    // Module2 should be removed
    files = indexer.getFilesForSymbol("Module2");
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
