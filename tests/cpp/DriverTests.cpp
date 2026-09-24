// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "ast/ShallowCompilation.h"
#include "utils/ServerHarness.h"
#include <catch2/generators/catch_generators.hpp>
#include <cstdlib>
#include <fstream>
#include <functional>

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/parsing/ParserMetadata.h"
#include "slang/util/OS.h"
#include "slang/util/ScopeGuard.h"

TEST_CASE("Shallow file views share analysis and retain compilation state independently") {
    slang::SourceManager sm;
    // Include lookup canonicalizes paths, so assigned buffers need absolute cache keys.
    auto root = fs::current_path();
    sm.assignText((root / "signal.svh").string(), "assign value = 1'b1;\n");
    auto buffer = sm.assignText((root / "top.sv").string(), R"(module top(output logic value);
`include "signal.svh"
endmodule
)");
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, sm);
    REQUIRE(tree->diagnostics().empty());
    REQUIRE(tree->getIncludeDirectives().size() == 1);
    auto headerBuffer = tree->getIncludeDirectives().front().buffer.id;

    auto compilation = std::make_shared<server::ShallowCompilation>(
        sm, tree, slang::Bag{}, std::vector<std::shared_ptr<slang::syntax::SyntaxTree>>{}, nullptr);
    auto source = std::make_shared<server::ShallowAnalysis>(buffer.id, compilation);
    auto header = std::make_shared<server::ShallowAnalysis>(headerBuffer, compilation);
    auto location = slang::SourceLocation(headerBuffer, 7);
    auto* symbol = header->getSymbolAt(location);
    REQUIRE(symbol);
    auto* value = symbol->as_if<slang::ast::ValueSymbol>();
    REQUIRE(value);
    CHECK(header->getSemanticDiagnostics().empty());
    CHECK(&header->getSemanticDiagnostics() == &source->getSemanticDiagnostics());
    auto* manager = header->getAnalysisManager();
    REQUIRE(manager);
    CHECK(manager == source->getAnalysisManager());
    CHECK(header->getDrivers(*value).size() == 1);

    source.reset();
    compilation.reset();
    tree.reset();
    CHECK(header->getSymbolAt(location) == symbol);
    CHECK(header->getAnalysisManager() == manager);
    CHECK(header->getDrivers(*value).size() == 1);
}

TEST_CASE("Shared shallow analysis localizes drivers for repeated included instances") {
    slang::SourceManager sm;
    auto root = fs::current_path();
    sm.assignText((root / "leaf.svh").string(), R"(module leaf;
    logic value;
    assign value = 1'b1;
endmodule
)");
    auto buffer = sm.assignText((root / "top.sv").string(), R"(`include "leaf.svh"
module top;
    leaf first();
    leaf second();
endmodule
)");
    auto tree = slang::syntax::SyntaxTree::fromBuffer(buffer, sm);
    REQUIRE(tree->diagnostics().empty());
    REQUIRE(tree->getIncludeDirectives().size() == 1);
    auto compilation = std::make_shared<server::ShallowCompilation>(
        sm, tree, slang::Bag{}, std::vector<std::shared_ptr<slang::syntax::SyntaxTree>>{}, nullptr);
    server::ShallowAnalysis source(buffer.id, compilation);
    server::ShallowAnalysis header(tree->getIncludeDirectives().front().buffer.id, compilation);
    REQUIRE(source.getSemanticDiagnostics().empty());
    auto instances = compilation->getCompilation()->getRoot().topInstances;
    REQUIRE(instances.size() == 1);
    auto& second = instances.front()->body.find<slang::ast::InstanceSymbol>("second");
    REQUIRE(second.getCanonicalBody());
    auto& value = second.body.find<slang::ast::VariableSymbol>("value");
    auto drivers = source.getDrivers(value);
    REQUIRE(drivers.size() == 1);
    CHECK(&drivers.front()->getSymbol() == &value);
    CHECK(drivers.front()->containingSymbol->getParentScope() == &second.body);
    CHECK(header.getDrivers(value) == drivers);
    CHECK(source.getDrivers(value) == drivers);
}

TEST_CASE("Parser metadata records virtual interface dependencies") {
    auto tree = slang::syntax::SyntaxTree::fromText(R"(
class client;
    local virtual field_if vif;
    typedef virtual interface alias_if alias_t;
    extern function void set_vif(virtual argument_if bus);
    virtual interface parameter_if #(types_pkg::WIDTH).monitor parameterized;
endclass
)");
    REQUIRE(tree->diagnostics().empty());
    for (const auto& metadata :
         {tree->getMetadata(), slang::parsing::ParserMetadata::fromSyntax(tree->root())}) {
        auto expected = std::vector<std::string_view>{"alias_if", "argument_if", "field_if",
                                                      "parameter_if", "types_pkg"};
        auto names = metadata.getReferencedSymbols();
        std::ranges::sort(names);
        CHECK(names == expected);
        names.clear();
        metadata.visitReferencedSymbols([&](auto name) { names.push_back(name); }, false);
        std::ranges::sort(names);
        CHECK(names == expected);
    }
}

