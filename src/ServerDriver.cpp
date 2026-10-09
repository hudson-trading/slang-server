//------------------------------------------------------------------------------
// ServerDriver.cpp
// Implementation of server driver class for processing syntax trees
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ServerDriver.h"

#include "Indexer.h"
#include "ServerDiagClient.h"
#include "SystemTaskDocs.h"
#include "ast/ServerCompilation.h"
#include "completions/CompletionDispatch.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "lsp/RequestContext.h"
#include "lsp/URI.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include "util/Markdown.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <queue>
#include <string_view>

#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/SystemSubroutine.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/driver/Driver.h"
#include "slang/driver/SourceLoader.h"
#include "slang/parsing/ParserMetadata.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {
using namespace slang;

bool ServerDriver::s_debugHoversEnabled =
#ifdef SLANG_DEBUG
    true;
#else
    false;
#endif

ServerDriver::ServerDriver(Indexer& indexer, SlangLspClient& client, const Config& config,
                           std::vector<std::string> buildfiles,
                           std::optional<std::string_view> workspaceFolder,
                           bool requireValidConfig) :
    sm(driver.sourceManager), diagEngine(driver.diagEngine), client(client),
    diagClient(std::make_shared<ServerDiagClient>(sm, client)),
    completions(*this, indexer, sm, options), codeActions(*this, sm, indexer), m_indexer(indexer),
    m_config(config), m_workspacePathPrefix(workspaceFolder.value_or("")) {
    if (!m_workspacePathPrefix.empty() && m_workspacePathPrefix.back() != '/' &&
        m_workspacePathPrefix.back() != '\\') {
        m_workspacePathPrefix.push_back(std::filesystem::path::preferred_separator);
    }

    // Slang resolves -f arguments and source paths against the process working directory.
    auto previousDirectory = std::filesystem::current_path();
    auto restoreDirectory = ScopeGuard([&] {
        std::error_code ec;
        std::filesystem::current_path(previousDirectory, ec);
        if (ec)
            WARN("Failed to restore working directory {}: {}", previousDirectory.string(),
                 ec.message());
    });
    if (workspaceFolder)
        std::filesystem::current_path(*workspaceFolder);
    m_configLoaded = parseAndLoadSources(buildfiles, requireValidConfig);
}

bool ServerDriver::parseAndLoadSources(const std::vector<std::string>& buildfiles,
                                       bool requireValidConfig) {
    driver.addStandardArgs();
    diagEngine.removeClient(driver.textDiagClient);
    if (requireValidConfig) {
        driver.textDiagClient = std::make_shared<TextDiagnosticClient>();
        diagEngine.addClient(driver.textDiagClient);
    }
    else {
        diagEngine.addClient(diagClient);
    }

    slang::CommandLine::ParseOptions parseOpts;
    parseOpts.expandEnvVars = true;
    parseOpts.ignoreProgramName = true;
    parseOpts.supportComments = true;
    parseOpts.ignoreDuplicates = true;

    // Parse each config file's flags separately so -D defines are attributed correctly
    bool ok = true;

    for (auto& src : m_config.flagsByFile.value()) {
        auto guard = driver.setCurrentCommandFile(src.filePath);
        ok &= driver.parseCommandLine(src.flags, parseOpts);
    }

    driver.options.errorLimit = 0;
    ok &= driver.processOptions(false);
    if (!ok && !requireValidConfig) {
        client.showError("Failed to parse config flags");
    }

    for (auto& buildfile : buildfiles) {
        auto loaded = driver.processCommandFiles(buildfile, m_config.buildRelativePaths.value(),
                                                 false);
        ok &= loaded;
        if (loaded) {
            INFO("Processed build file: {}", buildfile);
        }
        else if (!requireValidConfig) {
            client.showError(fmt::format("Failed to process build file: {}", buildfile));
        }
    }

    for (const auto& directory : m_config.incdirs.value()) {
        auto expanded = Config::expandPathVariables(directory);
        if (!expanded) {
            ok = false;
            continue;
        }
        auto path = std::filesystem::path(*expanded);
        if (path.is_relative() && !m_workspacePathPrefix.empty())
            path = std::filesystem::path(m_workspacePathPrefix) / path;
        if (auto ec = sm.addUserDirectories(path.string())) {
            ok = false;
            auto message = fmt::format("Cannot use include directory {}: {}", path.string(),
                                       ec.message());
            if (requireValidConfig) {
                WARN("Config reload failed: {}", message);
            }
            else {
                client.showError(message);
            }
        }
    }

    if (!ok && requireValidConfig) {
        WARN("Config reload failed: {}", driver.textDiagClient->getString());
        return false;
    }

    // Build macro name -> source file map from the driver's per-file define lists
    for (auto& [path, meta] : driver.getCommandFileMetadata()) {
        for (auto& define : meta.defines) {
            auto eqPos = define.find('=');
            auto macroName = eqPos != std::string::npos ? define.substr(0, eqPos) : define;
            m_defineSources[macroName] = path;
        }
    }

    // Configure diagnostic engine. The LSP server reports warnings as editor
    // diagnostics by default; user-provided -Wno-* mappings still suppress
    // specific warnings via the severity table populated by processOptions().
    diagEngine.setIgnoreAllWarnings(false);
    diagEngine.setIgnoreAllNotes(false);

    options = driver.createOptionBag();
    options.set(driver.getAnalysisOptions());
    ok &= driver.parseAllSources();
    if (requireValidConfig) {
        if (!ok) {
            WARN("Source reload failed: {}", driver.textDiagClient->getString());
            return false;
        }
        if (!driver.textDiagClient->getString().empty())
            WARN("Config reload diagnostics: {}", driver.textDiagClient->getString());
        diagEngine.removeClient(driver.textDiagClient);
        diagEngine.addClient(diagClient);
    }
    diagEngine.setMappingsFromPragmas();

    // Create documents from syntax trees
    INFO("Creating ServerDriver with {} trees", driver.syntaxTrees.size());
    for (auto& tree : driver.syntaxTrees) {
        m_indexer.updateIncludes(*tree);
        for (auto buffer : tree->getSourceBufferIds()) {
            auto path = sm.getFullPath(buffer);
            if (!path.empty())
                m_buildSourceUris.emplace(URI::fromFile(path));
        }

        auto uri = URI::fromFile(sm.getFullPath(tree->getSourceBufferIds()[0]));
        auto doc = SlangDoc::fromTree(*this, std::move(tree));
        docs[uri] = doc;
    }
    return ok;
}

void ServerDriver::analyzeDocument(SlangDoc& doc, const lsp::RequestContext& ctx) {
    ctx.throwIfCancelled("before diagnostics");

    // Clear and re-issue diagnostics for this document
    diagClient->clear(doc.getURI());

    // Pragma mappings are populated from the preprocessor/lexer
    doc.getSyntaxTree();

    // Update pragma mappings for the changed buffer
    diagEngine.setMappingsFromPragmas(doc.getBuffer());

    doc.issueDiagnosticsTo(diagEngine, ctx);
    ctx.throwIfCancelled("before publishing diagnostics");
    diagClient->pushDiags(doc.getURI());

    publishInactiveRegions(doc, ctx);
}

void ServerDriver::onDocDidSave(SlangDoc& doc) {
    auto tree = doc.getSyntaxTree();
    if (sm.getFullPath(tree->getSourceBufferIds()[0]) != sm.getFullPath(doc.getBuffer())) {
        auto indexOptions = options;
        auto ppOptions = indexOptions.getOrDefault<parsing::PreprocessorOptions>();
        ppOptions.maxIncludeDepth = 0;
        indexOptions.set(ppOptions);
        tree = syntax::SyntaxTree::fromBuffer(
            SourceBuffer{.data = doc.getText(), .id = doc.getBuffer()}, sm, indexOptions);
    }
    m_indexer.updateDocument(doc.getURI().getPath(), *tree);
    m_indexer.updateIncludes(*doc.getSyntaxTree());

    if (!comp) {
        analyzeDocument(doc);
        // Reanalyze open documents whose dependency buffers were invalidated
        for (const auto& uri : m_openDocs) {
            if (uri == doc.getURI())
                continue;

            auto it = docs.find(uri);
            if (it == docs.end()) {
                ERROR("Open Doc {} not found", uri.getPath());
                continue;
            }
            if (it->second->hasAnalysis() && it->second->getSyntaxTree() != doc.getSyntaxTree())
                continue;

            analyzeDocument(*it->second);
        }
    }
    else {
        diagClient->clear();
        comp->refresh();
        invalidateAnalysesAndRefreshClient();
        publishCompilationDiagnostics(&doc.getURI());
        publishInactiveRegions(doc);
    }
}

