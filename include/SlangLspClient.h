//------------------------------------------------------------------------------
// SlangLspClient.h
// LSP client implementation for Slang.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include "lsp/LspClient.h"
#include "lsp/LspTypeExtensions.h"
#include <algorithm>
#include <optional>
#include <rfl/from_generic.hpp>
#include <rfl/to_generic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class SlangLspClient : public lsp::LspClient {
public:
    enum class InteractionSource {
        editor,
        codeLensSelect,
        codeLensGotoInstantiation,
        hierarchy,
        terminal,
        waveform,
        waveformNetlist,
    };

    struct ActivateInstanceParams {
        std::string hierPath;
        InteractionSource interactionSource;
    };

    struct QuickPickItem {
        std::string label;
        std::optional<std::string> description;
        lsp::LSPAny value;
    };

    struct QuickPickParams {
        std::string placeholder;
        std::vector<QuickPickItem> items;
        /// The selected item's value is passed as this command's sole argument.
        std::string onSelectCommand;
        /// Clients with a hierarchy UI route the selection through that UI using this source.
        std::optional<InteractionSource> interactionSource;
    };

    struct Capabilities {
        /// Experimental slang-server inactive-regions extension.
        bool inactiveRegionsSupported = false;

        /// [`textDocument.definition.linkSupport`](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#definitionClientCapabilities)
        bool definitionLinksSupported = false;

        /// [`textDocument.completion.completionItem.resolveSupport`](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#completionClientCapabilities)
        bool completionEditResolveSupported = false;

        Capabilities() = default;

        explicit Capabilities(const lsp::ClientCapabilities& capabilities) {
            const auto& textDocument = capabilities.textDocument;
            definitionLinksSupported = textDocument && textDocument->definition &&
                                       textDocument->definition->linkSupport.value_or(false);

            if (textDocument && textDocument->completion &&
                textDocument->completion->completionItem &&
                textDocument->completion->completionItem->resolveSupport) {
                const auto& properties =
                    textDocument->completion->completionItem->resolveSupport->properties;
                auto supports = [&](std::string_view property) {
                    return std::ranges::find(properties, property) != properties.end();
                };
                completionEditResolveSupported = supports("insertText") &&
                                                 supports("insertTextFormat") &&
                                                 supports("textEdit");
            }

            if (!capabilities.experimental)
                return;

            auto experimental = rfl::from_generic<lsp::ExperimentalClientCapabilities>(
                *capabilities.experimental);
            inactiveRegionsSupported = experimental && experimental->inactiveRegions &&
                                       experimental->inactiveRegions->inactiveRegions.value_or(
                                           false);
        }
    } capabilities;

private:
    /// Pack arbitrary values into the LSP `arguments` array via rfl serialization.
    template<typename... Args>
    static std::vector<lsp::LSPAny> passArgs(Args&&... args) {
        return {rfl::to_generic(std::forward<Args>(args))...};
    }

public:
    virtual void setConfig(const Config& params) {
        lsp::sendNotification("slang/setConfig", rfl::to_generic(params));
    }

    std::monostate getClientRegisterCapability(const lsp::RegistrationParams& params) override {
        lsp::sendRequest("client/registerCapability",
                         rfl::to_generic<rfl::UnderlyingEnums>(params));
        return std::monostate{};
    }

    virtual void onActiveInstanceChanged(const ActivateInstanceParams& params) {
        lsp::sendNotification("slang/activeInstanceChanged", rfl::to_generic(params));
    }

    lsp::Command makeActivateInstanceCommand(std::string title, std::string tooltip,
                                             std::string_view instance,
                                             InteractionSource source) const {
        return lsp::Command{
            .title = std::move(title),
            .tooltip = std::move(tooltip),
            .command = "slang.activateInstance",
            .arguments = std::vector<lsp::LSPAny>{rfl::to_generic<rfl::UnderlyingEnums>(
                ActivateInstanceParams{.hierPath = std::string(instance),
                                       .interactionSource = source})},
        };
    }

    lsp::Command makeQuickPickCommand(std::string title, std::string tooltip,
                                      std::string placeholder, std::string_view activeValue,
                                      const std::vector<std::string>& values,
                                      std::string onSelectCommand,
                                      std::optional<InteractionSource> interactionSource) const {
        std::vector<QuickPickItem> items;
        items.reserve(values.size());
        for (const auto& value : values) {
            items.push_back(QuickPickItem{
                .label = value,
                .description = value == activeValue ? std::optional<std::string>("(current)")
                                                    : std::nullopt,
                .value = interactionSource
                             ? rfl::to_generic<rfl::UnderlyingEnums>(ActivateInstanceParams{
                                   .hierPath = value,
                                   .interactionSource = *interactionSource,
                               })
                             : rfl::to_generic(value),
            });
        }
        return lsp::Command{
            .title = std::move(title),
            .tooltip = std::move(tooltip),
            .command = "slang.quickPick",
            .arguments = passArgs(QuickPickParams{
                .placeholder = std::move(placeholder),
                .items = std::move(items),
                .onSelectCommand = std::move(onSelectCommand),
                .interactionSource = interactionSource,
            }),
        };
    }
};
