//------------------------------------------------------------------------------
// Indexer.cpp
// Implementation of the server's workspace indexer.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "Indexer.h"

#include "Logging.h"
#include "lsp/LspTypes.h"
#include <BS_thread_pool.hpp>
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <iostream>
#include <string_view>

#include "slang/driver/SourceLoader.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/util/TimeTrace.h"
#include "slang/util/Util.h"

namespace fs = std::filesystem;

namespace {

// Guard object for methods that work on index
class IndexGuard {
public:
    Indexer& m_indexer;
    IndexGuard(Indexer& indexer) : m_indexer(indexer) { indexer.resetIndexingComplete(); }

    IndexGuard(const IndexGuard&) = delete;
    IndexGuard(IndexGuard&&) = delete;

    ~IndexGuard() { m_indexer.notifyIndexingComplete(); }
};

void getDataFromTree(const slang::syntax::SyntaxTree* tree, auto& destSymbols, auto& destMacros) {
    if (!tree) {
        return;
    }
    const auto& meta = tree->getMetadata();

    const auto slangKindToLspKind = [](slang::syntax::SyntaxKind kind) {
        switch (kind) {
            case slang::syntax::SyntaxKind::InterfaceDeclaration:
                return lsp::SymbolKind::Interface;
            case slang::syntax::SyntaxKind::ModuleDeclaration:
                return lsp::SymbolKind::Module;
            case slang::syntax::SyntaxKind::ProgramDeclaration:
                return lsp::SymbolKind::Module;
            case slang::syntax::SyntaxKind::PackageDeclaration:
                return lsp::SymbolKind::Package;
            default:
                SLANG_UNREACHABLE;
        }
    };

    // Handle module/interface declarations
    for (auto& [n, _] : meta.nodeMeta) {
        lsp::SymbolKind kind;
        auto module = n->as_if<slang::syntax::ModuleDeclarationSyntax>();

        std::string_view name = module ? module->header->name.valueText() : std::string_view();
        if (module && !name.empty()) {
            std::string containerName;
            if (module->parent) {
                if (auto containerSyntax =
                        module->parent->as_if<slang::syntax::ModuleDeclarationSyntax>()) {
                    containerName = containerSyntax->header->name.valueText();
                }
            }

            destSymbols.insert(destSymbols.end(),
                               make_tuple(std::string(name), slangKindToLspKind(module->kind),
                                          std::move(containerName)));
        }
    }

    // get classes another way
    for (const auto& _class : meta.classDecls) {
        auto name = _class->name.valueText();
        if (!name.empty()) {
            std::string containerName;
            if (_class->parent) {

                if (auto containerSyntax =
                        _class->parent->as_if<slang::syntax::ModuleDeclarationSyntax>()) {
                    containerName = containerSyntax->header->name.valueText();
                }
            }

            destSymbols.insert(destSymbols.end(),
                               std::make_tuple(std::string(name), lsp::SymbolKind::Class,
                                               std::move(containerName)));
        }
    }

    // index macros if no global symbols were found
    if (!(meta.classDecls.empty() && meta.nodeMeta.empty())) {
        return;
    }

    const auto& macros = tree->getDefinedMacros();

    for (const auto macro : macros) {
        if (!macro)
            continue;

        // Only add macros that are defined in this file (not an included one,
        // although includes shouldn't be getting expanded here
        if (macro->name.location().buffer() != tree->getSourceBufferIds()[0])
            continue;

        destMacros.insert(destMacros.end(), std::string(macro->name.valueText()));
    }
}

void populateIndexForSingleFile(const fs::path& path, Indexer::IndexedPath& dest) {
    dest.path = path.string();

    // For the index, we actually don't want to expand includes since we only want the symbols
    // defined in that file, and that'll slow down indexing
    if (auto tree = slang::syntax::SyntaxTree::fromFile(path.string())) {
        getDataFromTree(tree->get(), dest.relevantSymbols, dest.relevantMacros);
    }
    else {
        std::cerr << "Error populateSingleIndexFile: " << path << std::endl;
        // TODO: decide the proper error handling here
        return;
    }
};

std::vector<Indexer::IndexedPath> indexPaths(const std::vector<fs::path>& paths,
                                             uint32_t numThreads) {
    if (paths.size() < slang::driver::SourceLoader::MinFilesForThreading) {
        numThreads = 1;
    }

    // Populate/add to an instance of above struct
    std::vector<Indexer::IndexedPath> loadResults;
    loadResults.resize(paths.size());
    {
        slang::TimeTraceScope _timeScope("syntaxTreeCreation",
                                         [&] { return fmt::format("numPaths: {}", paths.size()); });
        // Thread the syntax tree creation portion
        if (numThreads != 1) {
            BS::thread_pool threadPool(numThreads);

            threadPool.detach_blocks(size_t(0), paths.size(), [&](size_t start, size_t end) {
                for (size_t i = start; i < end; i++) {
                    populateIndexForSingleFile(paths[i], loadResults[i]);
                }
            });
            threadPool.wait();
        }
        else {
            for (size_t i = 0; i < paths.size(); ++i) {
                populateIndexForSingleFile(paths[i], loadResults[i]);
            }
        }
    }

    return loadResults;
}

} // namespace