void ServerDriver::publishCompilationDiagnostics(const URI* priorityUri) {
    diagEngine.setMappingsFromPragmas();
    for (const auto& entry : docs) {
        entry.second->issueParseDiagnostics(diagEngine);
    }
    comp->issueDiagnosticsTo(diagEngine);
    if (priorityUri)
        diagClient->pushDiags(*priorityUri);
    else
        diagClient->pushDiags();
}

void ServerDriver::copyOpenDocumentsFrom(const ServerDriver* oldDriver) {
    if (!oldDriver)
        return;

    m_includeSelections = oldDriver->m_includeSelections;
    completions.resolveEdits = oldDriver->completions.resolveEdits;
    oldDriver->diagClient->clearAndPush();
    for (const auto& uri : oldDriver->m_openDocs) {
        auto docIt = oldDriver->docs.find(uri);
        if (docIt == oldDriver->docs.end()) {
            ERROR("Open Doc {} not found in old driver", uri.getPath());
            continue;
        }

        // openDocument expects no \0
        auto text = docIt->second->getText();
        text.remove_suffix(1);
        openDocument(uri, text);
    }
}

std::unique_ptr<ServerDriver> ServerDriver::createForExplore(
    Indexer& indexer, SlangLspClient& client, const Config& config,
    std::optional<std::string_view> workspaceFolder, const ServerDriver* oldDriver,
    bool requireValidConfig) {
    auto newDriver = std::make_unique<ServerDriver>(indexer, client, config,
                                                    std::vector<std::string>{}, workspaceFolder,
                                                    requireValidConfig);
    if (requireValidConfig && !newDriver->m_configLoaded)
        return nullptr;
    newDriver->copyOpenDocumentsFrom(oldDriver);
    return newDriver;
}

std::unique_ptr<ServerDriver> ServerDriver::createFromFileLists(
    Indexer& indexer, SlangLspClient& client, const Config& config,
    std::vector<std::string> buildfiles, std::optional<std::string_view> workspaceFolder,
    const ServerDriver* oldDriver, bool requireValidConfig) {
    auto newDriver = std::make_unique<ServerDriver>(indexer, client, config, std::move(buildfiles),
                                                    workspaceFolder, requireValidConfig);
    if (requireValidConfig && !newDriver->m_configLoaded)
        return nullptr;

    std::vector<std::shared_ptr<SlangDoc>> buildDocuments;
    buildDocuments.reserve(newDriver->docs.size());
    for (const auto& entry : newDriver->docs) {
        buildDocuments.push_back(entry.second);
    }

    if (buildDocuments.empty()) {
        newDriver->copyOpenDocumentsFrom(oldDriver);
        ERROR("No documents available for compilation");
        return newDriver;
    }

    newDriver->comp = std::make_unique<ServerCompilation>(std::move(buildDocuments),
                                                          newDriver->options, newDriver->sm,
                                                          newDriver->client);

    newDriver->diagClient->clear();
    newDriver->copyOpenDocumentsFrom(oldDriver);
    newDriver->publishCompilationDiagnostics();
    return newDriver;
}

void ServerDriver::openDocument(const URI& uri, const std::string_view text) {
    auto docIter = docs.find(uri);
    std::shared_ptr<SlangDoc> doc;
    bool matchesBuildSource = false;
    if (docIter != docs.end() && docIter->second->textMatches(text)) {
        doc = docIter->second;
        matchesBuildSource = m_buildSourceUris.contains(uri);
    }
    else if (docIter == docs.end() && m_buildSourceUris.contains(uri)) {
        doc = SlangDoc::open(*this, uri);
        if (doc->textMatches(text)) {
            matchesBuildSource = true;
            docs[uri] = doc;
        }
        else {
            doc.reset();
        }
    }

    if (!doc) {
        if (docIter != docs.end())
            WARN("Document {} text does not match, updating", uri.getPath());
        doc = SlangDoc::fromText(*this, uri, text);
        docs[uri] = doc;
    }

    if (comp && matchesBuildSource) {
        // File is already part of the compilation — compilation diags were
        // already published, so skip shallow diags. Still publish inactive regions.
        publishInactiveRegions(*doc);
    }
    else {
        analyzeDocument(*doc);
    }

    // Track this as an open document
    m_openDocs.insert(uri);
}

std::shared_ptr<SlangDoc> ServerDriver::getDocument(const URI& uri) {
    auto it = docs.find(uri);
    if (it != docs.end())
        return it->second;

    auto doc = SlangDoc::open(*this, uri);
    if (doc) {
        docs[uri] = doc;
    }
    return doc;
}

bool ServerDriver::isDocumentOpen(const URI& uri) {
    return m_openDocs.find(uri) != m_openDocs.end();
}

void ServerDriver::onDocDidChange(const lsp::DidChangeTextDocumentParams& params,
                                  const lsp::RequestContext& ctx) {
    auto doc = getDocument(params.textDocument.uri);
    if (!doc) {
        ctx.error("Document {} not found", params.textDocument.uri.getPath());
        return;
    }

    doc->onChange(params.contentChanges);
    if (ctx.isCancelled()) {
        ctx.info("Applied changes for {}; skipping superseded analysis", doc->getWsRelativePath());
        ctx.throwIfCancelled("before analysis");
    }
    analyzeDocument(*doc, ctx);
}

void ServerDriver::closeDocument(const URI& uri) {
    // Remove from open docs set
    m_openDocs.erase(uri);
    if (!comp) {
        diagClient->clear(uri);
    }
}

void ServerDriver::reloadDocument(const URI& uri) {
    // Only reload if this is an open document
    if (m_openDocs.find(uri) == m_openDocs.end()) {
        return;
    }

    auto doc = getDocument(uri);
    if (!doc) {
        WARN("Document {} not found for reload", uri.getPath());
        return;
    }

    if (!doc->reloadBuffer()) {
        return;
    }

    INFO("Reloaded document {} from disk", uri.getPath());

    // Update the document (reparse and issue diagnostics)
    analyzeDocument(*doc);
}

void ServerDriver::onWorkspaceDidChangeWatchedFiles(
    const lsp::DidChangeWatchedFilesParams& params) {
    // Collect docs that need updating after all buffers are reloaded
    std::vector<std::shared_ptr<SlangDoc>> updatedDocs;

    for (const auto& change : params.changes) {
        // didChange ranges refer to the editor's previous text. A disk reload here
        // would apply the next incremental edit to the wrong version of the buffer.
        if (isDocumentOpen(change.uri))
            continue;

        switch (change.type) {
            case lsp::FileChangeType::Created:
            case lsp::FileChangeType::Changed: {
                auto it = docs.find(change.uri);
                if (it == docs.end()) {
                    // Included headers can be cached without having a SlangDoc.
                    if (sm.isCached(change.uri.getPath())) {
                        auto buffer = sm.readSource(change.uri.getPath(), nullptr);
                        if (buffer) {
                            auto result = sm.reloadBuffer(buffer->id);
                            if (!result)
                                WARN("Failed to reload {}: {}", change.uri.getPath(),
                                     result.error().message());
                        }
                    }
                    continue;
                }

                auto doc = it->second;
                if (!doc->reloadBuffer()) {
                    continue;
                }

                INFO("Reloaded document {} from disk", change.uri.getPath());
                updatedDocs.push_back(doc);
                break;
            }
            case lsp::FileChangeType::Deleted:
                closeDocument(change.uri);
                break;
        }
    }

    // Analyze only after every changed disk buffer has been reloaded.
    for (auto& doc : updatedDocs) {
        analyzeDocument(*doc);
    }
    for (const auto& uri : m_openDocs) {
        auto it = docs.find(uri);
        if (it != docs.end() && !it->second->hasAnalysis())
            analyzeDocument(*it->second);
    }
}

void ServerDriver::invalidateAnalysesAndRefreshClient() {
    for (auto& [_, doc] : docs) {
        doc->invalidateAnalysis();
    }
    client.onWorkspaceCodeLensRefresh(std::monostate{});
    client.onWorkspaceInlayHintRefresh(std::monostate{});
}

