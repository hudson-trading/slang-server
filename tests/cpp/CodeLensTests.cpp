// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "SlangLspClient.h"
#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <ranges>
#include <vector>

#include "slang/util/OS.h"
#include "slang/util/ScopeGuard.h"

using namespace server;

TEST_CASE("CodeLensNestedIncludesRecognizeDirectoryAliases") {
    const bool openAlias = GENERATE(false, true);
    const bool indexBothPaths = GENERATE(false, true);
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_nested_include_alias_{}", slang::OS::getpid());
    auto physical = root / "physical";
    auto alias = root / "alias";
    fs::create_directories(physical / "base");
    slang::ScopeGuard cleanup([&] {
        std::error_code ec;
        fs::remove_all(root, ec);
    });
    std::error_code ec;
    fs::create_directory_symlink(physical, alias, ec);
    if (ec)
        SKIP("Directory symlinks unavailable: " + ec.message());

    std::ofstream(physical / "library_pkg.sv") << R"(package library_pkg;
    typedef logic [7:0] data_t;
    `include "base/all.svh"
endpackage
)";
    std::ofstream(physical / "base/all.svh") << "`include \"base/item.svh\"\n";
    std::ofstream(physical / "base/item.svh") << "class item; data_t value; endclass\n";

    ServerHarness server;
    Config config;
    Config::IndexConfig indexConfig;
    indexConfig.dirs = std::vector<std::string>{alias.string()};
    if (indexBothPaths)
        indexConfig.dirs.value().push_back(physical.string());
    config.index.value().push_back(std::move(indexConfig));
    config.incdirs = std::vector<std::string>{alias.string()};
    server.loadConfig(config, true);

    CHECK(server.m_indexer.getFilesIncluding(physical / "base/item.svh") ==
          std::vector<fs::path>{physical / "base/all.svh"});
    CHECK(server.m_indexer.getFilesIncluding(alias / "base/item.svh") ==
          std::vector<fs::path>{physical / "base/all.svh"});

    auto header = server.openFile(((openAlias ? alias : physical) / "base/item.svh").string());
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by all.svh:1");
    REQUIRE(lenses->front().command->arguments);
    server.executeCommand({.command = lenses->front().command->command,
                           .arguments = lenses->front().command->arguments});
    REQUIRE(server.client.m_showDocuments.size() == 1);
    CHECK(server.client.m_showDocuments.front().uri == URI::fromFile(physical / "base/all.svh"));

    auto package = server.m_driver->getDocument(URI::fromFile(physical / "library_pkg.sv"));
    CHECK(header.doc->getCompilation() == package->getCompilation());
    CHECK(header.getDiagnostics().empty());
    CHECK(header.before("data_t value").hasDefinition());
}

TEST_CASE("Missing headers do not resolve contexts from matching filenames") {
    ServerHarness server("header_context");
    auto missing = fs::current_path() / "missing/shared.svh";
    CHECK(server.m_driver->getIncludeContexts(missing).empty());
    CHECK_FALSE(server.m_driver->setIncludeContext(
        {.uri = URI::fromFile(missing),
         .source = URI::fromFile(fs::current_path() / "first_pkg.sv")}));
}

TEST_CASE("CodeLensIncludedHeaderNavigatesToIncludeDirective") {
    ServerHarness server("header_context");
    auto header = server.openFile("src/transaction.svh");
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    const auto& lens = lenses->front();
    CHECK(lens.range == lsp::Range{{0, 0}, {0, 0}});
    REQUIRE(lens.command);
    CHECK(lens.command->title == "Included by device_pkg.sv:7");
    CHECK(lens.command->command == "slang.showLocation");
    REQUIRE(lens.command->arguments);
    auto package = server.openFile("device_pkg.sv");

    server.executeCommand({.command = lens.command->command, .arguments = lens.command->arguments});
    REQUIRE(server.client.m_showDocuments.size() == 1);
    const auto& shown = server.client.m_showDocuments.front();
    CHECK(shown.uri == package.m_uri);
    CHECK(shown.takeFocus == true);
    REQUIRE(shown.selection);
    CHECK(shown.selection->start ==
          package.before("`include \"src/transaction.svh\"").getPosition());
    CHECK(shown.selection->end == package.after("`include \"src/transaction.svh\"").getPosition());
}