TEST_CASE("Expand macros preserves the selected file and uses its include context") {
    auto crlf = GENERATE(false, true);
    CAPTURE(crlf);
    auto withLineEndings = [=](std::string_view text) {
        std::string result;
        for (char c : text) {
            if (crlf && c == '\n')
                result += '\r';
            result += c;
        }
        return result;
    };
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_expand_file_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    const std::string sourceText = "`define VALUE_TYPE int\n"
                                   "`define MEMBER(n) `VALUE_TYPE n;\n"
                                   "`define EMPTY\n"
                                   "package outer_pkg;\n`include \"item.svh\"\nendpackage\n"
                                   "`undef VALUE_TYPE\n`define VALUE_TYPE bit\n";
    const std::string headerText = "// Leading comment\nclass item;\n"
                                   "    `MEMBER(first)\n"
                                   "    `include \"fields.svh\"\n"
                                   "    `EMPTY\nendclass\n// Trailing comment\n";
    const std::string fieldsText = "`MEMBER(second)\n";
    const std::string standaloneText = "`define ID(x) x\n`define VALUE 42\n`define EMPTY\n"
                                       "module top(output int value);\n"
                                       "    assign value = `ID(`VALUE);\n`EMPTY\nendmodule\n";
    std::ofstream(root / "source.sv", std::ios::binary) << withLineEndings(sourceText);
    std::ofstream(root / "item.svh", std::ios::binary) << withLineEndings(headerText);
    std::ofstream(root / "fields.svh", std::ios::binary) << withLineEndings(fieldsText);
    std::ofstream(root / "standalone.sv", std::ios::binary) << withLineEndings(standaloneText);
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "expand_file"}}}});

    auto checkExpansion = [&](std::string_view fileName, std::string_view expected) {
        auto output = root / "expanded.txt";
        REQUIRE(server.expandMacros({.src = (root / fileName).string(), .dst = output.string()}));
        std::ifstream file(output, std::ios::binary);
        REQUIRE(file.is_open());
        CHECK(std::string(std::istreambuf_iterator<char>(file), {}) == withLineEndings(expected));
    };
    checkExpansion("item.svh", "// Leading comment\nclass item;\n"
                               "    int first;\n"
                               "    `include \"fields.svh\"\n"
                               "    \nendclass\n// Trailing comment\n");
    checkExpansion("fields.svh", "int second;\n");
    checkExpansion("source.sv", sourceText);
    checkExpansion("standalone.sv", "`define ID(x) x\n`define VALUE 42\n`define EMPTY\n"
                                    "module top(output int value);\n"
                                    "    assign value = 42;\n\nendmodule\n");
}

TEST_CASE("Shallow compilation loads virtual interfaces from included class fields and arguments") {
    ServerHarness server("virtual_interface");
    auto header = server.openFile("src/client.svh");
    auto package = server.openFile("client_pkg.sv");
    CHECK(header.doc->getCompilation() == package.doc->getCompilation());
    CHECK(header.getDiagnostics().empty());
    CHECK(package.getDiagnostics().empty());
    const auto& compilation = header.doc->getCompilation();
    CHECK(compilation->tryGetDefinition("bus_if", compilation->getRoot()).definition);
    CHECK(compilation->getPackage("defs_pkg"));
    CHECK(server.m_driver->docs.contains(URI::fromFile(fs::current_path() / "defs_pkg.sv")));
}

TEST_CASE("Shallow compilation loads virtual interfaces referenced only in function arguments") {
    ServerHarness server("virtual_interface");
    auto type = GENERATE("virtual bus_if", "virtual interface bus_if",
                         "virtual bus_if #(4).monitor");
    auto doc = server.openFile("reader.sv", fmt::format(R"(
class reader;
    function int read({} bus);
        return bus.value;
    endfunction
endclass
)",
                                                        type));
    CHECK(doc.getDiagnostics().empty());
    const auto& compilation = doc.doc->getCompilation();
    CHECK(compilation->tryGetDefinition("bus_if", compilation->getRoot()).definition);
    CHECK(compilation->getPackage("defs_pkg"));
}

