//------------------------------------------------------------------------------
// Indexer.h
// Symbol and macro indexer for the workspace.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/URI.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/syntax/SyntaxTree.h"
#include "slang/util/SmallVector.h"

struct Indexer {
    // Data structures
    struct GlobalSymbol {
        std::string name;
        slang::syntax::SyntaxKind kind;
    };

    struct IndexedPath {
        std::string path;
        std::vector<GlobalSymbol> symbols;
        std::vector<std::string> macros;
    };

    Indexer();

    // Primary indexing function, called on startup
    void startIndexing(const std::vector<std::string>& globs,
                       const std::vector<std::string>& excludeDirs, uint32_t numThreads = 0);

    // For workspace changes
    void addDocuments(const std::vector<std::filesystem::path>& paths, uint32_t numThreads = 0);
    void removeDocuments(const std::vector<std::filesystem::path>& paths, uint32_t numThreads = 0);

    // For open document lifecycle
    void openDocument(const URI& uri, const slang::syntax::SyntaxTree& tree);
    void updateDocument(const URI& uri, const slang::syntax::SyntaxTree& tree);
    void closeDocument(const URI& uri);

    // Threading API
    void waitForIndexingCompletion() const;
    struct GlobalSymbolLoc {
        const URI* uri;
        slang::syntax::SyntaxKind kind{};
    };

    // Index storage, public for querying
    // Using SmallVector<2> to avoid heap allocations for the common case
    std::unordered_map<std::string, slang::SmallVector<GlobalSymbolLoc, 2>> symbolToFiles;
    std::unordered_map<std::string, slang::SmallVector<const URI*, 2>> macroToFiles;

    // Storage for unique URIs (all pointers in the index point here)
    std::unordered_set<URI> uniqueUris;

    std::vector<std::filesystem::path> getRelevantFilesForName(std::string_view name) const;
    std::vector<std::filesystem::path> getFilesForMacro(std::string_view name) const;

    static const int MinFilesForThreading = 4;

private:
    friend struct IndexGuard;

    void indexPath(IndexedPath& indexedFile);
    void notifyIndexingComplete();
    void resetIndexingComplete();

    // Intern a URI to get a stable pointer
    const URI* internUri(const URI& uri);

    // Storage for open documents
    std::unordered_map<URI, IndexedPath> openDocuments;

    // Thread synchronization
    mutable std::condition_variable indexingCondition;
    mutable std::mutex indexingMutex;
    bool indexingComplete = false;
};