TEST_CASE("CodeLensNestedHeaderPointsToImmediateIncluder") {
    ServerHarness server("header_context");
    auto header = server.openFile("src/fields.svh");
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by transaction.svh:5");
    REQUIRE(lenses->front().command->arguments);
    server.executeCommand({.command = lenses->front().command->command,
                           .arguments = lenses->front().command->arguments});
    REQUIRE(server.client.m_showDocuments.size() == 1);
    CHECK(server.client.m_showDocuments.front().uri ==
          URI::fromFile(fs::current_path() / "src/transaction.svh"));
    REQUIRE(server.client.m_showDocuments.front().selection);
    CHECK(server.client.m_showDocuments.front().selection->start == lsp::Position{4, 4});
}

TEST_CASE("CodeLensHeaderSelectsAnotherSharedCompilation") {
    ServerHarness server("header_context");
    auto header = server.openFile("shared.svh");
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 2);
    REQUIRE((*lenses)[0].command);
    REQUIRE((*lenses)[1].command);
    CHECK((*lenses)[0].command->title == "Included by first_pkg.sv:5");
    CHECK((*lenses)[1].command->title == "And 1 other");
    CHECK((*lenses)[1].command->command == "slang.quickPick");
    for (const auto& lens : *lenses)
        CHECK(lens.range == lsp::Range{{0, 0}, {0, 0}});
    REQUIRE((*lenses)[1].command->arguments);
    auto pick = rfl::from_generic<SlangLspClient::QuickPickParams>(
        (*lenses)[1].command->arguments->front());
    REQUIRE(pick);
    REQUIRE(pick->items.size() == 2);
    CHECK(pick->onSelectCommand == "slang.setIncludeContext");
    CHECK_FALSE(pick->interactionSource);
    CHECK(pick->items.front().description == "first_pkg.sv (current)");
    auto result = server.executeCommand(
        {.command = pick->onSelectCommand, .arguments = {{pick->items.back().value}}});
    REQUIRE(result);
    CHECK(rfl::from_generic<bool>(*result).value());
    CHECK(server.client.m_showDocuments.empty());
    auto second = server.openFile("second_pkg.sv");
    CHECK(header.doc->getCompilation() == second.doc->getCompilation());

    lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 2);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by second_pkg.sv:5");
    server.executeCommand({.command = lenses->front().command->command,
                           .arguments = lenses->front().command->arguments});
    REQUIRE(server.client.m_showDocuments.size() == 1);
    CHECK(server.client.m_showDocuments.front().uri == second.m_uri);
}

TEST_CASE("CodeLensIncludePickerOffersEveryContextUsingTwoLenses") {
    const bool sameParent = GENERATE(false, true);
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_include_lens_limit_{}_{}", slang::OS::getpid(), sameParent);
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    const std::string headerText = "typedef int value_t;\n";
    std::ofstream(root / "shared.svh") << headerText;
    for (int i = sameParent ? 1 : 12; i-- > 0;) {
        auto source = std::ofstream(root / fmt::format("source_{:02}.sv", i));
        for (int j = 0; j < (sameParent ? 12 : 1); ++j)
            source << fmt::format("package p_{}_{}; `include \"shared.svh\"\nendpackage\n", i, j);
    }

    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "lens_limit"}}}});
    auto header = server.openFile("shared.svh", headerText);
    auto previousBuffer = header.doc->getBuffer();
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 2);
    REQUIRE((*lenses)[0].command);
    REQUIRE((*lenses)[1].command);
    CHECK((*lenses)[0].command->title == "Included by source_00.sv:1");
    CHECK((*lenses)[1].command->title == "And 11 others");
    REQUIRE((*lenses)[1].command->arguments);
    auto pick = rfl::from_generic<SlangLspClient::QuickPickParams>(
        (*lenses)[1].command->arguments->front());
    REQUIRE(pick);
    REQUIRE(pick->items.size() == 12);
    server.executeCommand(
        {.command = pick->onSelectCommand, .arguments = {{pick->items.back().value}}});
    CHECK(header.doc->getBuffer() != previousBuffer);
    lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title ==
          fmt::format("Included by source_{:02}.sv:{}", sameParent ? 0 : 11, sameParent ? 23 : 1));
}

