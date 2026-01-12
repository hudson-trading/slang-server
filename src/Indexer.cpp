//------------------------------------------------------------------------------
// Indexer.cpp
// Implementation of the server's workspace indexer.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "Indexer.h"

#include "Config.h"
#include "util/Logging.h"
#include <BS_thread_pool.hpp>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <string_view>
#include <unordered_map>

#include "slang/driver/SourceLoader.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"
#include "slang/util/Util.h"

namespace fs = std::filesystem;

// Guard object for methods that work on index
struct IndexGuard {
    Indexer& m_indexer;
    IndexGuard(Indexer& indexer) : m_indexer(indexer) { indexer.resetIndexingComplete(); }

    IndexGuard(const IndexGuard&) = delete;
    IndexGuard(IndexGuard&&) = delete;

    ~IndexGuard() { m_indexer.notifyIndexingComplete(); }
};

namespace {

bool isNestedModule(const slang::syntax::SyntaxNode& node) {
    return node.parent && slang::syntax::ModuleDeclarationSyntax::isKind(node.parent->kind);
}

void extractDataFromTree(const slang::syntax::SyntaxTree& tree, Indexer::IndexedPath& dest) {

    const auto& meta = tree.getMetadata();

    // Extract symbols: modules, interfaces, packages, programs (skip nested ones - they're private)
    for (auto& [node, _] : meta.nodeMeta) {
        std::string_view name = node->header->name.valueText();
        if (!name.empty() && !isNestedModule(*node)) {
            dest.symbols.push_back(
                Indexer::GlobalSymbol{.name = std::string(name), .kind = node->kind});
        }
    }

    // Extract classes (skip nested ones - they're private)
    for (const auto& classDecl : meta.classDecls) {
        std::string_view name = classDecl->name.valueText();
        if (!name.empty() && !isNestedModule(*classDecl)) {
            dest.symbols.push_back(Indexer::GlobalSymbol{
                .name = std::string(name), .kind = slang::syntax::SyntaxKind::ClassDeclaration});
        }
    }

    // Extract referenced symbols
    for (auto ref : meta.getReferencedSymbols()) {
        if (!ref.empty())
            dest.referencedSymbols.push_back(std::string(ref));
    }

    // Extract macros (only if no global symbols were found)
    if (meta.classDecls.empty() && meta.nodeMeta.empty()) {
        const auto& macros = tree.getDefinedMacros();
        for (const auto macro : macros) {
            if (!macro)
                continue;

            // Only add macros defined in this file (not included files)
            // This won't happen in the intial index because of the lack of incdirs,
            // but may occur in open document reindexing
            if (macro->name.location().buffer() != tree.getSourceBufferIds()[0])
                continue;

            dest.macros.push_back(std::string(macro->name.valueText()));
        }
    }
}

void populateIndexForSingleFile(const fs::path& path, Indexer::IndexedPath& dest,
                                slang::SourceManager& sourceManager, const slang::Bag& options) {
    dest.path = path.string();

    // Don't expand includes - we only want symbols defined in this file
    auto tree = slang::syntax::SyntaxTree::fromFile(path.string(), sourceManager, options);
    if (!tree) {
        ERROR("Error parsing file for indexing: {}", path.string());
        return;
    }

    extractDataFromTree(*tree->get(), dest);
}

std::vector<Indexer::IndexedPath> indexPaths(const std::vector<fs::path>& paths,
                                             uint32_t numThreads) {
    if (paths.size() < Indexer::MinFilesForThreading) {
        numThreads = 1;
    }

    // Populate/add to an instance of above struct
    std::vector<Indexer::IndexedPath> loadResults;
    loadResults.resize(paths.size());
    {
        // Thread the syntax tree creation portion
        if (numThreads != 1) {
            BS::thread_pool threadPool(numThreads);

            threadPool.detach_blocks(size_t(0), paths.size(), [&](size_t start, size_t end) {
                // Create a SourceManager and options per thread to avoid contention
                slang::SourceManager sourceManager;
                slang::Bag options;
                options.set(slang::parsing::PreprocessorOptions{.maxIncludeDepth = 0});

                for (size_t i = start; i < end; i++) {
                    populateIndexForSingleFile(paths[i], loadResults[i], sourceManager, options);
                }
            });
            threadPool.wait();
        }
        else {
            slang::SourceManager sourceManager;
            slang::Bag options;
            options.set(slang::parsing::PreprocessorOptions{.maxIncludeDepth = 0});

            for (size_t i = 0; i < paths.size(); ++i) {
                populateIndexForSingleFile(paths[i], loadResults[i], sourceManager, options);
            }
        }
    }

    return loadResults;
}

} // namespace