Indexer::Indexer() {
    notifyIndexingComplete();
}

void Indexer::dumpIndex() const {
    std::cerr << "Dumping Index\n";
    static constexpr int max = 10000;
    int cnt = 0;
    for (const auto& [key, paths] : symbolToFiles.getAllEntries()) {
        std::cerr << "Symbol: " << " [" << paths.toString(key).c_str() << "]\n";
        std::cerr << "----\n";
        if (++cnt > max) {
            break;
        }
    }
    cnt = 0;
    for (const auto& [key, paths] : macroToFiles.getAllEntries()) {
        std::cerr << "Macro: " << " [" << paths.toString(key).c_str() << "]\n";
        std::cerr << "----\n";
        if (++cnt > max) {
            break;
        }
    }
}

std::string Indexer::IndexMapEntry::toString(std::string_view name) const {

    std::stringstream ss;
    ss << "name: [" << name << "], uri: [" << location.uri.str() << "], kind: [" << (int)kind
       << "], container: [" << containerName.value_or("") << "]";

    return ss.str();
}

lsp::WorkspaceSymbol Indexer::IndexMapEntry::toWorkSpaceSymbol(std::string_view name) const {
    return lsp::WorkspaceSymbol{
        .location = location,
        .name = std::string(name),
        .kind = kind,
        .containerName = containerName,
    };
}

void Indexer::IndexStorage::addEntry(IndexKeyType name, IndexMapEntry entry) {
    auto [start, end] = getEntriesForKey(name);

    /// This operation needs to be idempotent
    if (auto it = std::find_if(start, end, [&](auto& it) { return it.second == entry; });
        it != end) {
        it->second.kind = entry.kind;
        it->second.containerName = std::move(entry.containerName);
        return;
    }

    entries.emplace(std::move(name), std::move(entry));
}

void Indexer::IndexStorage::removeEntry(IndexKeyType key, URI uri) {
    auto [start, end] = getEntriesForKey(key);

    // an equivalent entry (same name and uri) should only exist once
    for (auto it = start; it != end; ++it) {
        if (it->second.location.uri == uri) {
            std::cerr << "found\n";
            entries.erase(it);
            return;
        }
    }
}

