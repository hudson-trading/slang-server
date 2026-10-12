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
#include "util/Process.h"
#include <BS_thread_pool.hpp>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <iterator>
#include <limits>
#include <map>
#include <set>
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

// Borrows stable strings, keeping short exclusion lists out of the hash table.
class ExclusionSet {
public:
    explicit ExclusionSet(std::span<const std::string> values) : values(values) {
        if (values.size() > 8) {
            hashed.reserve(values.size());
            for (const auto& value : values)
                hashed.insert(value);
        }
    }

    bool empty() const { return values.empty(); }

    bool contains(std::string_view value) const {
        if (hashed.empty())
            return std::ranges::find(values, value) != values.end();
        return hashed.contains(value);
    }

    // Both the stored directories and the query have normalized, trailing '/' separators.
    bool containsDirectoryOrAncestor(std::string_view path) const {
        // Walking and hashing every ancestor costs more than a few prefix comparisons.
        if (values.size() <= 16) {
            return std::ranges::any_of(values,
                                       [&](const auto& value) { return path.starts_with(value); });
        }
        while (!path.empty()) {
            if (hashed.contains(path))
                return true;
            path.remove_suffix(1);
            const auto separator = path.find_last_of('/');
            if (separator == std::string_view::npos)
                break;
            path = path.substr(0, separator + 1);
        }
        return false;
    }

private:
    std::span<const std::string> values;
    slang::flat_hash_set<std::string_view> hashed;
};

template<typename T, typename GetPath>
std::vector<const T*> nearestFiles(const fs::path& source, std::span<const T> candidates,
                                   GetPath getPath) {
    std::error_code ec;
    auto sourcePath = fs::weakly_canonical(source, ec);
    if (ec)
        return {};

    std::vector<std::pair<fs::path, const T*>> nearest;
    size_t nearestDistance = std::numeric_limits<size_t>::max();
    for (const auto& candidate : candidates) {
        auto* candidatePath = getPath(candidate);
        if (!candidatePath)
            continue;
        auto path = fs::weakly_canonical(*candidatePath, ec);
        if (ec)
            continue;
        auto relative = path.parent_path().lexically_relative(sourcePath.parent_path());
        if (relative.empty())
            continue;
        auto distance = size_t(
            std::ranges::count_if(relative, [](const auto& part) { return part != "."; }));
        if (distance < nearestDistance) {
            nearest.clear();
            nearestDistance = distance;
        }
        if (distance == nearestDistance)
            nearest.emplace_back(std::move(path), &candidate);
    }
    std::ranges::sort(nearest, {}, [](const auto& entry) -> const auto& { return entry.first; });
    std::vector<const T*> result;
    for (size_t i = 0; i < nearest.size(); ++i) {
        if (i == 0 || nearest[i].first != nearest[i - 1].first)
            result.push_back(nearest[i].second);
    }
    return result;
}

std::optional<fs::path> nearestInclude(std::string_view spelling, const fs::path& source,
                                       std::span<const fs::path* const> candidates) {
    auto includePath = fs::path(spelling).lexically_normal();
    if (includePath.empty() || includePath.is_absolute() ||
        std::ranges::any_of(includePath, [](const auto& part) { return part == ".."; }))
        return std::nullopt;

    auto nearest = nearestFiles(source, candidates, [&](const fs::path* candidate) {
        auto remaining = candidate->lexically_normal();
        auto suffix = includePath;
        while (!suffix.empty() && suffix.filename() == remaining.filename()) {
            suffix = suffix.parent_path();
            remaining = remaining.parent_path();
        }
        std::error_code ec;
        return suffix.empty() && fs::is_regular_file(*candidate, ec) ? candidate : nullptr;
    });
    return nearest.empty() ? std::nullopt : std::optional<fs::path>(**nearest.front());
}

} // namespace