Indexer::Indexer() {
    notifyIndexingComplete();
}

const fs::path* Indexer::internUri(const fs::path& path) {
    auto [it, inserted] = uniqueUris.insert(path);
    return &(*it);
}

void Indexer::openDocument(const fs::path& path, const slang::syntax::SyntaxTree& tree) {
    IndexGuard guard(*this);

    IndexedPath indexedPath;
    extractDataFromTree(tree, indexedPath);

    // Store the indexed path for this document, but don't add to global index yet
    // Entries will be added to the global index when the document is saved
    openDocuments[path] = std::move(indexedPath);
}

void Indexer::updateDocument(const fs::path& path, const slang::syntax::SyntaxTree& tree) {
    IndexGuard guard(*this);

    // Extract new data
    IndexedPath newPath;
    extractDataFromTree(tree, newPath);

    // Intern the URI once for all operations
    const fs::path* uriPtr = internUri(path);

    // Get the old indexed path
    auto it = openDocuments.find(path);
    if (it == openDocuments.end()) {
        // Document wasn't open, just add all entries and store it
        for (const auto& item : newPath.symbols)
            symbolToFiles[item.name].push_back(GlobalSymbolLoc{.uri = uriPtr, .kind = item.kind});

        for (const auto& name : newPath.macros)
            macroToFiles[name].push_back(uriPtr);

        for (const auto& ref : newPath.referencedSymbols)
            symbolReferences[ref].push_back(uriPtr);

        openDocuments[path] = std::move(newPath);
        return;
    }

    // Make a copy of the old path to avoid issues with map modification
    IndexedPath oldPath = it->second;

    // Remove all old entries for this URI from the global index
    for (const auto& oldItem : oldPath.symbols) {
        auto mapIt = symbolToFiles.find(oldItem.name);
        if (mapIt != symbolToFiles.end()) {
            auto& vec = mapIt->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [&](const GlobalSymbolLoc& loc) {
                                         return loc.uri == uriPtr && loc.kind == oldItem.kind;
                                     }),
                      vec.end());
            if (vec.empty())
                symbolToFiles.erase(mapIt);
        }
    }

    for (const auto& oldMacro : oldPath.macros) {
        auto mapIt = macroToFiles.find(oldMacro);
        if (mapIt != macroToFiles.end()) {
            auto& vec = mapIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), uriPtr), vec.end());
            if (vec.empty())
                macroToFiles.erase(mapIt);
        }
    }

    for (const auto& oldRef : oldPath.referencedSymbols) {
        auto mapIt = symbolReferences.find(oldRef);
        if (mapIt != symbolReferences.end()) {
            auto& vec = mapIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), uriPtr), vec.end());
            if (vec.empty())
                symbolReferences.erase(mapIt);
        }
    }

    // Add all new entries to the global index
    for (const auto& newItem : newPath.symbols) {
        symbolToFiles[newItem.name].push_back(GlobalSymbolLoc{.uri = uriPtr, .kind = newItem.kind});
    }

    for (const auto& newMacro : newPath.macros) {
        macroToFiles[newMacro].push_back(uriPtr);
    }

    for (const auto& newRef : newPath.referencedSymbols) {
        symbolReferences[newRef].push_back(uriPtr);
    }

    // Replace the stored indexed path
    it->second = std::move(newPath);
}

void Indexer::closeDocument(const std::filesystem::path& uri) {
    IndexGuard guard(*this);

    // Just remove from open documents tracking
    // Keep the saved content in the global index
    openDocuments.erase(uri);
}