void Indexer::UnsavedDocumentUpdate::consumeDocSyntaxChange(DocSyntaxChange&& change) {

    auto getUpdates =
        []<typename T>(const IndexDataSet<T>& newVals, const IndexDataSet<T>& oldVals,
                       slang::flat_hash_map<T, Indexer::IndexDataUpdateType>& updatesOut) {
            slang::flat_hash_map<const T*, Indexer::IndexDataUpdateType> tempUpdates;
            std::for_each(newVals.begin(), newVals.end(), [&](auto& newSym) {
                if (!oldVals.contains(newSym)) {
                    tempUpdates.emplace(&newSym, Indexer::IndexDataUpdateType::Added);
                }
            });
            std::for_each(oldVals.begin(), oldVals.end(), [&](const auto& newSym) {
                if (!newVals.contains(newSym)) {
                    tempUpdates.emplace(&newSym, Indexer::IndexDataUpdateType::Removed);
                }
            });
            for (auto [upd, updateType] : tempUpdates) {
                auto it = updatesOut.find(*upd);
                if (it == updatesOut.end()) {
                    updatesOut.emplace(std::move(*upd), updateType);
                }
                else if (it->second != updateType) {
                    // Updates annihilate
                    updatesOut.erase(it);
                }
            }
        };

    getUpdates(change.newSymbols, change.oldSymbols, symbolUpdates);
    getUpdates(change.newMacros, change.oldMacros, macroUpdates);
}

void Indexer::noteDocSaved(const URI& uri) {
    auto updateIt = pendingUpdates.find(uri);
    if (updateIt == pendingUpdates.end()) {
        std::cerr << "Document saved: " << uri.str() << " No pending changes\n";
        return;
    }
    const auto& update = updateIt->second;

    for (auto& it : symbolToFiles.getAllEntries()) {
        if (it.second.location.uri == uri) {
            std::cerr << it.second.toString(it.first) << "\n";
        }
    }

    std::for_each(
        update.symbolUpdates.begin(), update.symbolUpdates.end(), [&](auto& singleUpdate) {
            auto& [indexData, updateType] = singleUpdate;
            auto& [name, kind, container] = indexData;

            switch (updateType) {
                case Indexer::IndexDataUpdateType::Added:
                    symbolToFiles.addEntry(std::move(name),
                                           std::move(IndexMapEntry::fromSymbolData(
                                               kind, container, lsp::LocationUriOnly{uri})));
                    break;
                case Indexer::IndexDataUpdateType::Removed:
                    symbolToFiles.removeEntry(name, uri);
                    break;
            }
        });

    std::for_each(update.macroUpdates.begin(), update.macroUpdates.end(), [&](auto& singleUpdate) {
        auto& [name, updateType] = singleUpdate;
        switch (updateType) {
            case Indexer::IndexDataUpdateType::Added:
                macroToFiles.addEntry(std::move(name),
                                      std::move(IndexMapEntry::fromMacroData(uri)));
                break;
            case Indexer::IndexDataUpdateType::Removed:
                macroToFiles.removeEntry(name, uri);
                break;
        }
    });

    pendingUpdates.erase(updateIt);
    indexDidChange();
}

// synchronously index a tree
void Indexer::indexTree(const slang::syntax::SyntaxTree& tree) {
    IndexGuard guard(*this);
    indexDidChange();
    Indexer::IndexedPath dest;
    getDataFromTree(&tree, dest.relevantSymbols, dest.relevantMacros);
    indexPath(dest);
}

void Indexer::indexPath(Indexer::IndexedPath& indexedFile) {
    for (auto& [symbol, kind, container] : indexedFile.relevantSymbols) {
        symbolToFiles.addEntry(std::move(symbol),
                               std::move(IndexMapEntry::fromSymbolData(
                                   kind, container,
                                   lsp::LocationUriOnly{URI::fromFile(indexedFile.path)})));
    }
    for (auto& macro : indexedFile.relevantMacros) {
        macroToFiles.addEntry(std::move(macro),
                              IndexMapEntry::fromMacroData(URI::fromFile(indexedFile.path)));
    }
}