TEST_CASE("Included headers share their package compilation with per-file indexes") {
    ServerHarness server("header_context");
    auto driver = server.openFile("src/driver.svh");
    auto originalCompilation = driver.doc->getAnalysis()->getShallowCompilation();
    auto transaction = server.openFile("src/transaction.svh");
    CHECK(driver.doc->getAnalysis()->getShallowCompilation() == originalCompilation);
    auto package = server.openFile("device_pkg.sv");
    auto fields = server.openFile("src/fields.svh");
    CHECK(driver.doc->getCompilation() == package.doc->getCompilation());
    CHECK(transaction.doc->getCompilation() == package.doc->getCompilation());
    CHECK(fields.doc->getCompilation() == package.doc->getCompilation());
    CHECK(driver.doc->getAnalysis() != transaction.doc->getAnalysis());
    CHECK(driver.doc->getSyntaxTree() == package.doc->getSyntaxTree());
    CHECK(driver.getDiagnostics().empty());
    CHECK(transaction.getDiagnostics().empty());
    CHECK(driver.before("transaction current").hasDefinition());
    auto definitions = driver.before("transaction current").getDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions[0].targetUri == transaction.m_uri);
    CHECK(driver.getHoverAt(driver.before("payload_t read").m_offset));
    CHECK(driver.before("payload_t read").hasDefinition());
    CHECK(fields.before("payload_t payload").hasDefinition());

    auto symbols = driver.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "driver");
    symbols = transaction.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    CHECK(symbols[0].name == "transaction");
    symbols = fields.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0].children);
    REQUIRE(symbols[0].children->size() == 1);
    CHECK(symbols[0].children->front().name == "payload");

    for (const auto* token : driver.doc->getAnalysis()->syntaxes.collected)
        CHECK(token->location().buffer() == driver.doc->getBuffer());
    for (const auto* token : transaction.doc->getAnalysis()->syntaxes.collected)
        CHECK(token->location().buffer() == transaction.doc->getBuffer());
}

TEST_CASE("Top-level class files share package context through sv include lists") {
    ServerHarness server("class_include_context");
    std::ifstream listFile("src/sequence_list.sv");
    REQUIRE(listFile);
    auto list = server.openFile("src/sequence_list.sv",
                                std::string(std::istreambuf_iterator<char>(listFile), {}));
    auto fields = server.openFile("src/fields.svh");
    auto sequence = server.openFile("src/base_sequence.sv");
    auto package = server.openFile("device_pkg.sv");
    CHECK(fields.doc->getCompilation() == package.doc->getCompilation());
    CHECK(sequence.doc->getCompilation() == package.doc->getCompilation());
    CHECK(sequence.doc->getSyntaxTree() == package.doc->getSyntaxTree());
    CHECK(list.doc->getCompilation() == package.doc->getCompilation());
    CHECK(list.doc->getSyntaxTree() == package.doc->getSyntaxTree());
    CHECK(list.getDiagnostics().empty());
    CHECK(list.getSymbolTree().empty());
    CHECK(fields.getDiagnostics().empty());
    CHECK(sequence.getDiagnostics().empty());
    auto definitions = sequence.before("payload_t read").getDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front().targetUri == package.m_uri);
    CHECK(fields.before("payload_t payload").hasDefinition());
    auto symbols = sequence.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    CHECK(symbols.front().name == "base_sequence");
    auto lenses = server.getDocCodeLens({.textDocument = {sequence.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by sequence_list.sv:4");

    for (auto* file : {&sequence, &list}) {
        file->append("// saved edit\n");
        file->save();
        CHECK(file->doc->getCompilation() == package.doc->getCompilation());
        CHECK(file->getDiagnostics().empty());
    }

    lenses = server.getDocCodeLens({.textDocument = {list.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by device_pkg.sv:7");
    REQUIRE(lenses->front().command->arguments);
    server.executeCommand({.command = lenses->front().command->command,
                           .arguments = lenses->front().command->arguments});
    REQUIRE(server.client.m_showDocuments.size() == 1);
    const auto& shown = server.client.m_showDocuments.front();
    CHECK(shown.uri == package.m_uri);
    REQUIRE(shown.selection);
    CHECK(shown.selection->start ==
          package.before("`include \"src/sequence_list.sv\"").getPosition());

    auto other = server.openFile("other_pkg.sv", "package other_pkg;\n"
                                                 "`include \"src/sequence_list.sv\"\nendpackage\n");
    CHECK(sequence.doc->getCompilation() == package.doc->getCompilation());
    CHECK(sequence.doc->getCompilation() != other.doc->getCompilation());
    CHECK(list.doc->getCompilation() == package.doc->getCompilation());
    CHECK(list.doc->getCompilation() != other.doc->getCompilation());
}

TEST_CASE("Include context follows relationships regardless of file extension or declarations") {
    auto extension = GENERATE("sv", "v", "inc");
    auto text = GENERATE("typedef `VALUE_TYPE value_t;\n",
                         "function `VALUE_TYPE get_value(); return '0; endfunction\n",
                         "module unit(output `VALUE_TYPE value); assign value = '0; endmodule\n",
                         "package types; typedef `VALUE_TYPE value_t; endpackage\n");
    CAPTURE(extension, text);
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_any_include_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    auto fileName = fmt::format("included.{}", extension);
    const std::string prefix = "`define VALUE_TYPE logic [7:0]\n";
    auto sourceText = prefix + fmt::format("`include \"{}\"\n", fileName);
    std::ofstream(root / fileName) << text;
    std::ofstream(root / "source.sv") << sourceText;
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "any_include"}}}});
    auto included = server.openFile(fileName, text);
    auto source = server.openFile("source.sv", sourceText);
    CHECK(included.doc->getCompilation() == source.doc->getCompilation());
    CHECK(included.getDiagnostics().empty());
    auto lenses = server.getDocCodeLens({.textDocument = {included.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by source.sv:2");

    included.append("// saved edit\n");
    included.save();
    CHECK(included.doc->getCompilation() == source.doc->getCompilation());
    CHECK(included.getDiagnostics().empty());

    auto other = server.openFile("other.sv", sourceText);
    CHECK(included.doc->getCompilation() != source.doc->getCompilation());
    CHECK(included.doc->getCompilation() == other.doc->getCompilation());
    lenses = server.getDocCodeLens({.textDocument = {included.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 2);
    REQUIRE((*lenses)[0].command);
    REQUIRE((*lenses)[1].command);
    CHECK((*lenses)[0].command->title == "Included by other.sv:2");
    CHECK((*lenses)[1].command->title == "And 1 other");

    source.replaceAll(prefix);
    source.publishChanges();
    CHECK(included.doc->getCompilation() == other.doc->getCompilation());
    CHECK(included.getDiagnostics().empty());
    other.replaceAll(prefix);
    other.publishChanges();
    CHECK(included.doc->getSyntaxTree()->getSourceBufferIds().front() == included.doc->getBuffer());
    lenses = server.getDocCodeLens({.textDocument = {included.m_uri}});
    CHECK((!lenses || lenses->empty()));
}

TEST_CASE("Included module references survive instantiation in a sibling include") {
    auto dimension = GENERATE("", "[2]");
    CAPTURE(dimension);
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_included_module_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    const std::string leafText =
        "module leaf(output logic value); assign value = 1'b1; endmodule\n";
    std::ofstream(root / "leaf.sv") << leafText;
    std::ofstream(root / "parent.sv")
        << fmt::format("module parent; leaf child{}(); endmodule\n", dimension);
    const std::string sourceText = "`include \"leaf.sv\"\n`include \"parent.sv\"\n";
    std::ofstream(root / "source.sv") << sourceText;
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "included_module"}}}});
    auto leaf = server.openFile("leaf.sv", leafText);
    auto source = server.openFile("source.sv", sourceText);
    CHECK(leaf.doc->getCompilation() == source.doc->getCompilation());
    CHECK(leaf.getDiagnostics().empty());
    CHECK(leaf.before("value =").hasDefinition());
    auto analysis = leaf.doc->getAnalysis();
    auto location = leaf.doc->getLocation(leaf.before("value =").getPosition());
    REQUIRE(location);
    auto* symbol = analysis->getSymbolAt(*location);
    REQUIRE(symbol);
    auto* value = symbol->as_if<slang::ast::ValueSymbol>();
    REQUIRE(value);
    CHECK(analysis->getDrivers(*value).size() == 1);
    auto references = server.getDocReferences({
        .context = {.includeDeclaration = true},
        .textDocument = {leaf.m_uri},
        .position = leaf.before("value)").getPosition(),
    });
    REQUIRE(references);
    CHECK(references->size() == 2);
}

