//------------------------------------------------------------------------------
// Indexer.h
// Symbol and macro indexer for the workspace.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include <condition_variable>
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
        const std::filesystem::path* path = nullptr;
        std::vector<GlobalSymbol> symbols;
        std::vector<std::string> macros;
        std::vector<std::string> referencedSymbols;
    };

    Indexer();

    // Primary indexing function, called on startup
    void startIndexing(const std::vector<std::string>& globs,
                       const std::vector<std::string>& excludeDirs, uint32_t numThreads = 0);
    void startIndexing(const std::vector<Config::IndexConfig>& indexConfigs,
                       std::optional<std::string_view> workspaceFolder, uint32_t numThreads = 0);

    // For workspace changes
    void addDocuments(const std::vector<std::filesystem::path>& paths, uint32_t numThreads = 0);
    void onWorkspaceDidChangeWatchedFiles(const lsp::DidChangeWatchedFilesParams& params);

    // For open document lifecycle
    void updateDocument(const std::filesystem::path& uri, const slang::syntax::SyntaxTree& tree);

    // Threading API
    void waitForIndexingCompletion() const;
    struct GlobalSymbolLoc {
        const std::filesystem::path* uri;
        slang::syntax::SyntaxKind kind{};
    };

    // Index storage, public for querying
    // Using SmallVector<2> to avoid extra indirection for the common case
    std::unordered_map<std::string, slang::SmallVector<GlobalSymbolLoc, 2>> symbolToFiles;
    std::unordered_map<std::string, slang::SmallVector<const std::filesystem::path*, 2>>
        macroToFiles;

    // Top level references; References tend to have more entries
    std::unordered_map<std::string, std::vector<const std::filesystem::path*>> symbolReferences;

    // Map of filename -> fspath. Used for looking up incdirs
    std::unordered_map<std::string, std::vector<const std::filesystem::path*>> fileMap;

    // Storage for unique URIs (all pointers in the index point here)
    std::unordered_set<std::filesystem::path> uniqueUris;

    std::vector<std::filesystem::path> getRelevantFilesForName(std::string_view name) const;
    std::vector<std::filesystem::path> getFilesForMacro(std::string_view name) const;
    std::vector<std::filesystem::path> getFilesReferencingSymbol(std::string_view name) const;

    static const int MinFilesForThreading = 4;

private:
    friend struct IndexGuard;

    void indexPath(const std::filesystem::path& path, IndexedPath& indexedFile);
    void indexAndReport(std::vector<std::filesystem::path> pathsToIndex, uint32_t numThreads);
    void notifyIndexingComplete();
    void resetIndexingComplete();

    // Remove all index entries for a path without needing the file contents
    void removePathFromIndex(const std::filesystem::path* pathPtr);

    // Intern a URI to get a stable pointer
    const std::filesystem::path* internUri(const std::filesystem::path& uri);

    // Storage for all indexed files (for efficient removal)
    std::unordered_map<const std::filesystem::path*, IndexedPath> indexedFiles;

    // Thread synchronization
    mutable std::condition_variable indexingCondition;
    mutable std::mutex indexingMutex;
    bool indexingComplete = false;
};
