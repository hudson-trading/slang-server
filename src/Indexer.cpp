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
#include <limits>
#include <string_view>
#include <unordered_map>

#include "slang/driver/SourceLoader.h"
#include "slang/parsing/Parser.h"
#include "slang/parsing/ParserMetadata.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"
#include "slang/util/OS.h"
#include "slang/util/SmallMap.h"
#include "slang/util/Util.h"

namespace fs = std::filesystem;

namespace {

std::optional<fs::path> nearestInclude(std::string_view spelling, const fs::path& source,
                                       std::span<const fs::path* const> candidates) {
    auto includePath = fs::path(spelling).lexically_normal();
    if (includePath.empty() || includePath.is_absolute() ||
        std::ranges::any_of(includePath, [](const auto& part) { return part == ".."; }))
        return std::nullopt;

    std::optional<fs::path> nearest;
    fs::path nearestIdentity;
    size_t nearestDistance = std::numeric_limits<size_t>::max();
    for (const auto* candidate : candidates) {
        auto remaining = candidate->lexically_normal();
        auto suffix = includePath;
        while (!suffix.empty() && suffix.filename() == remaining.filename()) {
            suffix = suffix.parent_path();
            remaining = remaining.parent_path();
        }
        if (!suffix.empty())
            continue;

        std::error_code ec;
        if (!fs::is_regular_file(*candidate, ec))
            continue;
        auto path = fs::weakly_canonical(*candidate, ec);
        if (ec)
            continue;
        auto relative = path.parent_path().lexically_relative(source.parent_path());
        if (relative.empty())
            continue;
        auto distance = size_t(
            std::ranges::count_if(relative, [](const auto& part) { return part != "."; }));
        if (distance < nearestDistance || (distance == nearestDistance && path < nearestIdentity)) {
            nearest = *candidate;
            nearestIdentity = std::move(path);
            nearestDistance = distance;
        }
    }
    return nearest;
}

} // namespace

void Indexer::extractFromRoot(const slang::syntax::CompilationUnitSyntax& root,
                              const slang::parsing::ParserMetadata& meta, IndexedPath& dest) {
    using namespace slang::syntax;

    // Extract top-level symbols
    for (auto* member : root.members) {
        if (ModuleDeclarationSyntax::isKind(member->kind)) {
            auto& decl = member->as<ModuleDeclarationSyntax>();
            std::string_view name = decl.header->name.valueText();
            if (!name.empty()) {
                dest.symbols.push_back(GlobalSymbol{.name = std::string(name), .kind = decl.kind});
            }
        }
    }

    // Extract referenced symbols from metadata
    slang::SmallSet<std::string_view, 8> seenDeps;
    meta.visitReferencedSymbols([&](std::string_view name) {
        if (seenDeps.insert(name).second)
            dest.referencedSymbols.push_back(std::string{name});
    });
}

void Indexer::extractHeaderSymbols(const slang::syntax::CompilationUnitSyntax& root,
                                   slang::BufferID buffer, const fs::path& path,
                                   IndexedPath& dest) {
    using namespace slang::syntax;
    bool hasTopLevelClass = std::ranges::any_of(root.members, [&](const auto* member) {
        auto* decl = member->template as_if<ClassDeclarationSyntax>();
        return decl && decl->name.location().buffer() == buffer;
    });
    auto ext = path.extension();
    if (!hasTopLevelClass && ext != ".svh" && ext != ".vh")
        return;

    auto add = [&](slang::parsing::Token token) {
        if (token.location().buffer() != buffer || token.valueText().empty())
            return;
        std::string name(token.valueText());
        if (std::ranges::find(dest.headerSymbols, name) == dest.headerSymbols.end())
            dest.headerSymbols.push_back(std::move(name));
    };
    for (auto* member : root.members) {
        if (auto* decl = member->as_if<ClassDeclarationSyntax>())
            add(decl->name);
        else if (auto* decl = member->as_if<TypedefDeclarationSyntax>())
            add(decl->name);
        else if (auto* decl = member->as_if<FunctionDeclarationSyntax>()) {
            if (auto* name = decl->prototype->name->as_if<IdentifierNameSyntax>())
                add(name->identifier);
        }
        else if (auto* decl = member->as_if<DataDeclarationSyntax>()) {
            for (auto* declarator : decl->declarators)
                add(declarator->name);
        }
    }
}