TEST_CASE("CodeLensIncludeLocationsFollowParentEdits") {
    ServerHarness server("header_context");
    auto header = server.openFile("src/driver.svh");
    auto package = server.openFile("device_pkg.sv");
    package.begin().write("\n");
    package.publishChanges();
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);
    REQUIRE(lenses->front().command);
    CHECK(lenses->front().command->title == "Included by device_pkg.sv:9");

    package.replaceAll("package device_pkg; endpackage\n");
    package.publishChanges();
    lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    CHECK((!lenses || lenses->empty()));
}

TEST_CASE("CodeLensUnincludedHeaderHasNoIncludeLens") {
    ServerHarness server;
    auto header = server.openFile("standalone.svh", "typedef int value_t;\n");
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    CHECK((!lenses || lenses->empty()));
}

TEST_CASE("Macro definition headers remain standalone without include context lenses") {
    const bool guarded = GENERATE(false, true);
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_macro_header_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    const std::string macroText = guarded ? "`ifndef DEFS_SVH\n`define DEFS_SVH\n"
                                            "`define VALUE 1\n`endif\n"
                                          : "`define VALUE 1\n";
    std::ofstream(root / "defs.svh") << macroText;
    std::ofstream(root / "source.sv") << "`define WIDTH 8\npackage source;\n"
                                         "`include \"defs.svh\"\nendpackage\n";
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "macro_header"}}}});
    auto header = server.openFile("defs.svh");
    auto source = server.openFile("source.sv");
    auto checkStandalone = [&] {
        CHECK(header.doc->getCompilation() != source.doc->getCompilation());
        CHECK(header.doc->getSyntaxTree()->getSourceBufferIds().front() == header.doc->getBuffer());
        auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
        CHECK((!lenses || lenses->empty()));
        CHECK_FALSE(
            server.m_driver->setIncludeContext({.uri = header.m_uri, .source = source.m_uri}));
    };
    checkStandalone();

    header.append("typedef logic [`WIDTH-1:0] data_t;\n");
    header.publishChanges();
    CHECK(header.doc->getCompilation() == source.doc->getCompilation());
    CHECK(header.getDiagnostics().empty());
    auto lenses = server.getDocCodeLens({.textDocument = {header.m_uri}});
    REQUIRE(lenses);
    REQUIRE(lenses->size() == 1);

    header.replaceAll(macroText);
    header.publishChanges();
    checkStandalone();
}

