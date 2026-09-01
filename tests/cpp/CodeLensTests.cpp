// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "SlangLspClient.h"
#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <ranges>
#include <vector>

using namespace server;

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
        CHECK(lens.command->command == "slang.quickPick");
        REQUIRE(lens.command->arguments);
        REQUIRE(lens.command->arguments->size() == 1);
        auto params = rfl::from_generic<SlangLspClient::QuickPickParams>(
            lens.command->arguments->at(0));
        REQUIRE(params);
        CHECK(params->onSelectCommand == "slang.activateInstance");
        CHECK(params->interactionSource == SlangLspClient::InteractionSource::codeLensSelect);
        CHECK(params->placeholder == "Select active instance for cpu");
        REQUIRE_FALSE(params->items.empty());
        auto selected =
            rfl::from_generic<SlangLspClient::ActivateInstanceParams, rfl::UnderlyingEnums>(
                params->items[0].value);
        REQUIRE(selected);
        CHECK(selected->hierPath == activeInstance->instPath);
        CHECK(selected->interactionSource == SlangLspClient::InteractionSource::codeLensSelect);
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

TEST_CASE("ActiveInstanceAtPositionTracksIndependentGenerateScopes") {
    ServerHarness server("active_instance_hover");
    server.setBuildFile("design.f");
    REQUIRE(server.setActiveInstance("top.leaf16.lanes[1]"));
    REQUIRE(server.setActiveInstance("top.leaf16.channels[2]"));

    auto hdl = server.openFile("leaf.sv");
    auto getActivePath = [&](const Cursor& cursor) {
        return server.getActiveInstanceAtPosition({
            .moduleName = "leaf",
            .textDocument = {.uri = hdl.m_uri},
            .position = cursor.getPosition(),
        });
    };

    auto lane = getActivePath(hdl.before("lane();"));
    REQUIRE(lane);
    CHECK(*lane == "top.leaf16.lanes[1].lane");

    auto channel = getActivePath(hdl.before("channel();"));
    REQUIRE(channel);
    CHECK(*channel == "top.leaf16.channels[2].channel");
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
