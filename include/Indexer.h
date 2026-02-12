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
#include <concepts>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/syntax/SyntaxTree.h"
#include "slang/util/SmallVector.h"

namespace slang::syntax {
class SyntaxNode;
}

namespace slang::parsing {
struct ParserMetadata;
}

struct Indexer {

    Indexer();

    // Configure threading
    void setNumThreads(uint32_t numThreads) { numThreads_ = numThreads; }

    //////////////////////////////////////////
    // Updating interface
    //////////////////////////////////////////

    // Primary indexing function, called on startup
    void startIndexing(const std::vector<Config::IndexConfig>& indexConfigs,
                       std::optional<std::string_view> workspaceFolder);

    // Legacy glob-based indexing, slower
    void startIndexing(const std::vector<std::string>& globs,
                       const std::vector<std::string>& excludeDirs);

    // For workspace changes
    void addDocuments(const std::vector<std::filesystem::path>& paths);
    void onWorkspaceDidChangeWatchedFiles(const lsp::DidChangeWatchedFilesParams& params);

    // For open document lifecycle
    void updateDocument(const std::filesystem::path& uri, const slang::syntax::SyntaxTree& tree);

    //////////////////////////////////////////
    // Querying interface
    //////////////////////////////////////////
    std::vector<std::filesystem::path> getFilesForSymbol(std::string_view name) const;
    std::vector<std::filesystem::path> getFilesForMacro(std::string_view name) const;
    std::vector<std::filesystem::path> getFilesReferencingSymbol(std::string_view name) const;

    struct GlobalSymbolLoc {
        const std::filesystem::path* uri;
        slang::syntax::SyntaxKind kind;
    };
    // Get first symbol location for a name (for instance completions, etc.)
    std::optional<GlobalSymbolLoc> getFirstSymbolLoc(std::string_view name) const;

    // Get all macro names (for macro completions)
    std::vector<std::string> getAllMacroNames() const;

    // Get count of unique symbol names (for testing)
    size_t getSymbolCount() const;

    // Iterate over all symbols (for workspace symbols)
    template<std::invocable<const std::string&, const Indexer::GlobalSymbolLoc&> Callback>
    void forEachSymbol(Callback&& callback) const;

private:
    friend struct IndexWriteGuard;
    friend struct IndexReadGuard;

    // Data structures
    struct GlobalSymbol {
        std::string name;
        slang::syntax::SyntaxKind kind;
    };

    struct IndexedPath {
        const std::filesystem::path* path = nullptr;
        slang::SmallVector<GlobalSymbol> symbols;
        slang::SmallVector<std::string> macros;
        slang::SmallVector<std::string> referencedSymbols;
    };

    // Index storage
    // Using SmallVector<2> to avoid extra indirection for the common case
    std::unordered_map<std::string, slang::SmallVector<GlobalSymbolLoc, 2>> symbolToFiles_;
    std::unordered_map<std::string, slang::SmallVector<const std::filesystem::path*, 2>>
        macroToFiles_;
    // Top level references; References tend to have more entries
    std::unordered_map<std::string, std::vector<const std::filesystem::path*>> symbolReferences_;

    // Storage for unique URIs (all pointers in the index point here)
    std::unordered_set<std::filesystem::path> uniqueUris_;

    void indexPath(const std::filesystem::path& path, IndexedPath& indexedFile);
    void indexAndReport(std::vector<std::filesystem::path> pathsToIndex);

    // Remove all index entries for a path without needing the file contents
    void removePathFromIndex(const std::filesystem::path* pathPtr);

    // Intern a URI to get a stable pointer
    const std::filesystem::path* internUri(const std::filesystem::path& uri);

    // Storage for all indexed files (for efficient removal)
    std::unordered_map<const std::filesystem::path*, IndexedPath> indexedFiles;

    // Extracts symbols and referenced symbols
    static void extractFromRoot(const slang::syntax::CompilationUnitSyntax& root,
                                const slang::parsing::ParserMetadata& meta, IndexedPath& dest);

    // Extracts macros
    template<typename MacroRange>
    static void extractMacros(const MacroRange& macros, IndexedPath& dest);

    static void collectFilesFromDirectory(const std::filesystem::path& dir,
                                          const std::vector<std::string>& excludeDirs,
                                          std::vector<std::filesystem::path>& outFiles);

    // Core indexing function that splits work across threads
    std::vector<IndexedPath> indexPaths(const std::vector<std::filesystem::path>& paths) const;

    // Threading - uses reader-writer lock pattern
    uint32_t numThreads_ = 0;
    mutable std::shared_mutex indexMutex_;
    mutable std::condition_variable_any indexingCondition_;
    bool indexingInProgress_ = false;
    static const int MinFilesForThreading = 8;
};

// Write guard - acquires exclusive lock, blocks readers and other writers
struct IndexWriteGuard {
    Indexer& indexer;
    std::unique_lock<std::shared_mutex> lock;

    IndexWriteGuard(Indexer& idx) : indexer(idx), lock(idx.indexMutex_) {
        // Wait for any in-progress indexing to complete
        idx.indexingCondition_.wait(lock, [&] { return !idx.indexingInProgress_; });
        idx.indexingInProgress_ = true;
    }

    IndexWriteGuard(const IndexWriteGuard&) = delete;
    IndexWriteGuard(IndexWriteGuard&&) = delete;

    ~IndexWriteGuard() {
        indexer.indexingInProgress_ = false;
        lock.unlock();
        indexer.indexingCondition_.notify_all();
    }
};

// Read guard - acquires shared lock, allows concurrent readers
struct IndexReadGuard {
    std::shared_lock<std::shared_mutex> lock;

    IndexReadGuard(const Indexer& idx) : lock(idx.indexMutex_) {
        // Wait for any in-progress indexing to complete before reading
        // Note: We need to temporarily release shared lock to wait on condition
        // Actually, condition_variable_any works with shared_lock
        idx.indexingCondition_.wait(lock, [&] { return !idx.indexingInProgress_; });
    }

    IndexReadGuard(const IndexReadGuard&) = delete;
    IndexReadGuard(IndexReadGuard&&) = default;
};

// Implementation of forEachSymbol template (needs IndexReadGuard definition)
template<std::invocable<const std::string&, const Indexer::GlobalSymbolLoc&> Callback>
void Indexer::forEachSymbol(Callback&& callback) const {
    IndexReadGuard guard(*this);
    for (const auto& [name, entries] : symbolToFiles_) {
        for (const auto& entry : entries) {
            callback(name, entry);
        }
    }
}