bool ServerDriver::setActiveInstance(std::string_view hierPath) {
    if (!comp) {
        return false;
    }

    if (!comp->setActiveInstance(std::string(hierPath))) {
        return false;
    }

    invalidateAnalysesAndRefreshClient();
    return true;
}

std::vector<ServerDriver::IncludeContext> ServerDriver::getIncludeContexts(
    const std::filesystem::path& path, bool selectedOnly) {
    const auto canonical = std::filesystem::weakly_canonical(path);
    if (!m_indexer.hasPotentialIncluders(canonical))
        return {};
    std::error_code ec;
    if ((!sm.isCached(canonical) && !std::filesystem::is_regular_file(canonical, ec)) ||
        getDocument(URI::fromFile(canonical))->isMacroOnly())
        return {};
    std::vector<std::filesystem::path> pending{canonical};
    std::unordered_set<std::filesystem::path> visited;
    std::vector<std::filesystem::path> sources;
    std::optional<IncludeContextSelection> preferred;
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto current = pending[i];
        if (!visited.insert(current).second)
            continue;
        if (!preferred) {
            if (auto it = m_includeSelections.find(current); it != m_includeSelections.end()) {
                preferred = it->second;
                if (current != canonical)
                    preferred->occurrence = 0;
            }
        }
        auto parents = m_indexer.getFilesIncluding(current);
        std::ranges::sort(parents);
        if (parents.empty() && current != canonical)
            sources.push_back(current);
        pending.insert(pending.end(), parents.begin(), parents.end());
    }
    std::ranges::sort(sources);
    if (sources.empty())
        return {};
    if (preferred) {
        auto it = std::ranges::find(sources, std::filesystem::path(preferred->source.getPath()));
        if (it != sources.end())
            std::rotate(sources.begin(), it, it + 1);
    }

    std::vector<IncludeContext> contexts;
    for (const auto& source : sources) {
        std::error_code ec;
        if (!sm.isCached(source) && !std::filesystem::is_regular_file(source, ec))
            continue;
        auto doc = getDocument(URI::fromFile(source));
        if (!doc)
            continue;
        auto tree = doc->getSyntaxTree();
        size_t occurrence = 0;
        for (const auto& include : tree->getIncludeDirectives()) {
            if (!include.buffer || sm.getFullPath(include.buffer.id) != canonical)
                continue;
            contexts.push_back({.source = doc,
                                .buffer = include.buffer.id,
                                .location = toLocation(include.syntax->sourceRange(), sm),
                                .occurrence = occurrence++});
        }
        if (selectedOnly && !contexts.empty())
            break;
    }
    if (preferred) {
        auto it = std::ranges::find_if(contexts, [&](const auto& context) {
            return context.source->getURI() == preferred->source &&
                   context.occurrence == preferred->occurrence;
        });
        if (it != contexts.end())
            std::rotate(contexts.begin(), it, it + 1);
    }
    if (selectedOnly && contexts.size() > 1)
        contexts.erase(contexts.begin() + 1, contexts.end());
    return contexts;
}

bool ServerDriver::setIncludeContext(const IncludeContextSelection& selection) {
    auto canonical = std::filesystem::weakly_canonical(selection.uri.getPath());
    auto source = URI::fromFile(std::filesystem::weakly_canonical(selection.source.getPath()));
    auto contexts = getIncludeContexts(canonical);
    if (std::ranges::none_of(contexts, [&](const auto& context) {
            return context.source->getURI() == source && context.occurrence == selection.occurrence;
        }))
        return false;
    m_includeSelections.insert_or_assign(
        canonical, IncludeContextSelection{.uri = URI::fromFile(canonical),
                                           .source = source,
                                           .occurrence = selection.occurrence});
    invalidateAnalysesAndRefreshClient();
    for (const auto& uri : m_openDocs)
        analyzeDocument(*getDocument(uri));
    return true;
}

std::shared_ptr<syntax::SyntaxTree> ServerDriver::parseShallowTree(SourceBuffer buffer,
                                                                   Bag& documentOptions) {
    documentOptions = options;
    auto tree = syntax::SyntaxTree::fromBuffer(buffer, sm, documentOptions);
    if (!m_buildSourceUris.contains(URI::fromFile(sm.getFullPath(buffer.id)))) {
        const auto configured = options.getOrDefault<parsing::PreprocessorOptions>();
        auto findTarget = [&](const parsing::IncludeMetadata& include) {
            auto location = sm.getFullyExpandedLoc(include.syntax->sourceRange().start());
            auto normal = sm.readHeader(include.path, location, sm.getLibraryFor(location.buffer()),
                                        false, configured.additionalIncludePaths);
            auto target = normal ? std::optional(sm.getFullPath(normal->id))
                                 : m_indexer.getNearestFileForInclude(
                                       include.path, sm.getFullPath(location.buffer()));
            return std::pair{std::move(target), bool(normal)};
        };
        std::vector<std::filesystem::path> inferred;
        while (true) {
            auto ppOptions = configured;
            auto& paths = ppOptions.additionalIncludePaths;
            auto proposed = inferred;
            for (const auto& include : tree->getIncludeDirectives()) {
                if (include.isSystem)
                    continue;
                auto [target, normal] = findTarget(include);
                if (!target)
                    continue;
                auto suffix = std::filesystem::path(include.path).lexically_normal();
                if (suffix.is_absolute() ||
                    std::ranges::any_of(suffix, [](const auto& part) { return part == ".."; }))
                    continue;
                auto directory = *target;
                while (!suffix.empty() && directory.filename() == suffix.filename()) {
                    directory = directory.parent_path();
                    suffix = suffix.parent_path();
                }
                if (!suffix.empty())
                    continue;
                // Additional paths precede global -I paths, so keep known resolutions first.
                auto& destinations = normal ? paths : proposed;
                if (std::ranges::find(destinations, directory) == destinations.end())
                    destinations.push_back(std::move(directory));
            }
            if (proposed == inferred)
                break;
            for (const auto& directory : proposed) {
                if (std::ranges::find(paths, directory) == paths.end())
                    paths.push_back(directory);
            }
            auto proposedOptions = options;
            proposedOptions.set(std::move(ppOptions));
            auto candidate = syntax::SyntaxTree::fromBuffer(buffer, sm, proposedOptions);
            bool compatible = true;
            for (const auto& include : candidate->getIncludeDirectives()) {
                if (include.isSystem || !include.buffer)
                    continue;
                auto expected = findTarget(include).first;
                if (!expected || std::filesystem::weakly_canonical(*expected) !=
                                     sm.getFullPath(include.buffer.id)) {
                    compatible = false;
                    break;
                }
            }
            // One tree has one search order; conflicting choices need explicit configuration.
            if (!compatible)
                break;
            inferred = std::move(proposed);
            documentOptions = std::move(proposedOptions);
            tree = std::move(candidate);
        }
    }
    m_indexer.updateIncludes(*tree);
    return tree;
}

