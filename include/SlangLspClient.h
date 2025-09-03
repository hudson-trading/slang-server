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
};