TEST_CASE("Headers with multiple includers default to the first source") {
    ServerHarness server("header_context");
    auto header = server.openFile("shared.svh");
    auto first = server.openFile("first_pkg.sv");
    auto second = server.openFile("second_pkg.sv");
    CHECK(header.doc->getCompilation() == first.doc->getCompilation());
    CHECK(header.doc->getCompilation() != second.doc->getCompilation());
    CHECK(server.m_indexer.getFilesIncluding(fs::current_path() / "shared.svh").size() == 2);
}

TEST_CASE("Included declarations disabled by parent macros are not loaded as dependencies") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_disabled_dependency_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    const std::string headerText =
        "`ifndef SKIP\npackage p; typedef int value_t; endpackage\n`endif\n";
    const std::string sourceText =
        "`define SKIP\n`include \"header.svh\"\n"
        "module top(output p::value_t value); assign value = 0; endmodule\n";
    std::ofstream(root / "header.svh") << headerText;
    std::ofstream(root / "top.sv") << sourceText;
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "disabled_dependency"}}}});
    if (GENERATE(false, true))
        server.openFile("header.svh", headerText);
    auto top = server.openFile("top.sv", sourceText);
    CHECK_FALSE(top.getDiagnostics().empty());
    CHECK_FALSE(top.doc->getCompilation()->getPackage("p"));
    CHECK(server.m_driver->getDependentTrees(top.doc->getSyntaxTree()).empty());

    top.replaceAll(sourceText.substr(sourceText.find('\n') + 1));
    top.publishChanges();
    CHECK(top.getDiagnostics().empty());
    CHECK(top.doc->getCompilation()->getPackage("p"));
    auto header = server.openFile("header.svh", headerText);
    CHECK(header.doc->getCompilation() == top.doc->getCompilation());
}