template<typename MacroRange>
void Indexer::extractMacros(const MacroRange& macros, IndexedPath& dest) {
    for (const auto* macro : macros) {
        if (!macro)
            continue;

        // Only add macros defined in this file (not included files)
        if (macro->name.location() == slang::SourceLocation::NoLocation)
            continue;

        dest.macros.push_back(std::string(macro->name.valueText()));
    }
}

std::vector<Indexer::IndexedPath> Indexer::indexPaths(const std::vector<fs::path>& paths) const {
    using namespace slang;
    using namespace parsing;

    uint32_t numThreads = numThreads_;
    if (paths.size() < MinFilesForThreading) {
        numThreads = 1;
    }

    std::vector<IndexedPath> loadResults;
    loadResults.resize(paths.size());

    // Lambda that processes a range of files
    // Creates its own SourceManager and options to avoid contention when threaded
    auto processRange = [&loadResults, &paths](size_t start, size_t end) {
        SourceManager sourceManager;
        Bag options;
        options.set(PreprocessorOptions{.maxIncludeDepth = 0});

        SmallVector<char> bufferData;
        for (size_t i = start; i < end; i++) {
            auto& dest = loadResults[i];
            bufferData.clear();
            if (std::error_code ec = OS::readFile(paths[i], bufferData)) {
                continue;
            }

            SourceBuffer buffer{.data = std::string_view(bufferData.data(), bufferData.size()),
                                .id = BufferID::getPlaceholder()};

            BumpAllocator alloc;
            Diagnostics diagnostics;
            Preprocessor preprocessor(sourceManager, alloc, diagnostics, options, {});
            preprocessor.pushSource(buffer);
            Parser parser(preprocessor, options);

            auto& root = parser.parseCompilationUnit();
            const auto& meta = parser.getMetadata();
            for (const auto& include : preprocessor.getMetadata().includeDirectives) {
                if (!include.isSystem)
                    dest.includes.emplace_back(include.path);
            }
            extractHeaderSymbols(root, root.getFirstToken().location().buffer(), paths[i], dest);

            // Extract macros only if no global symbols were found (header files)
            if (!meta.nodeMeta.empty()) {
                extractFromRoot(root, meta, dest);
            }
            else if (meta.classDecls.empty()) {
                // If an svh file contains a class, it's likely actually included in a package
                extractMacros(preprocessor.getDefinedMacros(), dest);
            }
        }
    };

    if (numThreads != 1) {
        BS::thread_pool threadPool(numThreads);
        threadPool.detach_blocks(size_t(0), paths.size(), processRange);
        threadPool.wait();
    }
    else {
        processRange(0, paths.size());
    }

    return loadResults;
}

Indexer::Indexer() = default;

const fs::path* Indexer::internUri(const fs::path& path) {
    auto [it, inserted] = uniqueUris_.insert(path);
    return &(*it);
}

void Indexer::updateDocument(const fs::path& path, const slang::syntax::SyntaxTree& tree) {
    IndexWriteGuard guard(*this);

    // Extract new data
    IndexedPath newPath;
    extractFromRoot(tree.root().as<slang::syntax::CompilationUnitSyntax>(), tree.getMetadata(),
                    newPath);

    // Extract macros only if no global symbols were found (header files)
    if (newPath.symbols.empty()) {
        extractMacros(tree.getDefinedMacros(), newPath);
    }

    for (const auto& include : tree.getIncludeDirectives()) {
        if (!include.isSystem && tree.sourceManager()
                                         .getFullyExpandedLoc(include.syntax->sourceRange().start())
                                         .buffer() == tree.getSourceBufferIds()[0])
            newPath.includes.emplace_back(include.path);
    }
    extractHeaderSymbols(tree.root().as<slang::syntax::CompilationUnitSyntax>(),
                         tree.getSourceBufferIds()[0], path, newPath);

    indexPath(path, newPath);
}