std::vector<std::shared_ptr<syntax::SyntaxTree>> ServerDriver::getDependentTrees(
    std::shared_ptr<syntax::SyntaxTree> tree, bool fullHierarchy) {

    std::vector<std::shared_ptr<syntax::SyntaxTree>> result;
    std::queue<std::shared_ptr<syntax::SyntaxTree>> treesToProcess;
    flat_hash_set<std::string_view> knownNames;
    flat_hash_set<const syntax::SyntaxTree*> processedTrees{tree.get()};
    std::unordered_set<std::filesystem::path> processedFiles;
    for (auto buffer : tree->getSourceBufferIds())
        processedFiles.insert(sm.getFullPath(buffer));

    treesToProcess.push(tree);
    tree->getMetadata().visitDeclaredSymbols(
        [&](std::string_view name) { knownNames.emplace(name); });

    while (!treesToProcess.empty()) {
        auto currentTree = treesToProcess.front();
        treesToProcess.pop();

        auto& meta = currentTree->getMetadata();

        auto loadDependency = [&](parsing::Token token) {
            auto name = token.valueText();
            if (name.empty() || knownNames.contains(name))
                return;

            const auto& source = sm.getFullPath(sm.getFullyExpandedLoc(token.location()).buffer());
            auto symbolLoc = m_indexer.getNearestSymbolLoc(name, source);
            if (!symbolLoc)
                return;
            knownNames.emplace(name);
            // An included file may declare this name only under different preprocessor flags.
            // Reloading it would discard that context and can re-enter this compilation's analysis.
            if (processedFiles.contains(std::filesystem::weakly_canonical(*symbolLoc->uri)))
                return;
            auto newdoc = getDocument(URI::fromFile(*symbolLoc->uri));
            if (newdoc) {
                auto dependencyTree = newdoc->getSyntaxTree();
                if (!processedTrees.insert(dependencyTree.get()).second)
                    return;
                for (auto buffer : dependencyTree->getSourceBufferIds())
                    processedFiles.insert(sm.getFullPath(buffer));
                result.push_back(dependencyTree);
                dependencyTree->getMetadata().visitDeclaredSymbols(
                    [&](std::string_view declared) { knownNames.emplace(declared); });

                if (fullHierarchy ||
                    std::ranges::any_of(
                        dependencyTree->getMetadata().nodeMeta, [](const auto& entry) {
                            return entry.first->kind == syntax::SyntaxKind::PackageDeclaration ||
                                   entry.first->kind == syntax::SyntaxKind::InterfaceDeclaration;
                        }))
                    treesToProcess.push(dependencyTree);
            }
            else {
                ERROR("No doc found for {}", symbolLoc->uri->string());
            }
        };

        // Keep each reference's location so dependencies in included files use their own directory.
        for (auto instance : meta.globalInstances)
            loadDependency(instance->type);
        for (auto name : meta.classPackageNames)
            loadDependency(name->identifier);
        for (auto import : meta.packageImports) {
            for (auto item : import->items)
                loadDependency(item->package);
        }
        for (auto type : meta.virtualInterfaceTypes)
            loadDependency(type->name);
        if (!fullHierarchy) {
            for (auto port : meta.interfacePorts)
                loadDependency(port->nameOrKeyword);
            for (auto type : meta.namedPortTypes)
                loadDependency(type->identifier);
        }
    }

    return result;
}

std::vector<std::string> ServerDriver::getModulesInFile(const std::string& path) {
    // Find the document
    auto uri = URI::fromFile(path);
    auto it = docs.find(uri);
    if (it == docs.end()) {
        WARN("Document {} not found", path);
        return {};
    }

    auto& doc = it->second;

    // Get the module-like things from the document and collect into a vector
    std::vector<std::string> moduleNames;
    for (auto& name : doc->getSyntaxTree()->getMetadata().getDeclaredSymbols()) {
        moduleNames.push_back(std::string{name});
    }
    if (moduleNames.empty()) {
        WARN("No modules found in file {}", path);
    }
    INFO("Found {} modules in file {}", moduleNames.size(), path);
    return moduleNames;
}

std::unique_ptr<ServerDriver> ServerDriver::createFromTop(
    Indexer& indexer, SlangLspClient& client, const Config& config, const URI& topUri,
    std::optional<std::string_view> workspaceFolder, const ServerDriver* oldDriver) {
    auto newDriver = createForExplore(indexer, client, config, workspaceFolder, oldDriver);
    auto doc = newDriver->getDocument(topUri);
    if (!doc) {
        client.showError("Document not found: " + std::string(topUri.getPath()));
        return newDriver;
    }

    auto topTree = doc->getSyntaxTree();
    std::string topName;
    if (topTree->getMetadata().nodeMeta.size() == 1) {
        topName = topTree->getMetadata().nodeMeta[0].first->header->name.valueText();
    }
    else {
        ast::Compilation shallowCompilation;
        shallowCompilation.addSyntaxTree(topTree);
        auto& topInstances = shallowCompilation.getRoot().topInstances;
        if (topInstances.empty()) {
            client.showError("No top modules found in: " + std::string(topUri.getPath()));
            return newDriver;
        }
        for (auto& top : topInstances.subspan(1)) {
            WARN("Extra top module: {}", top->name);
        }
        topName = topInstances[0]->name;
    }

    auto syntaxTrees = newDriver->getDependentTrees(topTree, /* fullHierarchy */ true);
    for (auto& dependency : syntaxTrees)
        dependency->isLibraryUnit = true;
    syntaxTrees.insert(syntaxTrees.begin(), topTree);

    std::vector<std::shared_ptr<SlangDoc>> documents;
    documents.reserve(syntaxTrees.size());
    for (const auto& tree : syntaxTrees) {
        documents.push_back(SlangDoc::fromTree(*newDriver, tree));
    }
    for (const auto& dependency : documents) {
        newDriver->docs[dependency->getURI()] = dependency;
    }

    newDriver->comp = std::make_unique<ServerCompilation>(documents, newDriver->options,
                                                          newDriver->sm, newDriver->client,
                                                          std::move(topName));

    newDriver->publishCompilationDiagnostics();
    return newDriver;
}

std::optional<DefinitionInfo> ServerDriver::getMacroDefinitionInfo(
    const ShallowAnalysis& analysis, const parsing::Token& token,
    const syntax::SyntaxNode& referenceSyntax) {
    std::vector<const syntax::DefineDirectiveSyntax*> macroDefs;
    auto addMacroDef = [&](const syntax::DefineDirectiveSyntax* definition) {
        if (definition && std::ranges::find(macroDefs, definition) == macroDefs.end())
            macroDefs.push_back(definition);
    };
    if (referenceSyntax.kind == syntax::SyntaxKind::MacroUsage ||
        referenceSyntax.kind == syntax::SyntaxKind::UndefDirective) {
        auto it = analysis.macroUsageDefinitions.find(&referenceSyntax);
        if (it != analysis.macroUsageDefinitions.end())
            addMacroDef(it->second);
    }

    // A macro usage in a define body has no direct usage mapping. The workspace index also
    // supplies definitions that are not part of the current shallow compilation.
    if (macroDefs.empty()) {
        auto macroName = token.kind == parsing::TokenKind::Directive ? token.rawText().substr(1)
                                                                     : token.valueText();
        auto macro = analysis.macros.find(macroName);
        if (macro != analysis.macros.end()) {
            addMacroDef(macro->second);
        }
        else {
            std::vector<std::filesystem::path> visited;
            auto paths = m_indexer.getFilesForMacro(macroName);
            if (paths.size() > 1)
                std::ranges::sort(paths);
            for (const auto& path : paths) {
                if (std::ranges::find(visited, path) != visited.end())
                    continue;
                visited.push_back(path);

                auto macroDoc = getDocument(URI::fromFile(path));
                if (!macroDoc)
                    continue;
                auto macroAnalysis = macroDoc->getAnalysis();
                auto indexedMacro = macroAnalysis->macros.find(macroName);
                if (indexedMacro != macroAnalysis->macros.end())
                    addMacroDef(indexedMacro->second);
            }
        }
        if (macroDefs.empty())
            return {};
    }

    std::vector<DefinitionInfo::Target> targets;
    for (auto* macroDef : macroDefs) {
        auto nameToken = macroDef->name;
        const bool isMacroGenerated = sm.isMacroLoc(nameToken.location());
        auto syntaxTarget = DefinitionInfo::SyntaxTarget::fromNode(macroDef, nameToken, sm);
        if (isMacroGenerated && syntaxTarget.macroUsageRange == SourceRange::NoLocation)
            ERROR("Couldn't get original range for macro {}", nameToken.valueText());
        DefinitionInfo::MacroTarget::Definition macroDefinition = syntaxTarget;

        const auto defPath = sm.getFullPath(nameToken.location().buffer());
        const auto defPathStr = defPath.filename().string();
        if (!isMacroGenerated && (defPathStr.empty() || defPathStr[0] == '<')) {
            std::string defineSourceFile;
            const auto srcIt = m_defineSources.find(std::string(nameToken.valueText()));
            if (srcIt != m_defineSources.end())
                defineSourceFile = srcIt->second.string();
            macroDefinition = DefinitionInfo::CommandLineDefineTarget{nameToken, defineSourceFile};
        }

        targets.emplace_back(
            DefinitionInfo::MacroTarget{std::move(macroDefinition), referenceSyntax, analysis});
    }
    return DefinitionInfo{sm, std::move(targets)};
}