TEST_CASE("Included header stops sharing when its include becomes inactive") {
    ServerHarness server("header_context");
    auto header = server.openFile("src/driver.svh");
    auto package = server.openFile("device_pkg.sv");
    CHECK(header.doc->getCompilation() == package.doc->getCompilation());
    package.replaceAll("package device_pkg; endpackage\n");
    package.publishChanges();
    CHECK(header.doc->getCompilation() != package.doc->getCompilation());
    CHECK(header.doc->getSyntaxTree()->getSourceBufferIds()[0] == header.doc->getBuffer());
}

TEST_CASE("Editing and saving includers updates shared header context") {
    ServerHarness server("header_context");
    auto first = server.openFile("first_pkg.sv");
    auto second = server.openFile("second_pkg.sv");
    auto header = server.openFile("shared.svh");
    auto path = fs::current_path() / "shared.svh";
    auto directories = server.m_indexer.getIncludeDirectories();
    CHECK(header.doc->getCompilation() != second.doc->getCompilation());

    first.replaceAll("package first_pkg; endpackage\n");
    first.publishChanges();
    CHECK(server.m_indexer.getFilesIncluding(path) ==
          std::vector<fs::path>{fs::current_path() / "second_pkg.sv"});
    CHECK(header.doc->getCompilation() == second.doc->getCompilation());
    second.save();
    CHECK(server.m_indexer.getFilesIncluding(path) ==
          std::vector<fs::path>{fs::current_path() / "second_pkg.sv"});

    auto third = server.openFile("third_pkg.sv",
                                 "package third_pkg;\n`include \"shared.svh\"\nendpackage\n");
    CHECK(server.m_indexer.getFilesIncluding(path).size() == 2);
    CHECK(header.doc->getCompilation() == second.doc->getCompilation());
    third.save();
    CHECK(server.m_indexer.getFilesIncluding(path).size() == 2);

    second.replaceAll("package second_pkg; endpackage\n");
    second.save();
    CHECK(server.m_indexer.getFilesIncluding(path) ==
          std::vector<fs::path>{fs::current_path() / "third_pkg.sv"});
    CHECK(header.doc->getCompilation() == third.doc->getCompilation());
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by third_pkg.sv:2");
    CHECK(server.m_indexer.getIncludeDirectories() == directories);
}

TEST_CASE("Deleted and renamed includers do not prevent opening their headers") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_missing_includer_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    auto source = root / "source.sv";
    auto headerPath = root / "header.svh";
    std::ofstream(source) << "package source;\n`include \"header.svh\"\nendpackage\n";
    std::ofstream(headerPath) << "typedef int value_t;\n";
    ServerHarness server;
    server.m_indexer.startIndexing(std::vector<std::string>{source.string(), headerPath.string()},
                                   {});
    CHECK(server.m_indexer.getFilesIncluding(headerPath) == std::vector<fs::path>{source});

    bool renamed = false;
    SECTION("Deleted file notification") {
        fs::remove(source);
        server.onWorkspaceDidChangeWatchedFiles(
            {.changes = {{URI::fromFile(source), lsp::FileChangeType::Deleted}}});
        CHECK(server.m_indexer.getFilesIncluding(headerPath).empty());
    }
    SECTION("Deleted before the file notification arrives") {
        fs::remove(source);
    }
    SECTION("Renamed file notification") {
        auto newSource = root / "renamed.sv";
        fs::rename(source, newSource);
        server.onWorkspaceDidChangeWatchedFiles(
            {.changes = {{URI::fromFile(source), lsp::FileChangeType::Deleted},
                         {URI::fromFile(newSource), lsp::FileChangeType::Created}}});
        source = newSource;
        renamed = true;
        CHECK(server.m_indexer.getFilesIncluding(headerPath) == std::vector<fs::path>{source});
    }

    auto header = server.openFile(headerPath.string(), "typedef int value_t;\n");
    CHECK(header.getDiagnostics().empty());
    if (renamed) {
        auto parent = server.m_driver->getDocument(URI::fromFile(source));
        CHECK(header.doc->getCompilation() == parent->getCompilation());
    }
    else {
        CHECK(header.doc->getSyntaxTree()->getSourceBufferIds()[0] == header.doc->getBuffer());
    }
    CHECK(server.m_indexer.getIncludeDirectories().empty());
}