void Indexer::replaceIncludes(const fs::path& parent, std::vector<fs::path> targets) {
    resolvedIncluders_.clear();
    if (auto old = includedFiles_.find(parent); old != includedFiles_.end()) {
        for (const auto& target : old->second) {
            auto it = includers_.find(target);
            if (it == includers_.end())
                continue;
            std::erase(it->second, parent);
            if (it->second.empty())
                includers_.erase(it);
        }
        includedFiles_.erase(old);
    }
    std::ranges::sort(targets);
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    for (const auto& target : targets)
        includers_[target].push_back(parent);
    includedFiles_[parent] = std::move(targets);
}

void Indexer::updateIncludes(const slang::syntax::SyntaxTree& tree) {
    IndexWriteGuard guard(*this);
    const auto& sm = tree.sourceManager();
    std::unordered_map<fs::path, std::vector<fs::path>> includes;
    for (auto buffer : tree.getSourceBufferIds()) {
        const auto& path = sm.getFullPath(buffer);
        if (!path.empty())
            includes.try_emplace(path);
    }
    for (const auto& include : tree.getIncludeDirectives()) {
        if (!include.buffer)
            continue;
        auto location = sm.getFullyExpandedLoc(include.syntax->sourceRange().start());
        includes[sm.getFullPath(location.buffer())].push_back(sm.getFullPath(include.buffer.id));
    }
    for (auto& [path, targets] : includes)
        replaceIncludes(path, std::move(targets));
}

void Indexer::indexPath(const fs::path& path, IndexedPath& indexedFile) {
    const fs::path* uriPtr = internUri(path);
    removePathFromIndex(uriPtr);
    indexedFile.path = uriPtr;

    for (const auto& item : indexedFile.symbols)
        symbolToFiles_[item.name].push_back(GlobalSymbolLoc{.uri = uriPtr, .kind = item.kind});

    for (const auto& name : indexedFile.macros)
        macroToFiles_[name].push_back(uriPtr);

    for (const auto& ref : indexedFile.referencedSymbols)
        symbolReferences_[ref].push_back(uriPtr);

    for (const auto& name : indexedFile.headerSymbols)
        headerSymbolToFiles_[name].push_back(uriPtr);

    slang::SmallSet<std::string, 4> includeNames;
    for (const auto& spelling : indexedFile.includes) {
        auto name = fs::path(spelling).filename().string();
        if (includeNames.insert(name).second)
            includeReferences_[name].push_back(uriPtr);
    }

    // Store the indexed path for efficient removal later
    indexedFiles[uriPtr] = std::move(indexedFile);
}

void Indexer::addDocuments(const std::vector<fs::path>& paths) {
    IndexWriteGuard guard(*this);

    auto indexedPaths = indexPaths(paths);
    for (size_t i = 0; i < indexedPaths.size(); ++i)
        indexPath(paths[i], indexedPaths[i]);
}