TEST_CASE("Suffix context selection updates symbols diagnostics and survives reloads") {
    const bool nested = GENERATE(false, true);
    auto root = fs::weakly_canonical(fs::temp_directory_path()) /
                fmt::format("slang_suffix_context_{}", slang::OS::getpid());
    fs::create_directories(root);
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    const std::string suffixText = "localparam int total = BASE + 1;\n"
                                   "typedef logic [`WIDTH-1:0] data_t;\n"
                                   "`ifdef EXTRA\nlocalparam int invalid = absent;\n`endif\n";
    std::ofstream(root / "suffix.svh") << suffixText;
    if (nested)
        std::ofstream(root / "list.svh") << "`include \"suffix.svh\"\n";
    auto include = nested ? "list.svh" : "suffix.svh";
    for (auto name : {"alpha", "beta"}) {
        std::ofstream(root / fmt::format("{}.sv", name))
            << (std::string_view(name) == "alpha" ? "`define WIDTH 8\n"
                                                  : "`define WIDTH 16\n`define EXTRA\n")
            << fmt::format("package {};\nlocalparam int BASE = 1;\n`include \"{}\"\nendpackage\n",
                           name, include);
    }
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "suffix_context"}}}});
    auto header = server.openFile("suffix.svh");
    auto alpha = server.openFile("alpha.sv");
    auto beta = server.openFile("beta.sv");
    auto checkDefinition = [&](const URI& expected) {
        auto definitions = header.before("BASE +").getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetUri == expected);
    };
    CHECK(header.doc->getCompilation() == alpha.doc->getCompilation());
    CHECK(header.getDiagnostics().empty());
    checkDefinition(alpha.m_uri);

    auto selectedFile = nested ? URI::fromFile(root / "list.svh") : header.m_uri;
    REQUIRE(server.m_driver->setIncludeContext({.uri = selectedFile, .source = beta.m_uri}));
    CHECK(header.doc->getCompilation() == beta.doc->getCompilation());
    CHECK_FALSE(header.getDiagnostics().empty());
    checkDefinition(beta.m_uri);
    CHECK(server.client.m_showDocuments.empty());
    auto contexts = server.m_driver->getIncludeContexts(root / "suffix.svh");
    REQUIRE(contexts.size() == 2);
    CHECK(contexts.front().source->getURI() == beta.m_uri);
    CHECK(contexts.front().location.uri == (nested ? selectedFile : beta.m_uri));

    beta.begin().write("\n");
    beta.publishChanges();
    header.append("// edited suffix\n");
    header.publishChanges();
    checkDefinition(beta.m_uri);
    CHECK_FALSE(header.getDiagnostics().empty());
    auto config = server.getConfig();
    server.loadConfig(config, true);
    header.doc = server.getDoc(header.m_uri);
    beta.doc = server.getDoc(beta.m_uri);
    CHECK(header.doc->getCompilation() == beta.doc->getCompilation());
    checkDefinition(beta.m_uri);

    beta.replaceAll("package beta; endpackage\n");
    beta.publishChanges();
    CHECK(header.doc->getCompilation() == server.getDoc(alpha.m_uri)->getCompilation());
    checkDefinition(alpha.m_uri);
    CHECK_FALSE(server.m_driver->setIncludeContext({.uri = selectedFile, .source = beta.m_uri}));
}

TEST_CASE("CodeLensModuleActiveInstance") {
    ServerHarness server("comp_repo");
    server.setBuildFile("cpu_design.f");

    auto hdl = server.openFile("cpu.sv");
    auto activeInstance = server.getActiveInstance("cpu");
    REQUIRE(activeInstance);

    auto lenses = server.getDocCodeLens(lsp::CodeLensParams{
        .textDocument = lsp::TextDocumentIdentifier{.uri = hdl.m_uri},
    });
    REQUIRE(lenses);

    auto expectedTitle = activeInstance->instPath + " (1)";
    bool found = false;
    for (const auto& lens : *lenses) {
        if (!lens.command || lens.command->title != expectedTitle) {
            continue;
        }

        CHECK(lens.command->title == expectedTitle);
        CHECK(lens.command->command == "slang.showInHierarchy");
        REQUIRE(lens.command->arguments);
        REQUIRE(lens.command->arguments->size() == 1);
        auto params =
            rfl::from_generic<SlangLspClient::ActivateInstanceParams, rfl::UnderlyingEnums>(
                lens.command->arguments->at(0));
        REQUIRE(params);
        CHECK(params->interactionSource == SlangLspClient::InteractionSource::codeLensSelect);
        CHECK(params->hierPath == activeInstance->instPath);
        found = true;
        break;
    }

    CHECK(found);
}