std::optional<DefinitionInfo> ServerDriver::getDefinitionInfoAt(const URI& uri,
                                                                const lsp::Position& position) {
    auto doc = getDocument(uri);
    if (!doc) {
        return {};
    }
    auto analysis = doc->getAnalysis();

    // Get location, token, and syntax node at position
    auto loc = toSourceLocation(doc->getBuffer(), position, sm);
    if (!loc) {
        return {};
    }
    const parsing::Token* declTok = analysis->syntaxes.getWordTokenAt(loc.value());
    if (!declTok) {
        return {};
    }
    const syntax::SyntaxNode* declSyntax = analysis->syntaxes.getTokenParent(declTok);
    if (!declSyntax) {
        return {};
    }

    auto isMacroRef = [&]() {
        // Normal macro usage (`FOO) or usage inside a `define body
        return declTok->kind == parsing::TokenKind::Directive &&
               (declSyntax->kind == syntax::SyntaxKind::MacroUsage ||
                declSyntax->kind == syntax::SyntaxKind::DefineDirective);
    };
    auto isUndefRef = [&]() {
        // Identifier in `undef FOO
        return declTok->kind == parsing::TokenKind::Identifier &&
               declSyntax->kind == syntax::SyntaxKind::UndefDirective;
    };
    auto isIfdefRef = [&]() {
        // Identifier in `ifdef FOO / `ifndef FOO / `elsif FOO
        return declTok->kind == parsing::TokenKind::Identifier &&
               declSyntax->kind == syntax::SyntaxKind::NamedConditionalDirectiveExpression;
    };

    if (isMacroRef() || isUndefRef() || isIfdefRef())
        return getMacroDefinitionInfo(*analysis, *declTok, *declSyntax);

    if (declTok->kind == parsing::TokenKind::SystemIdentifier) {
        auto knownName = declTok->systemName();
        if (knownName == parsing::KnownSystemName::Unknown)
            return {};

        auto* sub = analysis->getCompilation()->getSystemSubroutine(knownName);
        auto* sysDoc = getSystemTaskDoc(knownName);
        if (!sub || !sysDoc)
            return {};

        return DefinitionInfo{sm, DefinitionInfo::SystemSubroutineTarget{
                                      *declTok, sysDoc, sub->kind == ast::SubroutineKind::Task}};
    }
    auto symbolsEquivalent = [&](const ast::Symbol* left, const ast::Symbol* right) {
        if (left == right)
            return true;
        if (left->kind != right->kind ||
            sm.getFullyOriginalLoc(left->location) != sm.getFullyOriginalLoc(right->location)) {
            return false;
        }

        if (ast::ValueSymbol::isKind(left->kind)) {
            auto& leftValue = left->as<ast::ValueSymbol>();
            auto& rightValue = right->as<ast::ValueSymbol>();
            if (!leftValue.getType().isMatching(rightValue.getType()))
                return false;

            auto constantsMatch = [](const ConstantValue& a, const ConstantValue& b) {
                return (a.bad() || b.bad()) ? a.bad() == b.bad() : a == b;
            };
            if (ast::ParameterSymbol::isKind(left->kind)) {
                return constantsMatch(left->as<ast::ParameterSymbol>().getValue(),
                                      right->as<ast::ParameterSymbol>().getValue());
            }
            if (ast::EnumValueSymbol::isKind(left->kind)) {
                return constantsMatch(left->as<ast::EnumValueSymbol>().getValue(),
                                      right->as<ast::EnumValueSymbol>().getValue());
            }
            return true;
        }
        if (ast::Type::isKind(left->kind))
            return left->as<ast::Type>().isMatching(right->as<ast::Type>());
        if (ast::TypeParameterSymbol::isKind(left->kind)) {
            return left->as<ast::TypeParameterSymbol>().targetType.getType().isMatching(
                right->as<ast::TypeParameterSymbol>().targetType.getType());
        }
        return false;
    };

    struct SymbolCandidate {
        const ast::Symbol* symbol;
        std::shared_ptr<ShallowAnalysis> analysis;
        bool renderInterfaceConnection;
    };
    std::vector<SymbolCandidate> symbols;
    auto addSymbol = [&](const ast::Symbol* symbol,
                         const std::shared_ptr<ShallowAnalysis>& symbolAnalysis,
                         bool renderInterfaceConnection = true) {
        if (!symbol)
            return;
        auto duplicate = std::ranges::find_if(symbols, [&](const auto& existing) {
            return (existing.symbol == symbol && existing.analysis == symbolAnalysis) ||
                   (existing.analysis != symbolAnalysis && existing.symbol->kind == symbol->kind &&
                    existing.symbol->location == symbol->location) ||
                   symbolsEquivalent(existing.symbol, symbol);
        });
        if (duplicate != symbols.end()) {
            duplicate->renderInterfaceConnection |= renderInterfaceConnection;
        }
        else {
            symbols.push_back({symbol, symbolAnalysis, renderInterfaceConnection});
        }
    };

    auto localSymbols = analysis->getSymbolsAtToken(declTok);
    for (auto* symbol : localSymbols) {
        addSymbol(symbol, analysis);
        if (auto* port = symbol->as_if<ast::InterfacePortSymbol>()) {
            if (auto* connection = analysis->getActiveInterfaceConnection(*port)) {
                for (auto* designSymbol : connection->sourcePath)
                    addSymbol(designSymbol, analysis, false);
            }
        }
    }
    if (std::ranges::any_of(localSymbols, [](const auto* symbol) {
            return symbol->kind == ast::SymbolKind::Genvar;
        })) {
        for (auto* parameter : analysis->getGenvarIterationParametersAtToken(declTok))
            addSymbol(parameter, analysis);
    }

    auto getGeneratedSignalCount = [&](const ast::Symbol* symbol) {
        if (symbol->kind != ast::SymbolKind::Net && symbol->kind != ast::SymbolKind::Variable)
            return size_t{1};

        bool isGenerated = false;
        for (auto* scope = symbol->getHierarchicalParent(); scope;
             scope = scope->asSymbol().getHierarchicalParent()) {
            if (scope->asSymbol().kind == ast::SymbolKind::GenerateBlock) {
                isGenerated = true;
                break;
            }
        }
        if (!isGenerated)
            return size_t{1};

        std::vector<const ast::Symbol*> generatedSignals;
        for (auto* candidate : localSymbols) {
            if ((candidate->kind == ast::SymbolKind::Net ||
                 candidate->kind == ast::SymbolKind::Variable) &&
                symbolsEquivalent(symbol, candidate) &&
                std::ranges::find(generatedSignals, candidate) == generatedSignals.end()) {
                generatedSignals.push_back(candidate);
            }
        }
        return std::max(size_t{1}, generatedSignals.size());
    };

    bool hasTopLevelResolution = std::ranges::any_of(localSymbols, [](const ast::Symbol* symbol) {
        return symbol->kind == ast::SymbolKind::Definition ||
               symbol->kind == ast::SymbolKind::Package;
    });
    bool hasLocalTopLevelResolution =
        std::ranges::any_of(localSymbols, [&](const ast::Symbol* symbol) {
            return (symbol->kind == ast::SymbolKind::Definition ||
                    symbol->kind == ast::SymbolKind::Package) &&
                   sm.getFullyExpandedLoc(symbol->location).buffer() == doc->getBuffer();
        });

    // A declaration in the queried document is authoritative. Other top-level names retain
    // every indexed definition tied at the nearest directory distance.
    bool searchIndex = localSymbols.empty() ||
                       (hasTopLevelResolution && !hasLocalTopLevelResolution);
    if (searchIndex) {
        auto matchesResolvedKind = [&](const ast::Symbol* candidate) {
            if (localSymbols.empty())
                return true;
            return std::ranges::any_of(localSymbols, [&](const ast::Symbol* resolved) {
                if (resolved->kind != candidate->kind)
                    return false;
                if (auto* resolvedDef = resolved->as_if<ast::DefinitionSymbol>()) {
                    auto* candidateDef = candidate->as_if<ast::DefinitionSymbol>();
                    return candidateDef &&
                           candidateDef->definitionKind == resolvedDef->definitionKind;
                }
                return true;
            });
        };

        auto nearest = m_indexer.getNearestSymbolLocs(declTok->valueText(),
                                                      doc->getURI().getPath());
        if (!nearest.empty()) {
            std::erase_if(symbols, [&](const auto& candidate) {
                if (candidate.symbol->kind != ast::SymbolKind::Definition &&
                    candidate.symbol->kind != ast::SymbolKind::Package)
                    return false;
                auto path = sm.getFullPath(
                    sm.getFullyExpandedLoc(candidate.symbol->location).buffer());
                return std::ranges::none_of(nearest, [&](const auto& entry) {
                    return URI::fromFile(*entry.uri) == URI::fromFile(path);
                });
            });
        }
        for (const auto& entry : nearest) {
            auto symbolDoc = getDocument(URI::fromFile(*entry.uri));
            if (!symbolDoc)
                continue;
            auto symbolAnalysis = symbolDoc->getAnalysis();
            auto addGlobalDefinition = [&](const ast::Symbol* symbol) {
                if (symbol && symbol->name == declTok->valueText() &&
                    sm.getFullyExpandedLoc(symbol->location).buffer() == symbolDoc->getBuffer() &&
                    matchesResolvedKind(symbol)) {
                    addSymbol(symbol, symbolAnalysis);
                }
            };

            for (auto* definition : symbolAnalysis->getCompilation()->getDefinitions())
                addGlobalDefinition(definition);

            for (auto* unit : symbolAnalysis->getCompilation()->getCompilationUnits()) {
                for (auto& member : unit->members()) {
                    if (member.kind == ast::SymbolKind::Package)
                        addGlobalDefinition(&member);
                }
            }
        }
    }

    auto makeSyntaxTarget =
        [&](const ast::Symbol* symbol) -> std::optional<DefinitionInfo::SyntaxTarget> {
        auto* symSyntax = symbol->getSyntax();
        if (!symSyntax) {
            if (auto* typeParam = symbol->as_if<ast::TypeParameterSymbol>())
                symSyntax = typeParam->getTypeAlias().getSyntax();
        }
        if (!symSyntax) {
            ERROR("Failed to get syntax for symbol {} of kind {}", symbol->name,
                  toString(symbol->kind));
            return {};
        }

        // Modports have a directional declaration around their name syntax.
        if ((symbol->kind == ast::SymbolKind::Modport ||
             symbol->kind == ast::SymbolKind::ModportPort) &&
            symSyntax->parent) {
            symSyntax = symSyntax->parent;
        }

        auto foundNameToken = findNameToken(symSyntax, symbol->name);
        if (!foundNameToken) {
            ERROR("Failed to find name token for symbol '{}' of kind {} = {}", symbol->name,
                  toString(symbol->kind), symSyntax->toString());
        }
        parsing::Token nameToken = foundNameToken ? *foundNameToken : symSyntax->getFirstToken();

        auto result = DefinitionInfo::SyntaxTarget::fromNode(symSyntax, nameToken, sm);
        if (sm.isMacroLoc(nameToken.location()) &&
            result.macroUsageRange == SourceRange::NoLocation) {
            ERROR("Couldn't get original range for symbol {}", nameToken.valueText());
        }
        return result;
    };

    auto makeSymbolTarget =
        [&](const ast::Symbol* symbol, const std::shared_ptr<ShallowAnalysis>& symbolAnalysis,
            bool renderInterfaceConnection = true) -> std::optional<DefinitionInfo::SymbolTarget> {
        if (!symbol)
            return {};
        auto syntaxTarget = makeSyntaxTarget(symbol);
        if (!syntaxTarget)
            return {};

        std::vector<DefinitionInfo::SyntaxTarget> syntaxes;
        syntaxes.push_back(std::move(*syntaxTarget));
        if (auto* modportPort = symbol->as_if<ast::ModportPortSymbol>()) {
            if (modportPort->internalSymbol) {
                if (auto internalSyntax = makeSyntaxTarget(modportPort->internalSymbol))
                    syntaxes.push_back(std::move(*internalSyntax));
            }
        }
        auto* designSymbol = symbolAnalysis->getDesignSymbol(*symbol);
        return DefinitionInfo::SymbolTarget{.syntaxes = std::move(syntaxes),
                                            .symbol = designSymbol ? designSymbol : symbol,
                                            .analysis = symbolAnalysis,
                                            .renderInterfaceConnection = renderInterfaceConnection,
                                            .generatedSignalCount = getGeneratedSignalCount(
                                                symbol)};
    };

    if (declSyntax && declSyntax->kind == syntax::SyntaxKind::NamedPortConnection &&
        !declSyntax->as<syntax::NamedPortConnectionSyntax>().openParen && localSymbols.size() > 1) {
        std::vector<DefinitionInfo::Target> targets;
        for (size_t i = 0; i + 1 < localSymbols.size(); i += 2) {
            auto outer = makeSymbolTarget(localSymbols[i], analysis);
            auto inner = makeSymbolTarget(localSymbols[i + 1], analysis);
            if (outer && inner) {
                inner->renderInputPortDriver = true;
                DefinitionInfo::PortConnectionTarget port{std::move(*outer), std::move(*inner)};
                auto duplicate = std::ranges::any_of(targets, [&](const auto& target) {
                    auto* existing = std::get_if<DefinitionInfo::PortConnectionTarget>(&target);
                    return existing &&
                           symbolsEquivalent(existing->outer.symbol, port.outer.symbol) &&
                           symbolsEquivalent(existing->inner.symbol, port.inner.symbol);
                });
                if (!duplicate)
                    targets.emplace_back(std::move(port));
            }
            else if (outer) {
                targets.emplace_back(std::move(*outer));
            }
            else if (inner) {
                targets.emplace_back(std::move(*inner));
            }
        }
        for (const auto& [symbol, symbolAnalysis, renderInterfaceConnection] : symbols) {
            auto alreadyRepresented = std::ranges::any_of(localSymbols, [&](const auto* local) {
                return local == symbol ||
                       (local->kind == symbol->kind && local->getSyntax() == symbol->getSyntax());
            });
            if (!alreadyRepresented) {
                if (auto target = makeSymbolTarget(symbol, symbolAnalysis,
                                                   renderInterfaceConnection)) {
                    targets.emplace_back(std::move(*target));
                }
            }
        }
        if (!targets.empty())
            return DefinitionInfo{sm, std::move(targets)};
    }

    std::vector<DefinitionInfo::Target> targets;
    std::vector<const ast::Symbol*> foldedSymbols;
    for (const auto& [symbol, symbolAnalysis, renderInterfaceConnection] : symbols) {
        if (std::ranges::find(foldedSymbols, symbol) != foldedSymbols.end())
            continue;

        auto target = makeSymbolTarget(symbol, symbolAnalysis, renderInterfaceConnection);
        if (!target)
            continue;

        if (auto* connection = declSyntax->as_if<syntax::NamedPortConnectionSyntax>();
            connection && connection->expr) {
            target->renderInputPortDriver = true;
        }

        if (auto* modportPort = symbol->as_if<ast::ModportPortSymbol>()) {
            if (modportPort->internalSymbol)
                foldedSymbols.push_back(modportPort->internalSymbol);
        }
        targets.emplace_back(std::move(*target));
    }

    if (targets.empty())
        return {};
    return DefinitionInfo{sm, std::move(targets)};
}