void Indexer::removePathFromIndex(const fs::path* pathPtr) {
    resolvedIncluders_.clear();
    if (!includedFiles_.empty()) {
        std::error_code ec;
        auto canonical = fs::weakly_canonical(*pathPtr, ec);
        if (!ec && includedFiles_.contains(canonical)) {
            replaceIncludes(canonical, {});
            includedFiles_.erase(canonical);
        }
    }

    // Look up the stored IndexedPath for targeted removal of symbols
    auto it = indexedFiles.find(pathPtr);
    if (it == indexedFiles.end())
        return;

    const IndexedPath& entry = it->second;

    for (const auto& spelling : entry.includes) {
        auto references = includeReferences_.find(fs::path(spelling).filename().string());
        if (references == includeReferences_.end())
            continue;
        auto& paths = references->second;
        paths.erase(std::remove(paths.begin(), paths.end(), pathPtr), paths.end());
        if (paths.empty())
            includeReferences_.erase(references);
    }

    for (const auto& name : entry.headerSymbols) {
        auto headerIt = headerSymbolToFiles_.find(name);
        if (headerIt == headerSymbolToFiles_.end())
            continue;
        auto& paths = headerIt->second;
        paths.erase(std::remove(paths.begin(), paths.end(), pathPtr), paths.end());
        if (paths.empty())
            headerSymbolToFiles_.erase(headerIt);
    }

    // Remove symbols
    for (const auto& item : entry.symbols) {
        auto mapIt = symbolToFiles_.find(item.name);
        if (mapIt != symbolToFiles_.end()) {
            auto& vec = mapIt->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [&](const GlobalSymbolLoc& loc) {
                                         return loc.uri == pathPtr && loc.kind == item.kind;
                                     }),
                      vec.end());
            if (vec.empty())
                symbolToFiles_.erase(mapIt);
        }
    }

    // Remove macros
    for (const auto& name : entry.macros) {
        auto mapIt = macroToFiles_.find(name);
        if (mapIt != macroToFiles_.end()) {
            auto& vec = mapIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), pathPtr), vec.end());
            if (vec.empty())
                macroToFiles_.erase(mapIt);
        }
    }

    // Remove references
    for (const auto& ref : entry.referencedSymbols) {
        auto mapIt = symbolReferences_.find(ref);
        if (mapIt != symbolReferences_.end()) {
            auto& vec = mapIt->second;
            vec.erase(std::remove(vec.begin(), vec.end(), pathPtr), vec.end());
            if (vec.empty())
                symbolReferences_.erase(mapIt);
        }
    }

    // Remove from indexedFiles
    indexedFiles.erase(it);
}