TEST_CASE("External includer edits refresh unopened cached context") {
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_cached_includer_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    auto source = root / "source.sv";
    auto firstPath = root / "first.svh";
    auto secondPath = root / "second.svh";
    std::ofstream(source) << "package source;\n`include \"first.svh\"\nendpackage\n";
    std::ofstream(firstPath) << "typedef int first_t;\n";
    std::ofstream(secondPath) << "typedef int second_t;\n";
    ServerHarness server;
    server.m_indexer.startIndexing(
        std::vector<std::string>{source.string(), firstPath.string(), secondPath.string()}, {});
    auto first = server.openFile(firstPath.string(), "typedef int first_t;\n");
    auto parent = server.m_driver->getDocument(URI::fromFile(source));
    REQUIRE(first.doc->getCompilation() == parent->getCompilation());
    CHECK_FALSE(server.m_driver->isDocumentOpen(URI::fromFile(source)));

    std::ofstream(source) << "package source;\n`include \"second.svh\"\nendpackage\n";
    server.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(source), lsp::FileChangeType::Changed}}});
    CHECK(server.m_indexer.getFilesIncluding(firstPath).empty());
    CHECK(first.doc->getCompilation() != parent->getCompilation());
    auto second = server.openFile(secondPath.string(), "typedef int second_t;\n");
    CHECK(second.doc->getCompilation() == parent->getCompilation());
    CHECK(server.m_indexer.getFilesIncluding(secondPath) == std::vector<fs::path>{source});
    CHECK(second.getDiagnostics().empty());
    CHECK(server.m_indexer.getIncludeDirectories().empty());
}

TEST_CASE("Editing an included header rebuilds the shared package analysis") {
    ServerHarness server("header_context");
    auto driver = server.openFile("src/driver.svh");
    auto transaction = server.openFile("src/transaction.svh");
    auto package = server.openFile("device_pkg.sv");
    std::weak_ptr<server::ShallowAnalysis> oldSourceAnalysis = package.doc->getAnalysis();
    auto oldAnalysis = driver.doc->getAnalysis();

    transaction.replaceAll("class transaction; payload_t renamed; endclass\n");
    transaction.publishChanges();
    auto newAnalysis = driver.doc->getAnalysis();
    CHECK(newAnalysis != oldAnalysis);
    CHECK(oldSourceAnalysis.expired());
    CHECK(newAnalysis->getShallowCompilation() != oldAnalysis->getShallowCompilation());
    CHECK_FALSE(oldAnalysis->hasValidBuffers());
    CHECK(newAnalysis->hasValidBuffers());
    CHECK(driver.doc->getCompilation() == transaction.doc->getCompilation());
    CHECK_FALSE(driver.before("payload;").hasDefinition());
    transaction.save();
    CHECK_FALSE(driver.getDiagnostics().empty());

    transaction.replaceAll("class transaction; payload_t payload; endclass\n");
    transaction.publishChanges();
    transaction.save();
    CHECK(driver.before("payload;").hasDefinition());
    CHECK(driver.getDiagnostics().empty());
    CHECK(server.m_indexer.getFilesForSymbol("device_pkg") ==
          std::vector<fs::path>{fs::current_path() / "device_pkg.sv"});
}

TEST_CASE("Normal indexing resolves headers without inferring workspace include directories") {
    ServerHarness server("include_index");
    CHECK(server.m_indexer.getIncludeDirectories().empty());
    CHECK(server.getConfig().incdirs.value().empty());
    CHECK_FALSE(server.sourceManager().readHeader("entry.svh", {}, nullptr, false, {}));
    auto doc = server.openFile("pkg.sv");
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
    CHECK_FALSE(server.sourceManager().readHeader("entry.svh", {}, nullptr, false, {}));
    CHECK(server.getConfig().incdirs.value().empty());
}

TEST_CASE("Configured incdirs resolve nested headers and dependent packages") {
    ServerHarness server("include_index");
    auto config = server.getConfig();
    config.incdirs = std::vector<std::string>{"headers", "types"};
    server.loadConfig(config);
    auto doc = server.openFile("top.sv", R"(module top(output pkg::value_t value);
    assign value = '0;
endmodule
)");
    CHECK(doc.getDiagnostics().empty());
    auto pkg = server.m_driver->getDocument(URI::fromFile(fs::current_path() / "pkg.sv"));
    REQUIRE(pkg);
    CHECK(pkg->getSyntaxTree()->diagnostics().empty());
    REQUIRE(pkg->getSyntaxTree()->getIncludeDirectives().size() == 3);
    for (const auto& include : pkg->getSyntaxTree()->getIncludeDirectives())
        CHECK(bool(include.buffer));
}

