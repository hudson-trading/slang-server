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

class SlangLspClient : public lsp::LspClient {
    /// Helper functions to send things to the client

public:
    void setConfig(const Config& params) {
        lsp::sendNotification("slang/setConfig", rfl::to_generic(params));
    }

    std::monostate getClientRegisterCapability(const lsp::RegistrationParams& params) override {
        lsp::sendRequest("client/registerCapability",
                         rfl::to_generic<rfl::UnderlyingEnums>(params));
        return std::monostate{};
    }
};