void Indexer::indexPath(IndexedPath& indexedFile) {
    const fs::path* uriPtr = internUri(indexedFile.path);

    for (const auto& item : indexedFile.symbols)
        symbolToFiles[item.name].push_back(GlobalSymbolLoc{.uri = uriPtr, .kind = item.kind});

    for (const auto& name : indexedFile.macros)
        macroToFiles[name].push_back(uriPtr);

    for (const auto& ref : indexedFile.referencedSymbols)
        symbolReferences[ref].push_back(uriPtr);
}

void Indexer::addDocuments(const std::vector<fs::path>& paths, uint32_t numThreads) {
    IndexGuard guard(*this);

    auto indexedPaths = indexPaths(paths, numThreads);
    for (auto& indexedFile : indexedPaths)
        indexPath(indexedFile);
}

void Indexer::removeDocuments(const std::vector<fs::path>& paths, uint32_t numThreads) {
    IndexGuard guard(*this);

    auto indexedPaths = indexPaths(paths, numThreads);
    for (const auto& entry : indexedPaths) {
        const fs::path* pathPtr = internUri(entry.path);

        // Remove symbols
        for (const auto& item : entry.symbols) {
            auto mapIt = symbolToFiles.find(item.name);
            if (mapIt != symbolToFiles.end()) {
                auto& vec = mapIt->second;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                                         [&](const GlobalSymbolLoc& loc) {
                                             return loc.uri == pathPtr && loc.kind == item.kind;
                                         }),
                          vec.end());
                if (vec.empty())
                    symbolToFiles.erase(mapIt);
            }
        }

        // Remove macros
        for (const auto& name : entry.macros) {
            auto mapIt = macroToFiles.find(name);
            if (mapIt != macroToFiles.end()) {
                auto& vec = mapIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), pathPtr), vec.end());
                if (vec.empty())
                    macroToFiles.erase(mapIt);
            }
        }

        // Remove references
        for (const auto& ref : entry.referencedSymbols) {
            auto mapIt = symbolReferences.find(ref);
            if (mapIt != symbolReferences.end()) {
                auto& vec = mapIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), pathPtr), vec.end());
                if (vec.empty())
                    symbolReferences.erase(mapIt);
            }
        }
    }
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
    auto it = symbolToFiles.find(std::string(name));

    std::vector<fs::path> result;
    if (it != symbolToFiles.end()) {
        for (const auto& entry : it->second)
            result.push_back(*entry.uri);
    }

    return result;
}

std::vector<fs::path> Indexer::getFilesForMacro(std::string_view name) const {
    auto it = macroToFiles.find(std::string(name));

    std::vector<fs::path> result;
    if (it != macroToFiles.end()) {
        for (const auto& path : it->second)
            result.push_back(*path);
    }

    return result;
}

std::vector<fs::path> Indexer::getFilesReferencingSymbol(std::string_view name) const {
    auto it = symbolReferences.find(std::string(name));

    std::vector<fs::path> result;
    if (it != symbolReferences.end()) {
        for (const auto& path : it->second)
            result.push_back(*path);
    }

    return result;
}

bool isSystemVerilogFile(const fs::path& path) {
    auto ext = path.extension().string();
    return ext == ".v" || ext == ".sv" || ext == ".svh" || ext == ".vh";
}

void collectFilesFromDirectory(const fs::path& dir, const std::vector<std::string>& excludeDirs,
                               std::vector<fs::path>& outFiles) {

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return;
    }
    auto startSize = outFiles.size();

    ScopedTimer t_index(fmt::format("Crawling {}", dir.string()));
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {

        // Check for exclude when entering a new directory
        if (it->is_directory(ec)) {
            // This map tends to be small; linear search is fine
            if (isExcluded(it->path().string(), excludeDirs)) {
                it.disable_recursion_pending();
                continue;
            }
        }
        else if (it->is_regular_file(ec) && isSystemVerilogFile(it->path())) {
            // Add SystemVerilog files
            outFiles.push_back(it->path());
        }
    }
    // check ec
    if (ec) {
        ERROR("Error while indexing directory {}: {}", dir.string(), ec.message());
    }
    INFO("Found {} files", outFiles.size() - startSize);
}