TEST_CASE("Configured incdirs use ordered lookup and leave missing headers unresolved") {
    ServerHarness server("include_index");
    auto config = server.getConfig();
    config.incdirs = std::vector<std::string>{"b", "a"};
    server.loadConfig(config);
    auto doc = server.openFile("top.sv", R"(`include "duplicate.svh"
`include "missing.svh"
module top; endmodule
)");
    auto tree = doc.doc->getSyntaxTree();
    REQUIRE(tree->getIncludeDirectives().size() == 2);
    REQUIRE(tree->getIncludeDirectives()[0].buffer);
    CHECK(server.sourceManager().getFullPath(tree->getIncludeDirectives()[0].buffer.id) ==
          fs::current_path() / "b/duplicate.svh");
    CHECK_FALSE(tree->getIncludeDirectives()[1].buffer);
    auto links = server.getDocDocumentLink({.textDocument = {doc.m_uri}});
    REQUIRE(links);
    REQUIRE(links->size() == 1);
    CHECK(links->front().target == URI::fromFile(fs::current_path() / "b/duplicate.svh"));
}

TEST_CASE("File edits do not add configured incdirs") {
    ServerHarness server("include_paths");
    auto config = server.getConfig();
    config.incdirs = std::vector<std::string>{"."};
    server.loadConfig(config);
    auto doc = server.openFile("design/top.sv");
    CHECK(doc.getDiagnostics().empty());
    doc.replaceAll("`include \"common.svh\"\nmodule top; endmodule\n");
    doc.save();
    auto tree = doc.doc->getSyntaxTree();
    REQUIRE(tree->getIncludeDirectives().size() == 1);
    REQUIRE(tree->getIncludeDirectives().front().buffer);
    CHECK(server.sourceManager().getFullPath(tree->getIncludeDirectives().front().buffer.id) ==
          fs::current_path() / "library/common/common.svh");
    CHECK_FALSE(server.sourceManager().readHeader("common.svh", {}, nullptr, false, {}));
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"."});
    doc.replaceAll("`include \"library/common/common.svh\"\nmodule top; endmodule\n");
    doc.publishChanges();
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
}

TEST_CASE("Reloading configuration replaces incdirs without accumulating paths") {
    ServerHarness server("include_index");
    auto config = server.getConfig();
    config.incdirs = std::vector<std::string>{"headers", "types"};
    server.loadConfig(config);
    REQUIRE(server.sourceManager().readHeader("entry.svh", {}, nullptr, false, {}));
    config.incdirs = std::vector<std::string>{"a"};
    server.loadConfig(config);
    CHECK_FALSE(server.sourceManager().readHeader("entry.svh", {}, nullptr, false, {}));
    CHECK(server.sourceManager().readHeader("only_a.svh", {}, nullptr, false, {}));
}

TEST_CASE("Configured incdirs support macro filenames but do not affect system includes") {
    ServerHarness server("include_index");
    auto config = server.getConfig();
    config.incdirs = std::vector<std::string>{"headers", "types"};
    server.loadConfig(config);
    auto doc = server.openFile("top.sv", R"(`define HEADER "entry.svh"
`include `HEADER
module top; endmodule
)");
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
    CHECK(doc.doc->getAnalysis()->macros.contains("VALUE_WIDTH"));
    CHECK_FALSE(server.sourceManager().readHeader("entry.svh", {}, nullptr, true, {}));
}

TEST_CASE("getAnalysis returns same object on repeated calls") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] data;
    logic clk;
endmodule
)");

    auto a1 = hdl.doc->getAnalysis();
    auto a2 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a2.get());

    // Third call should still return the same object
    auto a3 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a3.get());
}

TEST_CASE("getAnalysis returns new object after onChange") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] data;
endmodule
)");

    auto before = hdl.doc->getAnalysis();

    // Modify the document
    hdl.after("data;").write("\n    logic clk;");
    hdl.publishChanges();

    auto after = hdl.doc->getAnalysis();
    CHECK(before.get() != after.get());
}

TEST_CASE("onChange handles WholeDocument content change") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] foo;
endmodule
)");

    auto before = hdl.doc->getAnalysis();

    // Replace the entire document with a renamed symbol (the wire form Claude's
    // LSP client uses: { text } only, no range).
    std::string newText = R"(module test;
    logic [7:0] bar;
endmodule
)";
    hdl.replaceAll(newText);
    hdl.publishChanges();

    // Buffer was actually swapped (analysis invalidated)
    auto after = hdl.doc->getAnalysis();
    CHECK(before.get() != after.get());

    // Buffer text reflects the new contents (getText returns view including
    // the trailing null terminator, so size is +1).
    auto text = hdl.doc->getText();
    REQUIRE(text.size() == newText.size() + 1);
    CHECK(std::string_view{text.data(), newText.size()} == newText);

    // Follow-up symbol query reflects the renamed identifier
    auto syms = hdl.getSymbolTree();
    REQUIRE(!syms.empty());
    bool foundBar = false;
    bool foundFoo = false;
    std::function<void(const std::vector<lsp::DocumentSymbol>&)> walk =
        [&](const std::vector<lsp::DocumentSymbol>& nodes) {
            for (const auto& node : nodes) {
                if (node.name == "bar")
                    foundBar = true;
                if (node.name == "foo")
                    foundFoo = true;
                if (node.children.has_value())
                    walk(*node.children);
            }
        };
    walk(syms);
    CHECK(foundBar);
    CHECK_FALSE(foundFoo);
}

TEST_CASE("onChange handles mixed WholeDocument and Partial changes") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] foo;
endmodule
)");

    // First queue a WholeDocument change that resets the buffer
    std::string replacement = R"(module test;
    logic [7:0] bar;
