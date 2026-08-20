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
#include <rfl/from_generic.hpp>
#include <string_view>

class SlangLspClient : public lsp::LspClient {
    /// Helper functions to send things to the client

public:
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

    virtual void setConfig(const Config& params) {
        lsp::sendNotification("slang/setConfig", rfl::to_generic(params));
    }

    std::monostate getClientRegisterCapability(const lsp::RegistrationParams& params) override {
        lsp::sendRequest("client/registerCapability",
                         rfl::to_generic<rfl::UnderlyingEnums>(params));
        return std::monostate{};
    }
};