std::optional<std::string> ServerDriver::getDesignInstancePathAt(const URI& uri,
                                                                 const lsp::Position& position) {
    auto doc = getDocument(uri);
    if (!doc)
        return {};

    auto loc = toSourceLocation(doc->getBuffer(), position, sm);
    if (!loc)
        return {};

    auto analysis = doc->getAnalysis();
    return analysis->getDesignInstancePathAtToken(analysis->syntaxes.getWordTokenAt(*loc));
}

std::optional<lsp::Hover> ServerDriver::getDocHover(const URI& uri, const lsp::Position& position) {
    const auto doc = getDocument(uri);
    if (!doc) {
        return {};
    }
    auto loc = toSourceLocation(doc->getBuffer(), position, sm);
    if (!loc) {
        return {};
    }
    auto maybeInfo = getDefinitionInfoAt(uri, position);
    if (!maybeInfo) {
        if (s_debugHoversEnabled) {
            // Shows debug info for the token under cursor when debugging.
            auto analysis = doc->getAnalysis();
            markup::Document markup;
            markup.addParagraph(analysis->getDebugHover(loc.value()));
            return lsp::Hover{.contents = markup.build()};
        }
        return {};
    }
    const auto& info = *maybeInfo;
    return lsp::Hover{.contents = info.getHover(doc->getBuffer(), m_config.hovers.value())};
}

