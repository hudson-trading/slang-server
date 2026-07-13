//------------------------------------------------------------------------------
// ClientHarness.h
// Test harness for LSP client functionality
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "SlangLspClient.h"
#include "catch2/catch_test_macros.hpp"
#include "lsp/LspTypeExtensions.h"
#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <vector>

class ClientHarness : public SlangLspClient {

    // Model of diagnostics
    std::unordered_map<URI, std::vector<lsp::Diagnostic>> m_diagnostics;

    std::vector<std::string> errors;
    std::vector<std::string> warnings;

public:
    ~ClientHarness() {
        for (const auto& error : errors) {
            FAIL_CHECK("Unhandled client error: " << error);
        }
        for (const auto& warning : warnings) {
            FAIL_CHECK("Unhandled client warning: " << warning);
        }
    }
    void showError(const std::string& message) override { errors.push_back(message); }

    void showWarning(const std::string& message) override { warnings.push_back(message); }

    void setConfig(const Config&) override {}

    std::monostate getClientRegisterCapability(const lsp::RegistrationParams&) override {
        return std::monostate{};
    }

    void onDocPublishDiagnostics(const lsp::PublishDiagnosticsParams& params) override {
        m_diagnostics.insert_or_assign(params.uri, params.diagnostics);
    }

    std::vector<lsp::Diagnostic> getDiagnostics(const URI& uri) {
        auto it = m_diagnostics.find(uri);
        if (it != m_diagnostics.end()) {
            return it->second;
        }
        return {};
    }

    void expectError(const std::string& msg) { expectMessage(errors, "error", msg); }

    void expectWarning(const std::string& msg) { expectMessage(warnings, "warning", msg); }

    std::unordered_map<URI, std::vector<lsp::Range>> m_inactiveRegions;

    void onTextDocumentInactiveRegions(const lsp::InactiveRegionsParams& params) override {
        m_inactiveRegions.insert_or_assign(params.uri, params.regions);
    }

    std::vector<lsp::Range> getInactiveRegions(const URI& uri) {
        auto it = m_inactiveRegions.find(uri);
        if (it != m_inactiveRegions.end()) {
            return it->second;
        }
        return {};
    }

    std::deque<lsp::ShowDocumentParams> m_showDocuments;

    void onShowDocument(const lsp::ShowDocumentParams& params) final {
        m_showDocuments.push_back(params);
        // TODO -- ServerHarness::openFile() once the client and server harnesses are merged
    }

private:
    void expectMessage(std::vector<std::string>& messages, std::string_view kind,
                       const std::string& msg) {
        auto found = std::find_if(messages.begin(), messages.end(), [&](const std::string& item) {
            return item.find(msg) != std::string::npos;
        });

        if (found == messages.end()) {
            FAIL_CHECK("Expected client " << kind << " containing: " << msg);
            return;
        }

        for (auto it = messages.begin(); it != found; ++it) {
            FAIL_CHECK("Unexpected client " << kind << " before expected '" << msg << "': " << *it);
        }
        messages.erase(messages.begin(), std::next(found));
    }
};
