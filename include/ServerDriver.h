//------------------------------------------------------------------------------
// ServerDriver.h
// Server driver class for processing syntax trees with indexing support
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ServerDiagClient.h"
#include "SlangLspClient.h"
#include "ast/ServerCompilation.h"
#include "completions/CompletionDispatch.h"
#include "lsp/URI.h"
#include <memory>
#include <unordered_map>
#include <vector>

#include "slang/driver/Driver.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"
#include "slang/util/FlatMap.h"
namespace server {
using namespace slang;
enum FileUpdateType {
    OPEN,
    CHANGE,
    SAVE,
};

/// @brief Manages the document handles, which include open and referenced symbols/documents.
/// Syntax trees and options are used to build one, after flags are processed via a slang driver.
/// Compilations can be created either from a document (using the index to populate) or the existing
/// options passed in a filelist
class ServerDriver {
public:
    static std::unique_ptr<ServerDriver> create(Indexer& indexer, SlangLspClient& client,
                                                std::string_view flags,
                                                std::vector<std::string> buildfiles = {},
                                                const ServerDriver* oldDriver = nullptr);
    /// Mapping of URI to SlangDoc, which may hold a shallow analysis of the document
    std::unordered_map<URI, std::shared_ptr<SlangDoc>> docs;

    // Owned by the Slang Driver
    SourceManager& sm;
    DiagnosticEngine& diagEngine;
    SlangLspClient& client;

    driver::Driver driver;

    /// Options parsed from the flags on creation
    Bag options;

    // References m_client, receives diags from the engine
    std::shared_ptr<ServerDiagClient> diagClient;
    // The current compilation, if one has been created
    std::unique_ptr<ServerCompilation> comp;

    CompletionDispatch completions;

    /// @brief Destructor
    ~ServerDriver() {
        // Clear diags from this driver
        diagClient->clear();
    };

    /// @brief Gets a document by URI, creating it if it doesn't exist
    SlangDoc& openDocument(const URI& uri, const std::string_view text);

    /// @brief Close a document and remove it from the open docs set
    void closeDocument(const URI& uri);

    void onDocDidChange(const lsp::DidChangeTextDocumentParams& params);

    void updateDoc(SlangDoc& doc, FileUpdateType type);

    std::shared_ptr<SlangDoc> getDocument(const URI& uri) {
        auto it = docs.find(uri);
        if (it != docs.end())
            return it->second;
        auto doc = SlangDoc::open(uri, sm, this->options);
        if (doc)
            docs[uri] = doc;
        return doc;
    }

    std::vector<std::shared_ptr<SlangDoc>> getDependentDocs(std::shared_ptr<SyntaxTree> tree);

    std::vector<std::string> getModulesInFile(const std::string& path);

    /// @brief Creates a compilation from the given URI and top module name.
    /// @return True if the compilation was created successfully
    bool createCompilation(const URI& uri, std::string_view top);

    /// @brief Creates a compilation from the given syntax trees, typically when the .f already
    /// specifies the top level(s). Does not use the index.
    /// @return True if the compilation was created successfully
    bool createCompilation();

    /// @brief Constructs a new ServerDriver instance by creating and configuring a driver
    /// internally
    /// @param indexer Reference to an indexer for symbol/macro indexing
    /// @param client Reference to the slang client for error reporting
    /// @param flags Command line flags to parse
    /// @param buildfiles List of build files to process
    /// @param options Bag of options for configuration
    ServerDriver(Indexer& indexer, SlangLspClient& client, std::string_view flags,
                 std::vector<std::string> buildfiles);

private:
    /// Reference to the indexer for module/macro indexing
    Indexer& indexer;

    /// Set of URIs for documents that are explicitly opened by the client
    flat_hash_set<URI> m_openDocs;
};
} // namespace server