std::optional<std::vector<lsp::DocumentHighlight>> ServerDriver::getDocDocumentHighlight(
    const URI& uri, const lsp::Position& position) {
    auto doc = getDocument(uri);
    if (!doc) {
        return std::nullopt;
    }
    auto analysis = doc->getAnalysis();

    // Get the symbol at the position
    auto loc = toSourceLocation(doc->getBuffer(), position, sm);
    if (!loc) {
        return std::nullopt;
    }
    auto declTok = analysis->syntaxes.getWordTokenAt(loc.value());
    if (!declTok) {
        return std::nullopt;
    }
    auto symbol = analysis->getSymbolAtToken(declTok);
    if (!symbol) {
        return std::nullopt;
    }

    // Find all references to the symbol in the current document
    std::vector<lsp::Location> references;
    analysis->addLocalReferences(references, symbol->location, symbol->name);
    if (references.empty()) {
        return std::nullopt;
    }

    std::vector<lsp::DocumentHighlight> highlights;
    highlights.reserve(references.size());
    for (auto& ref : references) {
        highlights.push_back(lsp::DocumentHighlight{
            .range = ref.range,
        });
    }

    return highlights;
}

void ServerDriver::addMemberReferences(std::vector<lsp::Location>& references,
                                       const ast::Symbol& parentSymbol,
                                       const ast::Symbol& targetSymbol, bool isTypeMember,
                                       const lsp::RequestContext& ctx) {

    auto targetBuffer = sm.getFullyOriginalLoc(targetSymbol.location).buffer();
    auto targetDoc = getDocument(URI::fromFile(sm.getFullPath(targetBuffer)));
    auto targetName = targetSymbol.name;

    auto referencingFiles = m_indexer.getFilesReferencingSymbol(parentSymbol.name);
    for (auto& filePath : referencingFiles) {
        ctx.throwIfCancelled("while finding member references");
        URI fileUri = URI::fromFile(filePath.string());

        // Skip the file where targetSymbol is defined to avoid duplicates
        if (fileUri == targetDoc->getURI()) {
            continue;
        }

        auto fileDoc = getDocument(fileUri);
        if (!fileDoc) {
            continue;
        }

        // if a package, check if we can just use the package ref syntaxes to save on
        // making analysis
        if (!isTypeMember && parentSymbol.kind == ast::SymbolKind::Package) {
            auto& meta = fileDoc->getSyntaxTree()->getMetadata();
            bool hasWildcard = [&] {
                for (auto ref : meta.packageImports) {
                    for (auto item : ref->items) {
                        if (item->package.valueText() == parentSymbol.name &&
                            item->item.kind == parsing::TokenKind::Star) {
                            return true;
                        }
                    }
                }
                return false;
            }();
            if (!hasWildcard) {
                // no wildcard, just check cases of pkg::<targetName>
                for (auto ref : meta.classPackageNames) {
                    if (ref->identifier.valueText() != parentSymbol.name) {
                        continue;
                    }
                    auto tok = ref->parent->as<syntax::ScopedNameSyntax>().right->getFirstToken();
                    if (tok.valueText() == targetName) {
                        references.push_back(toOriginalLocation(tok.range(), sm));
                    }
                }
                continue;
            }
        }

        auto fileAnalysis = fileDoc->getAnalysis(ctx);
        fileAnalysis->addLocalReferences(references, targetSymbol.location, targetName);
    }
}

