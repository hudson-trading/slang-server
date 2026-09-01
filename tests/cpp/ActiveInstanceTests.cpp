// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

TEST_CASE("EditingDocumentsPreservesActiveInstanceHover") {
    ServerHarness server("active_interface_port_hover");
    server.setBuildFile("design.f");

    auto doc = server.openFile("logger.sv");
    auto interfaceDoc = server.openFile("valid_data.sv");
    auto valueParameter = doc.before("width,");
    auto typeParameter = doc.before("data_t\n");
    auto signal = doc.before("typed_data;");
    auto interfaceWidth = doc.after("data_bus.");
    auto interfacePort = doc.before("data_bus\n");
    auto interfaceValueParameter = interfaceDoc.before("width = 1");
    auto interfaceTypeParameter = interfaceDoc.before("payload_t = logic");
    auto getHoverContent = [&](const Cursor& cursor) {
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        return rfl::get<lsp::MarkupContent>(hover->contents).value;
    };
    auto getInterfaceHoverContent = [&](const Cursor& cursor) {
        auto hover = interfaceDoc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        return rfl::get<lsp::MarkupContent>(hover->contents).value;
    };

    REQUIRE(server.setActiveInstance("top.tap8"));
    doc.append("\n");
    doc.publishChanges();

    CHECK(getHoverContent(valueParameter).find("Value: `8`") != std::string::npos);
    CHECK(getHoverContent(typeParameter).find("Value: `byte`") != std::string::npos);
    auto signalHover = getHoverContent(signal);
    CHECK(signalHover.find("Width: `8`") != std::string::npos);
    CHECK(signalHover.find("Driven by continuous assignment") != std::string::npos);
    CHECK(getHoverContent(interfaceWidth).find("Value: `8`") != std::string::npos);
    CHECK(getHoverContent(interfacePort).find("Connected to [`top.bus8`](<file:") !=
          std::string::npos);

    auto signalInfo = doc.getDefinitionInfoAt(signal.m_offset);
    REQUIRE(signalInfo);
    auto* signalTarget = std::get_if<server::DefinitionInfo::SymbolTarget>(
        &signalInfo->primaryTarget());
    REQUIRE(signalTarget);
    CHECK(signalTarget->symbol->getHierarchicalPath() == "top.tap8.typed_data");

    REQUIRE(server.setActiveInstance("top.wrapper8.tap.data_bus"));
    auto forwardedPortHover = getHoverContent(interfacePort);
    auto connectionSummary = forwardedPortHover.find("Connected to ");
    REQUIRE(connectionSummary != std::string::npos);
    CHECK(forwardedPortHover.find("Connected to ", connectionSummary + 1) == std::string::npos);

    interfaceDoc.append("\n");
    interfaceDoc.publishChanges();

    CHECK(getInterfaceHoverContent(interfaceValueParameter).find("Value: `8`") !=
          std::string::npos);
    CHECK(getInterfaceHoverContent(interfaceTypeParameter).find("Value: `byte`") !=
          std::string::npos);
}

TEST_CASE("GotoDefinitionSelectsReferencedActiveInstance") {
    ServerHarness server("active_interface_port_hover");
    server.setBuildFile("design.f");

    auto declarations = server.openFile("logger.sv");
    auto top = server.openFile("top.sv");
    auto moduleDeclaration = declarations.after("module ");
    auto parameterDeclaration = declarations.after("parameter int ");
    auto portDeclaration = declarations.before("data_in,");

    auto checkGoto = [&](Cursor reference, const Cursor& declaration,
                         std::optional<std::string_view> expectedDesignPath = std::nullopt) {
        REQUIRE(server.setActiveInstance("top.tap16"));

        auto definitions = reference.getDefinitions();
        CHECK(std::ranges::any_of(definitions, [&](const auto& definition) {
            return definition.targetUri == declaration.getUri() &&
                   definition.targetSelectionRange.start == declaration.getPosition();
        }));

        auto active = server.getActiveInstance("StageTap");
        REQUIRE(active);
        CHECK(active->instPath == "top.tap8");

        if (expectedDesignPath) {
            auto info = reference.m_doc.getDefinitionInfoAt(reference.m_offset);
            REQUIRE(info);
            REQUIRE(info->symbol());
            CHECK(info->symbol()->getHierarchicalPath() == *expectedDesignPath);
        }
    };

    auto tap8Module = top.before("StageTap #(.width(8)");
    checkGoto(tap8Module, moduleDeclaration);
    checkGoto(tap8Module.after("#(."), parameterDeclaration, "top.tap8.width");
    checkGoto(top.after("tap8(\n        ."), portDeclaration, "top.tap8.data_in");
}

TEST_CASE("ActivateInstanceCommandNotifiesClientAndOnlyGotoOpensEditor") {
    ServerHarness server("active_instance_hover");
    server.setBuildFile("design.f");

    auto activate = [&](std::string hierPath, SlangLspClient::InteractionSource source) {
        auto params = SlangLspClient::ActivateInstanceParams{
            .hierPath = std::move(hierPath),
            .interactionSource = source,
        };
        auto result = server.executeCommand({
            .command = "slang.activateInstance",
            .arguments = std::vector<lsp::LSPAny>{rfl::to_generic<rfl::UnderlyingEnums>(params)},
        });
        REQUIRE(result);
        auto activated = rfl::from_generic<bool>(*result);
        REQUIRE(activated);
        CHECK(*activated);
        return params;
    };

    auto selected = activate("top.leaf16", SlangLspClient::InteractionSource::codeLensSelect);
    auto active = server.getActiveInstance("leaf");
    REQUIRE(active);
    CHECK(active->instPath == "top.leaf16");
    REQUIRE(server.client.m_activeInstanceChanges.size() == 1);
    CHECK(server.client.m_activeInstanceChanges.front().hierPath == selected.hierPath);
    CHECK(server.client.m_activeInstanceChanges.front().interactionSource ==
          selected.interactionSource);
    CHECK(server.client.m_showDocuments.empty());

    auto opened = activate("top.leaf8",
                           SlangLspClient::InteractionSource::codeLensGotoInstantiation);
    active = server.getActiveInstance("leaf");
    REQUIRE(active);
    CHECK(active->instPath == "top.leaf8");
    REQUIRE(server.client.m_activeInstanceChanges.size() == 2);
    CHECK(server.client.m_activeInstanceChanges.back().hierPath == opened.hierPath);
    CHECK(server.client.m_activeInstanceChanges.back().interactionSource ==
          opened.interactionSource);
    REQUIRE(server.client.m_showDocuments.size() == 1);
    CHECK(server.client.m_showDocuments.front().takeFocus);
}