bool isExcluded(const std::string& path, const std::vector<std::string>& excludeDirs) {
    for (const auto& dir : excludeDirs) {
        if (path.find(dir) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<fs::path> Indexer::getFilesForSymbol(std::string_view name) const {
    IndexReadGuard guard(*this);

    auto it = symbolToFiles_.find(std::string(name));

    std::vector<fs::path> result;
    if (it != symbolToFiles_.end()) {
        for (const auto& entry : it->second)
            result.push_back(*entry.uri);
    }

    return result;
}

bool Indexer::hasPotentialIncluders(const fs::path& canonicalPath) const {
    IndexReadGuard guard(*this);
    return includeReferences_.contains(canonicalPath.filename().string()) ||
           includers_.contains(canonicalPath);
}

std::vector<fs::path> Indexer::getFilesIncluding(const fs::path& path) {
    std::error_code ec;
    auto canonicalPath = fs::weakly_canonical(path, ec);
    if (ec)
        return {};
    IndexWriteGuard guard(*this);
    if (auto cached = resolvedIncluders_.find(canonicalPath); cached != resolvedIncluders_.end())
        return cached->second;

    std::vector<fs::path> result;
    if (auto parsed = includers_.find(canonicalPath); parsed != includers_.end())
        result = parsed->second;
    auto name = canonicalPath.filename().string();
    if (auto references = includeReferences_.find(name); references != includeReferences_.end()) {
        for (const auto* source : references->second) {
            auto parent = fs::weakly_canonical(*source, ec);
            if (ec || includedFiles_.contains(parent))
                continue;
            for (const auto& spelling : indexedFiles.at(source).includes) {
                if (fs::path(spelling).filename() != name)
                    continue;
                auto target = parent.parent_path() / spelling;
                if (!fs::is_regular_file(target, ec)) {
                    auto candidates = includeToFiles_.find(name);
                    if (candidates == includeToFiles_.end())
                        continue;
                    auto nearest = nearestInclude(spelling, parent, candidates->second);
                    if (!nearest)
                        continue;
                    target = *nearest;
                }
                target = fs::weakly_canonical(target, ec);
                if (!ec && target == canonicalPath) {
                    result.push_back(std::move(parent));
                    break;
                }
            }
        }
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    resolvedIncluders_.emplace(canonicalPath, result);
    return result;
}

std::optional<fs::path> Indexer::getNearestFileForInclude(std::string_view path,
                                                          const fs::path& source) const {
    IndexReadGuard guard(*this);
    auto it = includeToFiles_.find(fs::path(path).filename().string());
    if (it == includeToFiles_.end())
        return std::nullopt;
    return nearestInclude(path, source, it->second);
}

std::vector<fs::path> Indexer::getHeadersForSymbol(std::string_view name) const {
    IndexReadGuard guard(*this);
    std::vector<fs::path> result;
    if (auto it = headerSymbolToFiles_.find(std::string(name)); it != headerSymbolToFiles_.end()) {
        for (const auto* path : it->second)
            result.push_back(*path);
    }
    std::ranges::sort(result);
    return result;
}

std::vector<fs::path> Indexer::getFilesForInclude(std::string_view path) const {
    auto includePath = fs::path(path).lexically_normal();
    if (includePath.empty() || includePath.is_absolute())
        return {};
    for (const auto& part : includePath) {
        if (part == "..")
            return {};
    }

    IndexReadGuard guard(*this);
    auto it = includeToFiles_.find(includePath.filename().string());
    if (it == includeToFiles_.end())
        return {};

    std::vector<fs::path> result;
    for (const auto* candidate : it->second) {
        auto remaining = candidate->lexically_normal();
        auto suffix = includePath;
        while (!suffix.empty() && suffix.filename() == remaining.filename()) {
            suffix = suffix.parent_path();
            remaining = remaining.parent_path();
        }
        if (suffix.empty())
            result.push_back(*candidate);
    }
    std::ranges::sort(result);
    return result;
}

std::vector<fs::path> Indexer::getFilesForMacro(std::string_view name) const {
    IndexReadGuard guard(*this);

    auto it = macroToFiles_.find(std::string(name));

    std::vector<fs::path> result;
    if (it != macroToFiles_.end()) {
        for (const auto& path : it->second)
            result.push_back(*path);
    }

    return result;
}

std::vector<fs::path> Indexer::getFilesReferencingSymbol(std::string_view name) const {
    IndexReadGuard guard(*this);

    auto it = symbolReferences_.find(std::string(name));

    std::vector<fs::path> result;
    if (it != symbolReferences_.end()) {
        for (const auto& path : it->second)
            result.push_back(*path);
    }

    return result;
}

std::optional<Indexer::GlobalSymbolLoc> Indexer::getFirstSymbolLoc(std::string_view name) const {
    IndexReadGuard guard(*this);

    auto it = symbolToFiles_.find(std::string(name));
    if (it == symbolToFiles_.end() || it->second.empty()) {
        return std::nullopt;
    }
    return it->second[0];
}

std::vector<std::string> Indexer::getAllMacroNames() const {
    IndexReadGuard guard(*this);

    std::vector<std::string> result;
    result.reserve(macroToFiles_.size());
    for (const auto& [name, _] : macroToFiles_) {
        result.push_back(name);
    }
    return result;
}

size_t Indexer::getSymbolCount() const {
    IndexReadGuard guard(*this);
    return symbolToFiles_.size();
}

bool isSystemVerilogFile(const fs::path& path) {
    auto ext = path.extension().string();
    return ext == ".sv" || ext == ".svh" || ext == ".v" || ext == ".vh";
}

void Indexer::onWorkspaceDidChangeWatchedFiles(const lsp::DidChangeWatchedFilesParams& params) {
    IndexWriteGuard guard(*this);

    std::vector<fs::path> pathsToAdd;

    for (const auto& change : params.changes) {
        fs::path path = change.uri.getPath();

        switch (change.type) {
            case lsp::FileChangeType::Created: {
                // Add new file to index
                pathsToAdd.push_back(path);
                break;
            }
            case lsp::FileChangeType::Changed: {
                // Re-index the file: remove old entries, add new ones
                removePathFromIndex(internUri(path));

                // Re-add with new content
                if (fs::exists(path))
                    pathsToAdd.push_back(path);
                break;
            }
            case lsp::FileChangeType::Deleted: {
                // Remove all entries for this file
                removePathFromIndex(internUri(path));
                break;
            }
        }
    }

    // Parse all new/changed files potentially in thread pool
    if (!pathsToAdd.empty()) {
        auto indexedPaths = indexPaths(pathsToAdd);
        for (size_t i = 0; i < indexedPaths.size(); ++i)
            indexPath(pathsToAdd[i], indexedPaths[i]);
    }
}

void Indexer::collectFilesFromDirectory(const fs::path& dir,
                                        const std::vector<std::string>& excludeDirs,
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
                            std::optional<std::string_view> workspaceFolder) {
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

    indexAndReport(pathsToIndex);
}

void Indexer::startIndexing(const std::vector<std::string>& globs,
                            const std::vector<std::string>& excludeDirs) {
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

    indexAndReport(pathsToIndex);
}

void Indexer::indexAndReport(std::vector<fs::path> pathsToIndex) {
    INFO("Indexing {} files", pathsToIndex.size());

    IndexWriteGuard guard(*this);
    ScopedTimer t_index("Workspace indexing");
    includeReferences_.clear();
    includers_.clear();
    includedFiles_.clear();
    resolvedIncluders_.clear();
    {
        ScopedTimer t_parse("Slang parsing and metadata indexing");
        auto indexedPaths = indexPaths(pathsToIndex);
        for (size_t i = 0; i < indexedPaths.size(); ++i)
            indexPath(pathsToIndex[i], indexedPaths[i]);
    }

    includeToFiles_.clear();
    for (const auto& path : pathsToIndex) {
        const auto* interned = internUri(fs::absolute(path).lexically_normal());
        auto& paths = includeToFiles_[interned->filename().string()];
        if (std::ranges::find(paths, interned) == paths.end())
            paths.push_back(interned);
    }

    // Estimate memory usage
    size_t symbolsSize = 0;
    for (const auto& [name, entries] : symbolToFiles_) {
        symbolsSize += sizeof(
                           std::pair<const std::string, slang::SmallVector<GlobalSymbolLoc, 2>>) +
                       name.capacity();
        symbolsSize += entries.size() * sizeof(GlobalSymbolLoc);
    }

    size_t macrosSize = 0;
    for (const auto& [name, uris] : macroToFiles_) {
        macrosSize += sizeof(std::pair<const std::string, slang::SmallVector<const fs::path*, 2>>) +
                      name.capacity();
        macrosSize += uris.size() * sizeof(const fs::path*);
    }

    size_t refsSize = 0;
    for (const auto& [name, uris] : symbolReferences_) {
        refsSize += sizeof(std::pair<const std::string, slang::SmallVector<const fs::path*, 2>>) +
                    name.capacity();
        refsSize += uris.size() * sizeof(const fs::path*);
    }

    // Count unique URIs storage
    size_t urisSize = 0;
    for (const auto& uri : uniqueUris_) {
        urisSize += sizeof(URI) + uri.string().capacity();
    }

    INFO("Indexing complete: {} symbols (~{} KB), {} macros (~{} KB), {} references (~{} KB), {} "
         "unique URIs (~{} KB)",
         symbolToFiles_.size(), symbolsSize / 1024, macroToFiles_.size(), macrosSize / 1024,
         symbolReferences_.size(), refsSize / 1024, uniqueUris_.size(), urisSize / 1024);
}