std::optional<std::vector<lsp::Location>> ServerDriver::getDocReferences(
    const URI& srcUri, const lsp::Position& position, bool includeDeclaration,
    const lsp::RequestContext& ctx) {
    ctx.throwIfCancelled("before finding references");
    auto doc = getDocument(srcUri);
    if (!doc) {
        return std::nullopt;
    }

    // Get the symbol at the position. Hold the analysis via shared_ptr so that symbols remain
    // valid even if getAnalysis() is called on this doc again.
    auto analysis = doc->getAnalysis(ctx);
    ctx.throwIfCancelled("before resolving reference target");
    auto loc = toSourceLocation(doc->getBuffer(), position, sm);
    if (!loc) {
        return std::nullopt;
    }

    const parsing::Token* declTok = analysis->syntaxes.getWordTokenAt(loc.value());
    if (!declTok) {
        return std::nullopt;
    }

    struct ReferenceTarget {
        const ast::Symbol* symbol;
        std::shared_ptr<ShallowAnalysis> analysis;
        BufferID analysisBuffer;
    };
    SmallVector<ReferenceTarget, 2> targetSymbols;
    auto addTargetSymbol = [&](const ast::Symbol* symbol,
                               const std::shared_ptr<ShallowAnalysis>& symbolAnalysis,
                               BufferID symbolAnalysisBuffer) {
        if (!symbol)
            return;

        // A top level of a shallow compilation is an instance body; get the definition instead
        if (symbol->kind == ast::SymbolKind::InstanceBody)
            symbol = &symbol->as<ast::InstanceBodySymbol>().getDefinition();

        auto symbolLocation = sm.getFullyOriginalLoc(symbol->location);
        auto duplicate = std::ranges::any_of(targetSymbols, [&](const ReferenceTarget& existing) {
            return existing.symbol == symbol ||
                   (existing.symbol->kind == symbol->kind &&
                    sm.getFullyOriginalLoc(existing.symbol->location) == symbolLocation);
        });
        if (!duplicate)
            targetSymbols.push_back({symbol, symbolAnalysis, symbolAnalysisBuffer});
    };

    auto addTargetsAt = [&](const std::shared_ptr<SlangDoc>& referenceDoc,
                            const std::shared_ptr<ShallowAnalysis>& referenceAnalysis,
                            const parsing::Token* token) {
        auto symbols = referenceAnalysis->getSymbolsAtToken(token);
        bool hasGlobal = std::ranges::any_of(symbols, [](const auto* symbol) {
            return symbol->kind == ast::SymbolKind::Definition ||
                   symbol->kind == ast::SymbolKind::Package ||
                   symbol->kind == ast::SymbolKind::InstanceBody;
        });
        if (hasGlobal || symbols.empty()) {
            auto info = getDefinitionInfoAt(referenceDoc->getURI(),
                                            toRange(token->range(), sm).start);
            if (info) {
                bool found = false;
                for (const auto& entry : info->targets) {
                    auto* target = std::get_if<DefinitionInfo::SymbolTarget>(&entry);
                    if (target && (target->symbol->kind == ast::SymbolKind::Definition ||
                                   target->symbol->kind == ast::SymbolKind::Package)) {
                        addTargetSymbol(target->symbol, target->analysis,
                                        referenceDoc->getBuffer());
                        found = true;
                    }
                }
                if (found)
                    return;
            }
        }
        for (auto* symbol : symbols)
            addTargetSymbol(symbol, referenceAnalysis, referenceDoc->getBuffer());
    };

    addTargetsAt(doc, analysis, declTok);

    if (targetSymbols.empty())
        return std::nullopt;

    std::vector<lsp::Location> references;

    auto targetName = declTok->rawText();

    auto findPkgReferencesInDocument = [&](const parsing::ParserMetadata& meta, const URI&) {
        for (auto ref : meta.packageImports) {
            for (auto item : ref->items) {
                if (item->package.valueText() == targetName) {
                    references.push_back(toOriginalLocation(item->package.range(), sm));
                }
            }
        }
        for (auto ref : meta.classPackageNames) {
            if (ref->identifier.valueText() == targetName) {
                references.push_back(toOriginalLocation(ref->identifier.range(), sm));
            }
        }
    };

    auto findModuleReferencesInDocument = [&](const parsing::ParserMetadata& meta, const URI&) {
        for (auto inst : meta.globalInstances) {
            if (inst->type.valueText() == targetName) {
                references.push_back(toOriginalLocation(inst->type.range(), sm));
            }
        }
    };

    auto findInterfaceReferencesInDocument = [&](const parsing::ParserMetadata& meta, const URI&) {
        for (auto inst : meta.globalInstances) {
            if (inst->type.valueText() == targetName) {
                references.push_back(toOriginalLocation(inst->type.range(), sm));
            }
        }
        for (auto intf : meta.interfacePorts) {
            if (intf->nameOrKeyword.valueText() == targetName) {
                references.push_back(toOriginalLocation(intf->nameOrKeyword.range(), sm));
            }
        }
    };

    for (size_t targetIndex = 0; targetIndex < targetSymbols.size(); targetIndex++) {
        ctx.throwIfCancelled("while finding references");
        const auto& target = targetSymbols[targetIndex];
        const auto* targetSymbol = target.symbol;
        const auto existingReferenceCount = references.size();
        auto targetLoc = sm.getFullyOriginalLoc(targetSymbol->location);
        auto targetDoc = getDocument(URI::fromFile(sm.getFullPath(targetLoc.buffer())));

        const ast::Symbol* indexedSymbol = targetSymbol;
        while (indexedSymbol && indexedSymbol->kind != ast::SymbolKind::Definition &&
               indexedSymbol->kind != ast::SymbolKind::Package) {
            if (auto* body = indexedSymbol->as_if<ast::InstanceBodySymbol>()) {
                indexedSymbol = &body->getDefinition();
                break;
            }
            auto* parent = indexedSymbol->getParentScope();
            indexedSymbol = parent ? &parent->asSymbol() : nullptr;
        }
        const bool hasDuplicates = indexedSymbol &&
                                   m_indexer.getFilesForSymbol(indexedSymbol->name).size() > 1;

        // Helper to process referencing files with a given finder function
        auto processReferencingFiles = [&](std::string_view name, auto&& finder) {
            for (const auto& filePath : m_indexer.getFilesReferencingSymbol(name)) {
                ctx.throwIfCancelled("while finding references");
                if (targetDoc && filePath == targetDoc->getURI().getPath())
                    continue;

                URI fileUri = URI::fromFile(filePath.string());
                auto fileDoc = getDocument(fileUri);
                if (fileDoc) {
                    finder(fileDoc->getSyntaxTree()->getMetadata(), fileUri);
                }
                else {
                    ctx.error("No doc found for {}", filePath.string());
                }
            }
        };

        // Add refs in declaration file, and remove declaration if requested
        if (targetDoc) {
            auto targetAnalysis = targetDoc->getAnalysis(ctx);
            targetAnalysis->addLocalReferences(references, targetSymbol->location, targetName);
            if (!includeDeclaration) {
                auto targetLspLoc = lsp::Location{
                    .uri = URI::fromFile(sm.getFullPath(targetLoc.buffer())),
                    .range = toRange(SourceRange(targetLoc, targetLoc + targetSymbol->name.size()),
                                     sm),
                };
                references.erase(std::remove_if(references.begin(), references.end(),
                                                [&](const lsp::Location& loc) {
                                                    return loc.uri == targetLspLoc.uri &&
                                                           loc.range == targetLspLoc.range;
                                                }),
                                 references.end());
            }
        }

        // Add global references
        switch (targetSymbol->kind) {
            case ast::SymbolKind::Instance: {
                processReferencingFiles(
                    targetSymbol->as<ast::InstanceSymbol>().getDefinition().name,
                    findModuleReferencesInDocument);
            } break;
            case ast::SymbolKind::InstanceBody: {
                processReferencingFiles(
                    targetSymbol->as<ast::InstanceBodySymbol>().getDefinition().name,
                    findModuleReferencesInDocument);
            } break;
            case ast::SymbolKind::Definition: {
                const auto& definition = targetSymbol->as<ast::DefinitionSymbol>();
                if (definition.definitionKind == ast::DefinitionKind::Interface) {
                    processReferencingFiles(definition.name, findInterfaceReferencesInDocument);
                }
                else {
                    processReferencingFiles(definition.name, findModuleReferencesInDocument);
                }
            } break;
            case ast::SymbolKind::Package: {
                processReferencingFiles(targetName, findPkgReferencesInDocument);
            } break;
            default: {
                if (targetSymbol->getParentScope() == nullptr ||
                    targetSymbol->getParentScope()->asSymbol().getParentScope() == nullptr) {
                    ctx.error(
                        "Target symbol {}: {} has no parent scope, missed kind case for global "
                        "symbol",
                        targetName, toString(targetSymbol->kind));
                    break;
                }
                auto& parentSymbol = targetSymbol->getParentScope()->asSymbol();
                auto& gParentSymbol = parentSymbol.getParentScope()->asSymbol();
                if (gParentSymbol.kind == ast::SymbolKind::CompilationUnit) {
                    // Package and module members
                    addMemberReferences(references, parentSymbol, *targetSymbol, false, ctx);
                }
                else if (gParentSymbol.kind == ast::SymbolKind::Package &&
                         ast::Type::isKind(parentSymbol.kind)) {
                    // submembers in the case of structs and enums
                    addMemberReferences(references, gParentSymbol, *targetSymbol, true, ctx);
                }
                else if (targetLoc.buffer() != target.analysisBuffer) {
                    target.analysis->addLocalReferences(references, targetSymbol->location,
                                                        targetName);
                }
            }
        }

        if (hasDuplicates) {
            const auto targetLocation = toOriginalLocation(SourceRange(targetLoc, targetLoc), sm);
            auto begin = references.begin() + existingReferenceCount;
            references.erase(
                std::remove_if(
                    begin, references.end(),
                    [&](const auto& reference) {
                        ctx.throwIfCancelled("while matching reference declarations");
                        auto info = getDefinitionInfoAt(reference.uri, reference.range.start);
                        if (!info)
                            return true;
                        auto definitions = info->getDefinitionLspLinks();
                        return std::ranges::none_of(definitions, [&](const auto& definition) {
                            return definition.targetUri == targetLocation.uri &&
                                   definition.targetSelectionRange.start ==
                                       targetLocation.range.start;
                        });
                    }),
                references.end());
        }

        const auto newReferenceEnd = references.size();
        for (size_t i = existingReferenceCount; i < newReferenceEnd; i++) {
            ctx.throwIfCancelled("while resolving reference components");
            auto referenceDoc = getDocument(references[i].uri);
            if (!referenceDoc || !referenceDoc->hasAnalysis())
                continue;

            auto referenceAnalysis = referenceDoc->getAnalysis(ctx);
            auto referenceLoc = toSourceLocation(referenceDoc->getBuffer(),
                                                 references[i].range.start, sm);
            if (!referenceLoc)
                continue;

            auto* referenceToken = referenceAnalysis->syntaxes.getWordTokenAt(*referenceLoc);
            if (!referenceToken)
                continue;

            // Tied declarations and multi-symbol tokens join the same reference component.
            addTargetsAt(referenceDoc, referenceAnalysis, referenceToken);
        }

        auto output = references.begin() + existingReferenceCount;
        for (auto it = output; it != references.end(); ++it) {
            auto duplicate = std::any_of(references.begin(),
                                         references.begin() + existingReferenceCount,
                                         [&](const lsp::Location& existing) {
                                             return existing.uri == it->uri &&
                                                    existing.range == it->range;
                                         });
            if (!duplicate) {
                if (output != it)
                    *output = std::move(*it);
                ++output;
            }
        }
        references.erase(output, references.end());
    }

    return references.empty() ? std::nullopt : std::make_optional(std::move(references));
}

std::optional<lsp::WorkspaceEdit> ServerDriver::getDocRename(const URI& uri,
                                                             const lsp::Position& position,
                                                             std::string_view newName) {
    // Reuse getDocReferences to find all locations (including declaration)
    auto references = getDocReferences(uri, position, /* includeDeclaration */ true);
    if (!references || references->empty()) {
        return std::nullopt;
    }

    // Group edits by URI
    std::unordered_map<std::string, std::vector<lsp::TextEdit>> changes;

    for (const auto& loc : *references) {
        lsp::TextEdit edit{
            .range = loc.range,
            .newText = std::string(newName),
        };
        changes[loc.uri.str()].push_back(edit);
    }

    return lsp::WorkspaceEdit{.changes = changes};
}

void ServerDriver::publishInactiveRegions(SlangDoc& doc, const lsp::RequestContext& ctx) {
    if (!client.capabilities.inactiveRegionsSupported)
        return;

    ctx.throwIfCancelled("before collecting inactive regions");
    auto regions = doc.getInactiveRegions(ctx);
    ctx.info("Collected {} inactive regions for {}", regions.size(), doc.getWsRelativePath());
    ctx.throwIfCancelled("before publishing inactive regions");

    client.onTextDocumentInactiveRegions(lsp::InactiveRegionsParams{
        .uri = doc.getURI(),
        .regions = std::move(regions),
    });
}

} // namespace server