TEST_CASE("CodeLensModuleDefaultsToFirstInstance") {
    ServerHarness server("active_instance_hover");
    server.setBuildFile("design.f");

    auto hdl = server.openFile("leaf.sv");

    auto lenses = server.getDocCodeLens(lsp::CodeLensParams{
        .textDocument = lsp::TextDocumentIdentifier{.uri = hdl.m_uri},
    });
    REQUIRE(lenses);

    bool found = false;
    for (const auto& lens : *lenses) {
        if (!lens.command || lens.command->title != "top.leaf8 (2)") {
            continue;
        }

        CHECK(lens.command->title == "top.leaf8 (2)");
        CHECK(lens.command->command == "slang.quickPick");
        REQUIRE(lens.command->arguments);
        REQUIRE(lens.command->arguments->size() == 1);
        auto params = rfl::from_generic<SlangLspClient::QuickPickParams>(
            lens.command->arguments->at(0));
        REQUIRE(params);
        CHECK(params->onSelectCommand == "slang.activateInstance");
        CHECK(params->placeholder == "Select active instance for leaf");
        REQUIRE(params->items.size() == 2);
        CHECK(params->items[0].label == "top.leaf8");
        CHECK(params->items[0].description == "(current)");
        CHECK(params->items[1].label == "top.leaf16");
        CHECK_FALSE(params->items[1].description);
        found = true;
        break;
    }

    CHECK(found);
}

TEST_CASE("CodeLensModuleGoToInstantiation") {
    ServerHarness server("active_instance_hover");
    server.setBuildFile("design.f");

    auto hdl = server.openFile("leaf.sv");

    auto lenses = server.getDocCodeLens(lsp::CodeLensParams{
        .textDocument = lsp::TextDocumentIdentifier{.uri = hdl.m_uri},
    });
    REQUIRE(lenses);

    bool found = false;
    for (const auto& lens : *lenses) {
        if (!lens.command || lens.command->title != "Go to Instantiation") {
            continue;
        }

        CHECK(lens.command->command == "slang.activateInstance");
        REQUIRE(lens.command->arguments);
        REQUIRE(lens.command->arguments->size() == 1);
        auto params =
            rfl::from_generic<SlangLspClient::ActivateInstanceParams, rfl::UnderlyingEnums>(
                lens.command->arguments->at(0));
        REQUIRE(params);
        CHECK(params->hierPath == "top.leaf8");
        CHECK(params->interactionSource ==
              SlangLspClient::InteractionSource::codeLensGotoInstantiation);
        found = true;
        break;
    }

    CHECK(found);
}

TEST_CASE("CodeLensInterfaceSelectionUpdatesGoToInstantiation") {
    ServerHarness server("active_interface_codelens");
    server.setBuildFile("design.f");

    auto hdl = server.openFile("bus.sv");
    auto getLenses = [&] {
        auto lenses = server.getDocCodeLens(lsp::CodeLensParams{
            .textDocument = lsp::TextDocumentIdentifier{.uri = hdl.m_uri},
        });
        REQUIRE(lenses);
        return *lenses;
    };

    auto lenses = getLenses();
    auto selectLens = std::ranges::find_if(lenses, [](const auto& lens) {
        return lens.command && lens.command->command == "slang.quickPick";
    });
    REQUIRE(selectLens != lenses.end());
    REQUIRE(selectLens->command->arguments);
    auto quickPick = rfl::from_generic<SlangLspClient::QuickPickParams>(
        selectLens->command->arguments->at(0));
    REQUIRE(quickPick);

    auto selected = std::ranges::find(quickPick->items, "top.holder.selected",
                                      &SlangLspClient::QuickPickItem::label);
    REQUIRE(selected != quickPick->items.end());
    auto activated = server.executeCommand({
        .command = quickPick->onSelectCommand,
        .arguments = std::vector<lsp::LSPAny>{selected->value},
    });
    REQUIRE(activated);
    auto activationResult = rfl::from_generic<bool>(*activated);
    REQUIRE(activationResult);
    CHECK(*activationResult);

    auto active = server.getActiveInstance("StreamBus");
    REQUIRE(active);
    CHECK(active->instPath == "top.holder.selected");

    lenses = getLenses();
    auto gotoLens = std::ranges::find_if(lenses, [](const auto& lens) {
        return lens.command && lens.command->title == "Go to Instantiation";
    });
    REQUIRE(gotoLens != lenses.end());
    REQUIRE(gotoLens->command->arguments);
    auto gotoParams =
        rfl::from_generic<SlangLspClient::ActivateInstanceParams, rfl::UnderlyingEnums>(
            gotoLens->command->arguments->at(0));
    REQUIRE(gotoParams);
    CHECK(gotoParams->hierPath == "top.holder.selected");
}

