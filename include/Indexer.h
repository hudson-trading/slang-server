//------------------------------------------------------------------------------
// Indexer.h
// Symbol and macro indexer for the workspace.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

#include "slang/syntax/SyntaxTree.h"

struct Indexer {
    enum class IndexDataUpdateType : int { Added, Removed };
    using IndexKeyType = std::string;

    using SymbolIndexData = std::tuple<IndexKeyType, lsp::SymbolKind, std::string>;
    using MacroIndexData = IndexKeyType;

    struct IndexedPath {
        std::string path;
        std::vector<Indexer::SymbolIndexData> relevantSymbols;
        std::vector<Indexer::MacroIndexData> relevantMacros;
    };

    Indexer();

    template<typename T>
    using IndexDataSet = slang::flat_hash_set<T>;
    struct IndexMapEntry {
        std::string toString(std::string_view name) const;
        lsp::WorkspaceSymbol toWorkSpaceSymbol(std::string_view name) const;

        // The ordering of these members is important. They should be sorted by location first.
        URI uri;

        lsp::SymbolKind kind{};
        std::optional<std::string> containerName{};

        // This should be enough to uniquely identify an entry
        bool operator==(const IndexMapEntry& other) const { return uri == other.uri; }

        inline static IndexMapEntry fromSymbolData(lsp::SymbolKind kind, std::string container,
                                                   URI uri) {
            IndexMapEntry out(uri);
            out.kind = kind;
            out.containerName = container.empty() ? std::optional<std::string>()
                                                  : std::move(container);
            return out;
        }
        inline static IndexMapEntry fromMacroData(URI uri) { return IndexMapEntry(uri); }
        IndexMapEntry(URI uri) : uri{uri} {}
        // Non-copyable
        IndexMapEntry(IndexMapEntry&) = delete;
        IndexMapEntry(IndexMapEntry&&) noexcept = default;
    };

    using IndexMap = std::multimap<IndexKeyType, IndexMapEntry>;
    struct IndexStorage {
        /// Returns range
        auto getEntriesForKeyConst(const IndexKeyType& key) const {
            return entries.equal_range(key);
        }

        auto getEntriesForKey(const IndexKeyType& key) { return entries.equal_range(key); }

        void addEntry(IndexKeyType key, IndexMapEntry entry);

        void removeEntry(IndexKeyType key, URI uri);

        const IndexMap& getAllEntries() const { return entries; }

        size_t sizeInBytes() const {
            return entries.size() * sizeof(IndexMap::value_type) +
                   entries.size() * sizeof(IndexMap::key_type);
        }

    private:
        friend struct Indexer;
        IndexMap entries;
    };

    /// Returns sorted (by symbol lexicographically) array of workspace symbols
    std::vector<lsp::WorkspaceSymbol> getAllWorkspaceSymbols() const;

    /// Index files/symbols/macros from the provided globs. Populate internal symbol ->
    /// List<filenames> map.
    void startIndexing(const std::vector<std::string>& globs,
                       const std::vector<std::string>& excludeDirs, uint32_t numThreads = 0);

    /// Add following documents to the index
    void addDocuments(const std::vector<std::filesystem::path>& paths, uint32_t numThreads = 0);

    void indexTree(const slang::syntax::SyntaxTree& tree);

    void indexPath(IndexedPath& indexedFile);

    /// Remove these documents from the index's awareness. E.g. on file deletion
    void removeDocuments(const std::vector<std::filesystem::path>& paths, uint32_t numThreads = 0);

    // for convenience/testing for now, could remove later
    size_t sizeInBytes() const { return macroToFiles.sizeInBytes() + symbolToFiles.sizeInBytes(); }

    const IndexStorage& symbolMap() const { return symbolToFiles; }
    const IndexStorage& macroMap() const { return macroToFiles; }

    struct DocSyntaxChange {
        IndexDataSet<SymbolIndexData> newSymbols;
        IndexDataSet<SymbolIndexData> oldSymbols;
        IndexDataSet<MacroIndexData> newMacros;
        IndexDataSet<MacroIndexData> oldMacros;

        // Multiline, really just for debugging
        std::string toString() const {
            std::stringstream ss;

            auto addSymbol = [&ss](const auto& data, const char* _name) {
                ss << _name << ": \n";
                for (const auto& [name, kind, container] : data) {
                    ss << "\t[" << name << "]\n";
                }
            };

            auto addMacro = [&ss](const auto& data, const char* _name) {
                ss << _name << ": \n";
                for (const auto& name : data) {
                    ss << "\t[" << name << "]\n";
                }
            };

            addSymbol(newSymbols, "newSymbols");
            addSymbol(oldSymbols, "oldSymbols");
            addMacro(newMacros, "newMacros");
            addMacro(oldMacros, "oldMacros");

            return ss.str();
        }

        DocSyntaxChange(DocSyntaxChange&) = delete;
        DocSyntaxChange(DocSyntaxChange&&) = default;

    protected:
        DocSyntaxChange() = default;
    };

    // void addSyntaxUpdate(const URI& uri, const slang::syntax::SyntaxTree* tree);
    void consumeDocSyntaxChange(const URI& uri, DocSyntaxChange change) {
        pendingUpdates[uri].consumeDocSyntaxChange(std::move(change));
    }

    void noteDocSaved(const URI& uri);

    auto getEntriesForSymbol(std::string_view symbol) const {
        return symbolToFiles.getEntriesForKeyConst(std::string(symbol));
    }

    auto getEntriesForMacro(std::string_view symbol) const {
        return macroToFiles.getEntriesForKeyConst(std::string(symbol));
    }

    /// Signals when indexing is complete
    void waitForIndexingCompletion() const {
        std::unique_lock<std::mutex> lock(indexingMutex);
        indexingCondition.wait(lock, [this] { return indexingComplete; });
    }

    void notifyIndexingComplete() {
        {
            std::lock_guard<std::mutex> lock(indexingMutex);
            indexingComplete = true;
        }
        indexingCondition.notify_all();
    }

    void resetIndexingComplete() {
        {
            std::lock_guard<std::mutex> lock(indexingMutex);
            indexingComplete = false;
        }
    }

    std::vector<std::filesystem::path> getRelevantFilesForName(std::string_view name) const;

    std::vector<std::filesystem::path> getFilesForMacro(std::string_view name) const;

    // Macro -> relevant files
    IndexStorage macroToFiles;
    // Modules/Interfaces/Packages -> relevant files
    IndexStorage symbolToFiles;

    static const int MinFilesForThreading = 4;

private:
    mutable std::condition_variable indexingCondition;
    mutable std::mutex indexingMutex;
    bool indexingComplete = false;

    template<typename T>
    using DataUpdate = std::pair<IndexDataUpdateType, T>;
    struct UnsavedDocumentUpdate {
        // These lists will always be mutually exclusive
        slang::flat_hash_map<SymbolIndexData, IndexDataUpdateType> symbolUpdates;
        slang::flat_hash_map<MacroIndexData, IndexDataUpdateType> macroUpdates;

        void consumeDocSyntaxChange(DocSyntaxChange&& change);
    };

    /// Updates by document which are still unsaved
    slang::flat_hash_map<URI, UnsavedDocumentUpdate> pendingUpdates;

    void dumpIndex() const;

    void indexDidChange() { workSpaceResultCached = false; }

    mutable bool workSpaceResultCached = false;
};