void Indexer::extractFromRoot(const slang::syntax::CompilationUnitSyntax& root,
                              const slang::parsing::ParserMetadata& meta,
                              const slang::SourceManager& sourceManager,
                              slang::BufferID primaryBuffer, IndexedPath& dest) {
    using namespace slang::syntax;

    // Extract top-level symbols
    for (auto* member : root.members) {
        if (ModuleDeclarationSyntax::isKind(member->kind)) {
            auto& decl = member->as<ModuleDeclarationSyntax>();
            if (primaryBuffer != slang::BufferID::getPlaceholder() &&
                sourceManager.getFullyExpandedLoc(decl.header->name.location()).buffer() !=
                    primaryBuffer)
                continue;
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
void Indexer::extractMacros(const MacroRange& macros, const slang::SourceManager& sourceManager,
                            slang::BufferID primaryBuffer, IndexedPath& dest) {
    for (const auto* macro : macros) {
        if (!macro)
            continue;

        if (macro->name.location() == slang::SourceLocation::NoLocation ||
            (primaryBuffer != slang::BufferID::getPlaceholder() &&
             sourceManager.getFullyExpandedLoc(macro->name.location()).buffer() != primaryBuffer))
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
                extractFromRoot(root, meta, sourceManager, buffer.id, dest);
            }
            else if (meta.classDecls.empty()) {
                // If an svh file contains a class, it's likely actually included in a package
                extractMacros(preprocessor.getDefinedMacros(), sourceManager, buffer.id, dest);
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
                    tree.sourceManager(), tree.getSourceBufferIds()[0], newPath);

    // Extract macros only if no global symbols were found (header files)
    if (newPath.symbols.empty()) {
        extractMacros(tree.getDefinedMacros(), tree.sourceManager(), tree.getSourceBufferIds()[0],
                      newPath);
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

std::vector<fs::path> Indexer::getIncludeDirectories() const {
    IndexReadGuard guard(*this);
    return includeDirectories_;
}

std::vector<fs::path> Indexer::getSuggestedIndexDirectories(const fs::path& workspace) const {
    IndexReadGuard guard(*this);
    if (directoryEntryCounts_.empty())
        return {};
    const auto root = fs::absolute(workspace).lexically_normal();
    struct Directory {
        size_t entries = 0;
        bool hasSources = false;
        std::map<fs::path, Directory> children;
    } tree;
    auto addDirectory = [&](const fs::path& path) -> Directory* {
        auto relative = fs::absolute(path).lexically_normal().lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            return nullptr;
        auto* node = &tree;
        for (const auto& part : relative) {
            if (!part.empty() && part != ".")
                node = &node->children[part];
        }
        return node;
    };
    size_t workspaceEntries = 0;
    for (const auto& [path, entries] : directoryEntryCounts_) {
        auto* directory = addDirectory(path);
        if (directory) {
            directory->entries = entries;
            workspaceEntries += entries;
        }
    }
    for (const auto& [_, files] : includeToFiles_) {
        for (const auto* file : files) {
            auto* directory = addDirectory(file->parent_path());
            if (!directory)
                continue;
            if (directory == &tree)
                return {};
            directory->hasSources = true;
        }
    }
    struct Selection {
        size_t entries = 0;
        size_t cost = 0;
        std::vector<fs::path> directories;
    };
    // Each extra root must save roughly 5% of the workspace crawl, with a floor for small trees.
    const auto rootPenalty = std::max(size_t(32), workspaceEntries / 20);
    auto select = [rootPenalty](auto&& self, const Directory& directory,
                                const fs::path& path) -> Selection {
        Selection result{.entries = directory.entries};
        for (const auto& [name, child] : directory.children) {
            auto selected = self(self, child, path / name);
            result.entries += selected.entries;
            result.cost += selected.cost;
            result.directories.insert(result.directories.end(),
                                      std::make_move_iterator(selected.directories.begin()),
                                      std::make_move_iterator(selected.directories.end()));
        }
        auto parentCost = result.entries + rootPenalty;
        if (!path.empty() &&
            (directory.hasSources || (!result.directories.empty() && parentCost <= result.cost))) {
            result.cost = parentCost;
            result.directories = {path};
        }
        return result;
    };
    return select(select, tree, {}).directories;
}

std::vector<Indexer::GitIgnoreMatch> Indexer::getGitIgnoreMatches(
    const fs::path& workspace, std::span<const Config::IndexConfig> indexConfigs) const {
    const auto root = fs::absolute(workspace).lexically_normal();
    std::vector<std::string> files;
    std::set<std::string> directories;
    std::unordered_set<std::string> protectedNames;
    auto protectNames = [&](const fs::path& directory) {
        for (auto parent = directory; parent.has_relative_path(); parent = parent.parent_path())
            protectedNames.insert(parent.filename().string());
    };
    protectNames(root);
    auto addParents = [](const fs::path& path, auto& into) {
        for (auto parent = path.parent_path(); !parent.empty() && parent != ".";
             parent = parent.parent_path())
            into.insert(parent.generic_string() + '/');
    };
    {
        IndexReadGuard guard(*this);
        for (const auto& [_, paths] : includeToFiles_) {
            for (const auto* path : paths) {
                auto relative = path->lexically_relative(root);
                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") {
                    files.push_back(relative.generic_string());
                    addParents(relative, directories);
                }
                else {
                    // A name exclusion may share an index entry with external source roots.
                    protectNames(path->parent_path());
                }
            }
        }
        for (const auto& [directory, _] : directoryEntryCounts_) {
            auto relative = directory.lexically_relative(root);
            if (!relative.empty() && relative != "." && !relative.is_absolute() &&
                *relative.begin() != "..") {
                directories.insert((relative / "").generic_string());
                addParents(relative, directories);
            }
        }
    }
    for (const auto& entry : indexConfigs) {
        for (const auto& exclusion :
             entry.excludeDirs.value().value_or(std::vector<std::string>{})) {
            if ((!exclusion.starts_with("./") && !fs::path(exclusion).is_absolute()) ||
                exclusion.find_first_of("*?") != std::string::npos)
                continue;
            auto relative = (root / exclusion).lexically_normal().lexically_relative(root);
            if (!relative.empty() && relative != "." && !relative.is_absolute() &&
                *relative.begin() != "..") {
                directories.insert((relative / "").generic_string());
                addParents(relative, directories);
            }
        }
    }
    if (files.empty() && directories.empty())
        return {};
    std::ranges::sort(files);

    const std::vector<std::string> listArguments{"git",     "-C", root.string(), "ls-files",
                                                 "--stage", "-z", "--"};
    auto tracked = server::runProcess(listArguments);
    if (!tracked || tracked->exitCode != 0)
        return {};
    std::vector<std::string> submodules;
    std::unordered_set<std::string_view> trackedFiles;
    std::unordered_set<std::string> protectedDirectories;
    std::string_view records = tracked->output;
    while (!records.empty()) {
        auto end = records.find('\0');
        if (end == std::string_view::npos)
            return {};
        auto record = records.substr(0, end);
        auto separator = record.find('\t');
        if (separator != std::string_view::npos) {
            auto path = record.substr(separator + 1);
            trackedFiles.insert(path);
            addParents(fs::path(path), protectedDirectories);
            if (record.starts_with("160000 ")) {
                submodules.push_back(std::string(path) + '/');
                protectedDirectories.insert(std::string(path) + '/');
            }
        }
        records.remove_prefix(end + 1);
    }

    // check-ignore rejects paths inside submodules; their ignore rules belong to another repo.
    std::string input;
    std::set<std::string> queries(files.begin(), files.end());
    queries.insert(directories.begin(), directories.end());
    for (const auto& file : queries) {
        if (trackedFiles.contains(file) || protectedDirectories.contains(file))
            continue;
        if (std::ranges::none_of(submodules, [&](const auto& directory) {
                return file.starts_with(directory);
            })) {
            input += file;
            input += '\0';
        }
    }
    if (input.empty())
        return {};
    // The snapshot above already protects tracked paths. Without --no-index, Git searches its
    // index again for every input path, which is expensive in large repositories.
    const std::vector<std::string> arguments{"git",        "-C",      root.string(), "check-ignore",
                                             "--no-index", "--stdin", "-z",          "--verbose"};
    auto ignored = server::runProcess(arguments, input);
    if (!ignored || (ignored->exitCode != 0 && ignored->exitCode != 1))
        return {};

    std::map<std::string, GitIgnoreMatch> matches;
    std::unordered_set<std::string> ignoredFiles;
    std::map<std::string, std::string> ignoredDirectories;
    records = ignored->output;
    while (!records.empty()) {
        std::string_view fields[4];
        for (auto& field : fields) {
            auto end = records.find('\0');
            if (end == std::string_view::npos)
                return {};
            field = records.substr(0, end);
            records.remove_prefix(end + 1);
        }
        if (fields[2].empty() || fields[2].starts_with('!'))
            continue;
        auto rule = fmt::format("{}:{}: {}", fields[0], fields[1], fields[2]);
        if (fields[3].ends_with('/')) {
            ignoredDirectories.emplace(fields[3], rule);
        }
        else {
            ignoredFiles.emplace(fields[3]);
            auto match = matches.try_emplace(rule, GitIgnoreMatch{.rule = rule}).first;
            match->second.files.emplace_back(fields[3]);
        }
    }
    for (const auto& file : files) {
        if (!ignoredFiles.contains(file))
            addParents(fs::path(file), protectedDirectories);
    }
    std::set<std::string> selected;
    for (const auto& [directory, rule] : ignoredDirectories) {
        if (protectedDirectories.contains(directory))
            continue;
        bool covered = false;
        for (auto parent = fs::path(directory).parent_path().parent_path(); !parent.empty();
             parent = parent.parent_path()) {
            if (selected.contains(parent.generic_string() + '/')) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            selected.insert(directory);
            auto match = matches.try_emplace(rule, GitIgnoreMatch{.rule = rule}).first;
            match->second.directories.push_back(fs::path(directory).parent_path());
        }
    }
    for (const auto& directory : protectedDirectories)
        protectedNames.insert(fs::path(directory).parent_path().filename().string());
    std::vector<GitIgnoreMatch> result;
    for (auto& [_, match] : matches) {
        for (const auto& directory : match.directories) {
            auto name = directory.filename().string();
            if (!protectedNames.contains(name) &&
                std::ranges::find(match.names, name) == match.names.end())
                match.names.push_back(std::move(name));
        }
        result.push_back(std::move(match));
    }
    return result;
}

void Indexer::excludeDirectories(std::span<const fs::path> directories) {
    if (directories.empty())
        return;
    IndexWriteGuard guard(*this);
    std::vector<std::string> prefixes;
    prefixes.reserve(directories.size());
    for (const auto& directory : directories)
        prefixes.push_back((fs::absolute(directory).lexically_normal() / "").generic_string());
    const ExclusionSet excludedPaths(prefixes);
    auto excluded = [&](const fs::path& path) {
        return excludedPaths.containsDirectoryOrAncestor(
            (fs::absolute(path).lexically_normal() / "").generic_string());
    };
    std::unordered_set<fs::path> remaining;
    for (auto it = includeToFiles_.begin(); it != includeToFiles_.end();) {
        auto& paths = it->second;
        paths.erase(std::remove_if(paths.begin(), paths.end(),
                                   [&](const auto* path) { return excluded(*path); }),
                    paths.end());
        for (const auto* path : paths)
            remaining.insert(*path);
        if (paths.empty())
            it = includeToFiles_.erase(it);
        else
            ++it;
    }
    std::vector<fs::path> sources;
    for (auto it = indexedFiles.begin(); it != indexedFiles.end();) {
        const auto* path = (it++)->first;
        if (excluded(*path))
            removePathFromIndex(path);
        else if (remaining.contains(fs::absolute(*path).lexically_normal()))
            sources.push_back(*path);
    }
    std::erase_if(directoryEntryCounts_, [&](const auto& entry) { return excluded(entry.first); });
    includeDirectories_.clear();
    inferIncludeSearchDirectories(sources);
}

std::optional<fs::path> Indexer::getNearestFileForInclude(std::string_view path,
                                                          const fs::path& source) const {
    IndexReadGuard guard(*this);
    auto it = includeToFiles_.find(fs::path(path).filename().string());
    if (it == includeToFiles_.end())
        return std::nullopt;
    return nearestInclude(path, source, it->second);
}

std::vector<fs::path> Indexer::getConflictingIncludeDirectories(
    std::span<const fs::path> configuredDirectories) const {
    IndexReadGuard guard(*this);
    std::unordered_map<fs::path, std::pair<fs::path, fs::path>> matches;
    std::unordered_set<fs::path> conflicts;
    for (const auto& [_, files] : includeToFiles_) {
        for (const auto* file : files) {
            auto canonical = fs::weakly_canonical(*file);
            for (const auto& directory : includeDirectories_) {
                auto relative = canonical.lexically_relative(directory);
                if (relative.empty() || *relative.begin() == "..")
                    continue;
                auto [it, inserted] = matches.try_emplace(relative, canonical, directory);
                if (!inserted && it->second.first != canonical) {
                    conflicts.insert(directory);
                    conflicts.insert(it->second.second);
                }
                for (const auto& configured : configuredDirectories) {
                    auto existing = configured / relative;
                    std::error_code ec;
                    if (fs::is_regular_file(existing, ec) &&
                        fs::weakly_canonical(existing) != canonical)
                        conflicts.insert(directory);
                }
            }
        }
    }
    std::vector<fs::path> result(conflicts.begin(), conflicts.end());
    std::ranges::sort(result);
    return result;
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

std::optional<Indexer::GlobalSymbolLoc> Indexer::getNearestSymbolLoc(std::string_view name,
                                                                     const fs::path& source) const {
    auto nearest = getNearestSymbolLocs(name, source);
    return nearest.empty() ? std::nullopt : std::optional(nearest.front());
}

std::vector<Indexer::GlobalSymbolLoc> Indexer::getNearestSymbolLocs(std::string_view name,
                                                                    const fs::path& source) const {
    IndexReadGuard guard(*this);

    auto it = symbolToFiles_.find(std::string(name));
    if (it == symbolToFiles_.end() || it->second.empty()) {
        return {};
    }
    if (it->second.size() == 1)
        return {it->second[0]};
    auto nearest = nearestFiles<GlobalSymbolLoc>(source, it->second,
                                                 [](const auto& entry) { return entry.uri; });
    std::vector<GlobalSymbolLoc> result;
    for (auto* candidate : nearest)
        result.push_back(*candidate);
    return result;
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
                                        std::vector<fs::path>& outFiles,
                                        DirectoryEntryCounts* directoryEntries,
                                        std::span<const std::string> excludedPaths) {

    const ExclusionSet excludedNames(excludeDirs);
    const ExclusionSet paths(excludedPaths);
    auto excludedPath = [&](const fs::path& path) {
        if (paths.empty())
            return false;
        const auto normalized = (fs::absolute(path).lexically_normal() / "").generic_string();
        return paths.contains(normalized);
    };

    if ((!excludedNames.empty() &&
         std::ranges::any_of(
             dir, [&](const auto& part) { return excludedNames.contains(part.string()); })) ||
        (!paths.empty() && paths.containsDirectoryOrAncestor(
                               (fs::absolute(dir).lexically_normal() / "").generic_string())) ||
        !fs::exists(dir) || !fs::is_directory(dir)) {
        return;
    }
    auto startSize = outFiles.size();

    ScopedTimer t_index(fmt::format("Crawling {}", dir.string()));
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {

        if (directoryEntries)
            ++(*directoryEntries)[it->path().parent_path().lexically_normal()];

        // Check for exclude when entering a new directory
        if (it->is_directory(ec)) {
            if ((!excludedNames.empty() &&
                 excludedNames.contains(it->path().filename().string())) ||
                excludedPath(it->path())) {
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

bool Indexer::usesDefaultWorkspaceIndex(std::span<const Config::IndexConfig> indexConfigs,
                                        const fs::path& workspace) {
    if (indexConfigs.empty())
        return true;
    auto within = [](const fs::path& path, const fs::path& directory) {
        auto relative = path.lexically_relative(directory);
        return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
    };
    const auto root = workspace.lexically_normal();
    std::error_code ec;
    const auto canonicalRoot = fs::weakly_canonical(root, ec);
    for (const auto& entry : indexConfigs) {
        if (entry.dirs.value().empty())
            return false;
        for (const auto& directory : entry.dirs.value()) {
            auto path = (root / directory).lexically_normal();
            if (within(path, root) || within(root, path))
                return false;
            if (!canonicalRoot.empty()) {
                path = fs::weakly_canonical(path, ec);
                if (!ec && (within(path, canonicalRoot) || within(canonicalRoot, path)))
                    return false;
            }
        }
    }
    return true;
}

void Indexer::startIndexing(const std::vector<Config::IndexConfig>& indexConfigs,
                            std::optional<std::string_view> workspaceFolder,
                            bool inferIncludeDirectories) {
    std::vector<fs::path> pathsToIndex;
    DirectoryEntryCounts directoryEntries;
    auto* counts = inferIncludeDirectories ? &directoryEntries : nullptr;

    if (workspaceFolder && usesDefaultWorkspaceIndex(indexConfigs, fs::path(*workspaceFolder)))
        collectFilesFromDirectory(fs::path(*workspaceFolder), {}, pathsToIndex, counts);
    for (const auto& cfg : indexConfigs) {
        std::vector<std::string> excludedNames;
        std::vector<std::string> excludedPaths;
        for (const auto& exclusion : cfg.excludeDirs.value().value_or(std::vector<std::string>{})) {
            if (fs::path(exclusion).is_absolute() || exclusion.starts_with("./")) {
                auto path = fs::path(exclusion);
                if (!path.is_absolute()) {
                    if (!workspaceFolder)
                        continue;
                    path = fs::path(*workspaceFolder) / path;
                }
                excludedPaths.push_back(
                    (fs::absolute(path).lexically_normal() / "").generic_string());
            }
            else
                excludedNames.push_back(exclusion);
        }
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
            collectFilesFromDirectory(fullDirPath.lexically_normal(), excludedNames, pathsToIndex,
                                      counts, excludedPaths);
        }
    }

    indexAndReport(pathsToIndex, inferIncludeDirectories, std::move(directoryEntries));
}

void Indexer::startIndexing(const std::vector<std::string>& globs,
                            const std::vector<std::string>& excludeDirs,
                            bool inferIncludeDirectories) {
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

    indexAndReport(pathsToIndex, inferIncludeDirectories);
}

void Indexer::inferIncludeSearchDirectories(std::span<const fs::path> paths) {
    ScopedTimer timer("Include-directory inference");
    std::unordered_map<fs::path, std::pair<fs::path, std::string>> directorySources;
    for (const auto& path : paths) {
        auto file = indexedFiles.find(internUri(path));
        if (file == indexedFiles.end() || file->second.includes.empty())
            continue;
        auto parent = fs::weakly_canonical(path);
        for (const auto& spelling : file->second.includes) {
            std::error_code ec;
            if (fs::is_regular_file(parent.parent_path() / spelling, ec))
                continue;
            auto includePath = fs::path(spelling).lexically_normal();
            if (includePath.empty() || includePath.is_absolute() ||
                std::ranges::any_of(includePath, [](const auto& part) { return part == ".."; }))
                continue;
            auto candidates = includeToFiles_.find(includePath.filename().string());
            if (candidates == includeToFiles_.end())
                continue;
            const fs::path* match = nullptr;
            fs::path identity;
            for (const auto* candidate : candidates->second) {
                auto remaining = *candidate;
                auto suffix = includePath;
                while (!suffix.empty() && suffix.filename() == remaining.filename()) {
                    suffix = suffix.parent_path();
                    remaining = remaining.parent_path();
                }
                if (!suffix.empty() || !fs::is_regular_file(*candidate, ec))
                    continue;
                auto canonical = fs::weakly_canonical(*candidate, ec);
                if (ec)
                    continue;
                if (match && identity != canonical) {
                    match = nullptr;
                    break;
                }
                match = candidate;
                identity = std::move(canonical);
            }
            if (!match)
                continue;
            auto directory = *match;
            for (auto suffix = includePath; !suffix.empty(); suffix = suffix.parent_path())
                directory = directory.parent_path();
            directory = fs::weakly_canonical(directory);
            directorySources.try_emplace(directory, parent, spelling);
        }
    }
    for (const auto& [directory, _] : directorySources)
        includeDirectories_.push_back(directory);
    std::ranges::sort(includeDirectories_);
    INFO("Found {} inferred include directories", includeDirectories_.size());
    for (const auto& directory : includeDirectories_) {
        const auto& [source, spelling] = directorySources.at(directory);
        INFO("Inferred include directory: {} (from `include \"{}\" in {})", directory.string(),
             spelling, source.string());
    }
}

void Indexer::indexAndReport(std::vector<fs::path> pathsToIndex, bool inferIncludeDirectories,
                             DirectoryEntryCounts directoryEntries) {
    INFO("Indexing {} files", pathsToIndex.size());

    IndexWriteGuard guard(*this);
    ScopedTimer t_index("Workspace indexing");
    directoryEntryCounts_ = std::move(directoryEntries);
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

    includeDirectories_.clear();
    if (inferIncludeDirectories)
        inferIncludeSearchDirectories(pathsToIndex);

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
