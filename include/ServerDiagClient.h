//------------------------------------------------------------------------------
// ServerDiagClient.h
// Diagnostic client for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "SlangLspClient.h"
#include "lsp/URI.h"

#include "slang/diagnostics/DiagnosticClient.h"
namespace server {
class ServerDiagClient : public slang::DiagnosticClient {
public:
    ServerDiagClient(const slang::SourceManager& sourceManager, SlangLspClient& client) :
        m_sourceManager(sourceManager), m_client(client) {
        m_diagnostics = std::unordered_map<URI, std::vector<lsp::Diagnostic>>();
        cwd = std::filesystem::current_path().string();
    }

    ~ServerDiagClient() { clear(); }
    /// reports from the diagnostic engine
    void report(const slang::ReportedDiagnostic& to_report) override;

    /// report unpublished diags to the client
    void updateDiags();

    // Clear all diagnostics by publishing empty lists, then clear internal data structures
    // We want to see these go away for compilations, because they may be very stale
    void clear();

    // Clear a specific URI's diagnostics, put not publishing to client, since they are still likely
    // relevant
    void clear(URI uri);

private:
    std::unordered_map<URI, std::vector<lsp::Diagnostic>> m_diagnostics;
    // Uris that have modified diags yet to be pushed to the client
    slang::flat_hash_set<URI> m_dirtyUris;

    const slang::SourceManager& m_sourceManager;
    std::string cwd;
    SlangLspClient& m_client;
};
} // namespace server
