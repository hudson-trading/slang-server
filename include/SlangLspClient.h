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
#include <algorithm>

class SlangLspClient : public lsp::LspClient {
    /// Helper functions to send things to the client

public:
    struct {
        bool inactiveRegionsSupported = false;
    } experimentalCapabilities;

    bool supportsCompletionEditResolve(const lsp::ClientCapabilities& capabilities) const {
        if (!capabilities.textDocument || !capabilities.textDocument->completion ||
            !capabilities.textDocument->completion->completionItem ||
            !capabilities.textDocument->completion->completionItem->resolveSupport)
            return false;

        auto& properties =
            capabilities.textDocument->completion->completionItem->resolveSupport->properties;
        auto supports = [&](std::string_view property) {
            return std::ranges::find(properties, property) != properties.end();
        };
        return supports("insertText") && supports("insertTextFormat") && supports("textEdit");
    }

    virtual void setConfig(const Config& params) {
        lsp::sendNotification("slang/setConfig", rfl::to_generic(params));
    }

    std::monostate getClientRegisterCapability(const lsp::RegistrationParams& params) override {
        lsp::sendRequest("client/registerCapability",
                         rfl::to_generic<rfl::UnderlyingEnums>(params));
        return std::monostate{};
    }
};