endmodule
)";
    hdl.replaceAll(replacement);

    // Then queue a Partial change that depends on the post-replacement buffer:
    // append a new line at the end of the file (offset 0,0 -> append empty
    // wouldn't exercise it; instead insert at a position that requires the new
    // buffer's line offsets).
    auto insertOffset = replacement.size();
    auto insertPos = hdl.getPosition(insertOffset);
    std::string appended = "// trailing\n";
    hdl.pending_changes.push_back({lsp::TextDocumentContentChangePartial{
        .range = lsp::Range{insertPos, insertPos}, .text = appended}});

    // Apply both in a single didChange request
    server.onDocDidChange(lsp::DidChangeTextDocumentParams{
        .textDocument = lsp::VersionedTextDocumentIdentifier{.uri = hdl.doc->getURI()},
        .contentChanges = hdl.pending_changes});
    hdl.pending_changes.clear();

    std::string expected = replacement + appended;
    auto text = hdl.doc->getText();
    REQUIRE(text.size() == expected.size() + 1);
    CHECK(std::string_view{text.data(), expected.size()} == expected);
}

TEST_CASE("onChange uses the negotiated position encoding for successive edits") {
    lsp::InitializeParams params{};
    bool utf8Positions = false;
    SECTION("Default UTF-16") {
    }
    SECTION("Explicit UTF-16") {
        params.capabilities.general = lsp::GeneralClientCapabilities{
            .positionEncodings = std::vector<lsp::PositionEncodingKind>{"utf-16"}};
    }
    SECTION("UTF-8 offered after UTF-16") {
        params.capabilities.general = lsp::GeneralClientCapabilities{
            .positionEncodings = std::vector<lsp::PositionEncodingKind>{"utf-16", "utf-8"}};
        utf8Positions = true;
    }

    ClientHarness client;
    server::SlangServer server(client);
    auto result = server.getInitialize(params);
    CHECK(result.capabilities.positionEncoding.value_or("utf-16") ==
          (utf8Positions ? "utf-8" : "utf-16"));
    CHECK(client.capabilities.utf8Positions == utf8Positions);

    ServerHarness harness(params);
    std::string prefix = "// \xC3\xA4\xE2\x82\xAC\xF0\x90\x8D\x88";
    auto hdl = harness.openFile("test.sv", prefix + "x\r\nmodule m; endmodule\n");
    lsp::uint column = utf8Positions ? static_cast<lsp::uint>(prefix.size()) : 7;
    lsp::uint nextColumn = column + (utf8Positions ? 2 : 1);
    harness.onDocDidChange(lsp::DidChangeTextDocumentParams{
        .textDocument = lsp::VersionedTextDocumentIdentifier{.uri = hdl.doc->getURI()},
        .contentChanges = {lsp::TextDocumentContentChangePartial{
                               .range = {{0, column}, {0, column + 1}}, .text = "\xC3\xA4"},
                           lsp::TextDocumentContentChangePartial{
                               .range = {{0, nextColumn}, {0, nextColumn}}, .text = "y"}}});
    CHECK(hdl.doc->textMatches(prefix + "\xC3\xA4y\r\nmodule m; endmodule\n"));
}

TEST_CASE("Cancelled didChange applies edits without rebuilding analysis") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic foo;
endmodule
)");
    REQUIRE(hdl.doc->hasAnalysis());

    std::string replacement = R"(module test;
    logic bar;
endmodule
)";
    lsp::RequestContext ctx("textDocument/didChange", std::nullopt);
    ctx.cancel();
    CHECK_THROWS_WITH(
        server.onDocDidChange(
            lsp::DidChangeTextDocumentParams{
                .textDocument = lsp::VersionedTextDocumentIdentifier{.version = 2,
                                                                     .uri = hdl.doc->getURI()},
                .contentChanges = {lsp::TextDocumentContentChangeWholeDocument{replacement}}},
            ctx),
        "before analysis");

    CHECK(hdl.doc->textMatches(replacement));
    CHECK_FALSE(hdl.doc->hasAnalysis());
}

TEST_CASE("getAnalysis with cross-file dependencies is stable") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("crossfile_module.sv");
    hdl.ensureSynced();

    auto a1 = hdl.doc->getAnalysis();
    auto a2 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a2.get());
}

TEST_CASE("Document log paths are workspace relative") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("crossfile_module.sv");
    CHECK(hdl.doc->getWsRelativePath() == "crossfile_module.sv");
}

TEST_CASE("LoadConfig") {
    ServerHarness server("basic_config");
    auto flags = server.getConfig().flags.value();
    std::cerr << "Config flags: " << flags << '\n';

    CHECK(flags.size() > 0);

#if _WIN32
    server.client.expectWarning(
        "include directory 'some/include/path': The system cannot find the path specified.");
#else
    server.client.expectWarning("include directory 'some/include/path': No such file or directory");
#endif
}

TEST_CASE("CapturedDriverErrors") {
    ServerHarness server;
    Config cfg;
    cfg.flagsByFile.value().push_back({"test", "--std=invalid_standard"});
    server.loadConfig(cfg);
    server.expectError("invalid value for --std option");
    server.expectError("Failed to parse config flags");
}