TEST_CASE("ActiveInstanceTracksGenerateScope") {
    ServerHarness server("active_instance_hover");
    server.setBuildFile("design.f");

    REQUIRE(server.setActiveInstance("top.leaf16.lanes[1].lane"));

    auto generatedLeaf = server.getActiveInstance("generated_leaf");
    REQUIRE(generatedLeaf);
    CHECK(generatedLeaf->instPath == "top.leaf16.lanes[1].lane");

    auto leaf = server.getActiveInstance("leaf");
    REQUIRE(leaf);
    CHECK(leaf->instPath == "top.leaf16");

    REQUIRE(server.setActiveInstance("top.leaf8.lanes[0]"));
    leaf = server.getActiveInstance("leaf");
    REQUIRE(leaf);
    CHECK(leaf->instPath == "top.leaf8");
}

TEST_CASE("CodeLensGenerateLoopActiveIteration") {
    ServerHarness server("active_instance_hover");
    server.setBuildFile("design.f");
    REQUIRE(server.setActiveInstance("top.leaf16.lanes[1]"));
    REQUIRE(server.setActiveInstance("top.leaf16.channels[2]"));

    auto hdl = server.openFile("leaf.sv");
    auto lenses = server.getDocCodeLens(lsp::CodeLensParams{
        .textDocument = lsp::TextDocumentIdentifier{.uri = hdl.m_uri},
    });
    REQUIRE(lenses);

    auto lens = std::ranges::find_if(*lenses, [](const auto& candidate) {
        return candidate.command && candidate.command->title == "top.leaf16.lanes[1]";
    });
    REQUIRE(lens != lenses->end());
    CHECK(lens->command->title == "top.leaf16.lanes[1]");
    REQUIRE(lens->command->arguments);
    REQUIRE(lens->command->arguments->size() == 1);

    auto params = rfl::from_generic<SlangLspClient::QuickPickParams>(
        lens->command->arguments->at(0));
    REQUIRE(params);
    CHECK(params->onSelectCommand == "slang.activateInstance");
    CHECK(params->interactionSource == SlangLspClient::InteractionSource::codeLensSelect);
    REQUIRE(params->items.size() == 2);
    CHECK(params->items[0].label == "top.leaf16.lanes[0]");
    CHECK_FALSE(params->items[0].description);
    CHECK(params->items[1].label == "top.leaf16.lanes[1]");
    CHECK(params->items[1].description == "(current)");
    auto selectedValue =
        rfl::from_generic<SlangLspClient::ActivateInstanceParams, rfl::UnderlyingEnums>(
            params->items[1].value);
    REQUIRE(selectedValue);
    CHECK(selectedValue->hierPath == "top.leaf16.lanes[1]");
    CHECK(selectedValue->interactionSource == SlangLspClient::InteractionSource::codeLensSelect);

    auto siblingLens = std::ranges::find_if(*lenses, [](const auto& candidate) {
        return candidate.command && candidate.command->title == "top.leaf16.channels[2]";
    });
    REQUIRE(siblingLens != lenses->end());
    CHECK(siblingLens->command->command == "slang.quickPick");
}