void Indexer::startIndexing(const std::vector<Config::IndexConfig>& indexConfigs,
                            std::optional<std::string_view> workspaceFolder, uint32_t numThreads) {

    resetIndexingComplete();

    std::vector<fs::path> pathsToIndex;

    if (indexConfigs.empty()) {
        // No index configs - index entire workspace
        if (workspaceFolder.has_value()) {
            collectFilesFromDirectory(fs::path(*workspaceFolder), {}, pathsToIndex);
        }
    }
    else {
        for (const auto& cfg : indexConfigs) {
            for (const auto& dir : cfg.dirs.value()) {
                fs::path fullDirPath;
                if (fs::path(dir).is_absolute()) {
                    fullDirPath = fs::path(dir);
                }
                else if (workspaceFolder.has_value()) {
                    fullDirPath = fs::path(*workspaceFolder) / fs::path(dir);
                }
                else {
                    continue;
                }
                collectFilesFromDirectory(
                    fullDirPath, cfg.excludeDirs.value().value_or(std::vector<std::string>{}),
                    pathsToIndex);
            }
        }
    }

    indexAndReport(pathsToIndex, numThreads);
}

void Indexer::startIndexing(const std::vector<std::string>& globs,
                            const std::vector<std::string>& excludeDirs, uint32_t numThreads) {
    resetIndexingComplete();

    std::vector<fs::path> pathsToIndex;
    for (const auto& pattern : globs) {
        ScopedTimer t_glob("Globbing " + pattern);
        size_t beginCount = pathsToIndex.size();

        slang::SmallVector<fs::path> out;
        std::error_code ec;
        svGlob({}, pattern, slang::GlobMode::Files, out, true, ec);

        if (ec) {
            ERROR("Error globbing pattern {}: {}", pattern, ec.message());
            continue;
        }

        for (const auto& path : out) {
            if (!isExcluded(path.string(), excludeDirs))
                pathsToIndex.push_back(path);
        }
        INFO("Found {} files from pattern {}", pathsToIndex.size() - beginCount, pattern);
    }

    indexAndReport(pathsToIndex, numThreads);
}

void Indexer::indexAndReport(std::vector<fs::path> pathsToIndex, uint32_t numThreads) {
    INFO("Indexing {} files", pathsToIndex.size());

    {
        ScopedTimer t_index("Slang Indexing");
        addDocuments(pathsToIndex, numThreads);
    }

    // Estimate memory usage
    size_t symbolsSize = 0;
    for (const auto& [name, entries] : symbolToFiles) {
        symbolsSize += sizeof(
                           std::pair<const std::string, slang::SmallVector<GlobalSymbolLoc, 2>>) +
                       name.capacity();
        symbolsSize += entries.size() * sizeof(GlobalSymbolLoc);
    }

    size_t macrosSize = 0;
    for (const auto& [name, uris] : macroToFiles) {
        macrosSize += sizeof(std::pair<const std::string, slang::SmallVector<const fs::path*, 2>>) +
                      name.capacity();
        macrosSize += uris.size() * sizeof(const fs::path*);
    }

    size_t refsSize = 0;
    for (const auto& [name, uris] : symbolReferences) {
        refsSize += sizeof(std::pair<const std::string, slang::SmallVector<const fs::path*, 2>>) +
                    name.capacity();
        refsSize += uris.size() * sizeof(const fs::path*);
    }

    // Count unique URIs storage
    size_t urisSize = 0;
    for (const auto& uri : uniqueUris) {
        urisSize += sizeof(URI) + uri.string().capacity();
    }

    INFO("Indexing complete: {} symbols (~{} KB), {} macros (~{} KB), {} references (~{} KB), {} "
         "unique URIs (~{} KB)",
         symbolToFiles.size(), symbolsSize / 1024, macroToFiles.size(), macrosSize / 1024,
         symbolReferences.size(), refsSize / 1024, uniqueUris.size(), urisSize / 1024);

    notifyIndexingComplete();
}

void Indexer::waitForIndexingCompletion() const {
    std::unique_lock<std::mutex> lock(indexingMutex);
    indexingCondition.wait(lock, [this] { return indexingComplete; });
}

void Indexer::notifyIndexingComplete() {
    {
        std::lock_guard<std::mutex> lock(indexingMutex);
        indexingComplete = true;
    }
    indexingCondition.notify_all();
}

void Indexer::resetIndexingComplete() {
    std::lock_guard<std::mutex> lock(indexingMutex);
    indexingComplete = false;
}
