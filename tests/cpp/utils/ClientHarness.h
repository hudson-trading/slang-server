//------------------------------------------------------------------------------
// ClientHarness.h
// Test harness for LSP client functionality
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "SlangLspClient.h"
#include "catch2/catch_test_macros.hpp"
#include <unordered_map>

class ClientHarness : public SlangLspClient {

    int n_errors = 0;

    // Model of diagnostics
    std::unordered_map<URI, std::vector<lsp::Diagnostic>> m_diagnostics;

    std::vector<std::string> errors;

public:
    ~ClientHarness() {
        for (const auto& error : errors) {
            FAIL_CHECK("Unhandled client error: " << error);
        }
    }
    void showError(const std::string& message) override {
        SlangLspClient::showError(message);
        n_errors++;
        errors.push_back(message);
    }

    void onDocPublishDiagnostics(const lsp::PublishDiagnosticsParams& params) override {
        m_diagnostics.insert_or_assign(params.uri, params.diagnostics);
    }

    std::vector<lsp::Diagnostic> getDiagnostics(const URI& uri) {
        auto it = m_diagnostics.find(uri);
        if (it != m_diagnostics.end()) {
            return it->second;
        }
        throw std::runtime_error("No diagnostics found for URI: " + std::string{uri.getPath()});
    }

    void expectError(const std::string& msg) {
        // check that msg is in the first error
        CHECK(errors.size() > 0);
        CHECK(errors[0].find(msg) != std::string::npos);
        // pop front
        errors.erase(errors.begin());
    }

    std::deque<lsp::ShowDocumentParams> m_showDocuments;

    void onShowDocument(const lsp::ShowDocumentParams& params) final {
        m_showDocuments.push_back(params);
        // TODO -- ServerHarness::openFile() once the client and server harnesses are merged
    }
};