void Indexer::addDocuments(const std::vector<std::filesystem::path>& paths, uint32_t numThreads) {
    slang::TimeTraceScope timeScope("Indexer::addDocuments", "");
    IndexGuard guard(*this);

    std::vector<Indexer::IndexedPath> indexedPaths = indexPaths(paths, numThreads);

    {
        slang::TimeTraceScope mapPopulate("populateSymbolMap", "");
        indexDidChange();
        for (auto& indexedFile : indexedPaths) {
            indexPath(indexedFile);
        }
    }
    notifyIndexingComplete();
}

void Indexer::removeDocuments(const std::vector<std::filesystem::path>& paths,
                              uint32_t numThreads) {
    slang::TimeTraceScope timeScope("Indexer::removeDocuments", "");
    IndexGuard guard(*this);

    std::vector<Indexer::IndexedPath> indexedPaths = indexPaths(paths, numThreads);
    {
        slang::TimeTraceScope mapPopulate("removeSymbolMap", "");
        indexDidChange();
        for (const auto& entry : indexedPaths) {
            URI uri = URI::fromFile(entry.path);
            for (const auto& [sym, _, _other] : entry.relevantSymbols) {
                symbolToFiles.removeEntry(sym, uri);
            }
            for (const auto& macro : entry.relevantMacros) {
                macroToFiles.removeEntry(macro, uri);
            }
            // If there were pending changes, those can be dropped
            pendingUpdates.erase(uri);
        }
    }
}

std::vector<lsp::WorkspaceSymbol> Indexer::getAllWorkspaceSymbols() const {
    static std::vector<lsp::WorkspaceSymbol> result;

    if (workSpaceResultCached) {
        return result;
    }

    result.reserve(symbolToFiles.getAllEntries().size());
    result.clear();
    for (const auto& [sym, entry] : symbolToFiles.getAllEntries()) {
        result.emplace_back(entry.toWorkSpaceSymbol(sym));
    }

    workSpaceResultCached = true;

    return result;
}

bool isExcluded(const std::string& path, const std::vector<std::string>& excludeDirs) {
    for (const auto& dir : excludeDirs) {
        if (path.find(dir) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<fs::path> Indexer::getRelevantFilesForName(std::string_view name) const {

    auto [start, end] = getEntriesForSymbol(name);

    std::vector<fs::path> out;
    for (const auto& [_, entry] : std::ranges::subrange(start, end)) {
        out.push_back(fs::path(entry.location.uri.getPath()));
    }

    return out;
}

std::vector<fs::path> Indexer::getFilesForMacro(std::string_view name) const {
    auto [start, end] = getEntriesForMacro(name);

    std::vector<fs::path> out;
    for (const auto& [_, entry] : std::ranges::subrange(start, end)) {
        out.push_back(fs::path(entry.location.uri.getPath()));
    }

    return out;
}

void Indexer::startIndexing(const std::vector<std::string>& globs,
                            const std::vector<std::string>& excludeDirs, uint32_t numThreads) {
    slang::TimeTraceScope timeScope("indexIncludeGlobs", "");
    resetIndexingComplete();

    std::vector<fs::path> pathsToIndex;
    for (const auto& pattern : globs) {
        slang::SmallVector<fs::path> out;
        std::error_code ec;
        svGlob({}, pattern, slang::GlobMode::Files, out, true, ec);

        if (ec) {
            std::cerr << "Error indexing: " << ec.message() << ", path: " << pattern << "\n";
            // TODO: decide the proper error handling here
            continue;
        }

        for (const auto& path : out) {
            if (isExcluded(path, excludeDirs)) {
                continue;
            }
            pathsToIndex.push_back(path);
        }
    }

    std::cerr << "Indexing " << pathsToIndex.size() << " total files\n";

    addDocuments(pathsToIndex, numThreads);

    std::cerr << "Indexing complete. Total symbols: " << symbolToFiles.getAllEntries().size()
              << " Total Macros: " << macroToFiles.getAllEntries().size()
              << " Approximate size: " << sizeInBytes() << " B\n";

    notifyIndexingComplete();
}
