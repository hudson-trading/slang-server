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
                           std::vector<std::string> buildfiles) :
    sm(driver.sourceManager), diagEngine(driver.diagEngine), client(client),
    diagClient(std::make_shared<ServerDiagClient>(sm, client)),
    completions(*this, indexer, sm, options), codeActions(*this, sm), m_indexer(indexer),
    m_config(config) {
    parseAndLoadSources(buildfiles);
}

void ServerDriver::parseAndLoadSources(const std::vector<std::string>& buildfiles) {
    driver.addStandardArgs();
    diagEngine.removeClient(driver.textDiagClient);
    diagEngine.addClient(diagClient);

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
    if (!ok) {
        client.showError("Failed to parse config flags");
    }

    for (auto& buildfile : buildfiles) {
        ok = driver.processCommandFiles(buildfile, m_config.buildRelativePaths.value(), false);
        if (ok) {
            INFO("Processed build file: {}", buildfile);
        }
        else {
            client.showError(fmt::format("Failed to process build file: {}", buildfile));
        }
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
    ok = driver.parseAllSources();
    diagEngine.setMappingsFromPragmas();

    // Create documents from syntax trees
    INFO("Creating ServerDriver with {} trees", driver.syntaxTrees.size());
    for (auto& tree : driver.syntaxTrees) {
        auto uri = URI::fromFile(sm.getFullPath(tree->getSourceBufferIds()[0]));
        auto doc = SlangDoc::fromTree(*this, std::move(tree));
        docs[uri] = doc;
    }
}

// Doc updates (open, change, save)
void ServerDriver::updateDoc(SlangDoc& doc, FileUpdateType type) {
    // Grab dependent documents
    doc.setDependentDocuments(getDependentDocs(doc.getSyntaxTree()));

    // Clear and re-issue diagnostics for this document
    diagClient->clear(doc.getURI());

    // Update pragma mappings for the changed buffer
    diagEngine.setMappingsFromPragmas(doc.getBuffer());

    if (comp && type == FileUpdateType::SAVE) {
        // Clear just the data structures; add all uris to dirty set
        diagClient->clear();

        // Re-issue parse diagnostics for all documents, since we cleared
        for (const auto& [uri, d] : docs) {
            d->issueParseDiagnostics(diagEngine);
        }
        // Elaborate; Issue semantic diagnostics from full compilation
        comp->refresh();
        comp->issueDiagnosticsTo(diagEngine);
    }
    else {
        // In explore mode: issue normal shallow diags on changes
        doc.issueDiagnosticsTo(diagEngine);
    }
    diagClient->pushDiags(doc.getURI());
    INFO("Published diags for {}", doc.getURI().getPath());

    publishInactiveRegions(doc);
}

std::unique_ptr<ServerDriver> ServerDriver::create(Indexer& indexer, SlangLspClient& client,
                                                   const Config& config,
                                                   std::vector<std::string> buildfiles,
                                                   const ServerDriver* oldDriver) {
    auto newDriver = std::make_unique<ServerDriver>(indexer, client, config, buildfiles);

    // Copy only open documents from old driver if provided
    if (oldDriver) {
        newDriver->completions.resolveEdits = oldDriver->completions.resolveEdits;
        oldDriver->diagClient->clearAndPush();
        for (const auto& uri : oldDriver->m_openDocs) {
            auto docIt = oldDriver->docs.find(uri);
            if (docIt == oldDriver->docs.end()) {
                ERROR("Open Doc {} not found in old driver", uri.getPath());
                continue;
            }
            // Only copy if the URI isn't already in the new driver's docs
            auto newDocit = newDriver->docs.find(uri);
            if (newDocit == newDriver->docs.end()) {
                // Open the document in the new driver using the text from the old document
                newDriver->openDocument(uri, docIt->second->getText());
                // Trigger diagnostics for the newly opened document
            }
            else {
                // Publish diags for the existing document
                // Add to open doc set
                newDriver->m_openDocs.insert(uri);
                newDriver->updateDoc(*newDocit->second, FileUpdateType::OPEN);
            }
        }
    }

    return newDriver;
}

void ServerDriver::openDocument(const URI& uri, const std::string_view text) {
    auto docIter = docs.find(uri);
    std::shared_ptr<SlangDoc> doc;
    bool alreadyInBuild = false;
    if (docIter != docs.end() && docIter->second->textMatches(text)) {
        doc = docIter->second;
        alreadyInBuild = true;
    }
    else {
        if (docIter != docs.end())
            WARN("Document {} text does not match, updating", uri.getPath());
        doc = SlangDoc::fromText(*this, uri, text);
        docs[uri] = doc;
    }

    if (comp && alreadyInBuild) {
        // File is already part of the compilation — compilation diags were
        // already published, so skip shallow diags. Still publish inactive regions.
        publishInactiveRegions(*doc);
    }
    else {
        updateDoc(*doc, FileUpdateType::OPEN);
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

void ServerDriver::onDocDidChange(const lsp::DidChangeTextDocumentParams& params) {
    std::string_view path = params.textDocument.uri.getPath();
    auto doc = getDocument(params.textDocument.uri);
    if (!doc) {
        ERROR("Document {} not found", path);
        return;
    }

    doc->onChange(params.contentChanges);
    // Update Tree and Compilation
    updateDoc(*doc, FileUpdateType::CHANGE);
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
    updateDoc(*doc, FileUpdateType::CHANGE);
}

void ServerDriver::onWorkspaceDidChangeWatchedFiles(
    const lsp::DidChangeWatchedFilesParams& params) {
    // Collect docs that need updating after all buffers are reloaded
    std::vector<std::shared_ptr<SlangDoc>> updatedDocs;

    for (const auto& change : params.changes) {
        switch (change.type) {
            case lsp::FileChangeType::Changed: {
                // Only reload if this is an open document
                if (m_openDocs.find(change.uri) == m_openDocs.end()) {
                    continue;
                }

                auto doc = getDocument(change.uri);
                if (!doc) {
                    WARN("Document {} not found for reload", change.uri.getPath());
                    continue;
                }

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
            case lsp::FileChangeType::Created:
                break;
        }
    }

    // Update all open docs after all buffers have been reloaded
    for (auto& doc : updatedDocs) {
        updateDoc(*doc, FileUpdateType::CHANGE);
    }
}

std::vector<std::shared_ptr<SlangDoc>> ServerDriver::getDependentDocs(
    std::shared_ptr<SyntaxTree> tree) {
    std::vector<std::shared_ptr<SlangDoc>> result;
    std::queue<std::shared_ptr<SyntaxTree>> treesToProcess;
    flat_hash_set<std::string_view> knownNames;
    flat_hash_set<std::string> processedFiles;

    treesToProcess.push(tree);

    while (!treesToProcess.empty()) {
        auto currentTree = treesToProcess.front();
        treesToProcess.pop();

        auto& meta = currentTree->getMetadata();

        // Collect declared symbols from current tree
        meta.visitDeclaredSymbols([&](std::string_view name) { knownNames.emplace(name); });

        auto loadDependency = [&](std::string_view name) {
            if (knownNames.find(name) != knownNames.end())
                return; // already added

            // Don't try multiple times
            knownNames.emplace(name);
            auto symbolLoc = m_indexer.getFirstSymbolLoc(name);
            if (!symbolLoc)
                return;

            std::string filePath = symbolLoc->uri->string();

            // Check if we've already processed this file to avoid cycles
            if (processedFiles.find(filePath) != processedFiles.end())
                return;

            processedFiles.insert(filePath);

            auto newdoc = getDocument(URI::fromFile(filePath));
            if (newdoc) {
                result.push_back(newdoc);
                docs[newdoc->getURI()] = newdoc;

                // Recurse into packages and interfaces, since they may contain types from other
                // packages that are referenced by the analyzed module.
                for (auto& [decl, _] : newdoc->getSyntaxTree()->getMetadata().nodeMeta) {
                    if (decl->kind == syntax::SyntaxKind::PackageDeclaration ||
                        decl->kind == syntax::SyntaxKind::InterfaceDeclaration) {
                        treesToProcess.push(newdoc->getSyntaxTree());
                        break;
                    }
                }
            }
            else {
                ERROR("No doc found for {}", filePath);
            }
        };

        meta.visitReferencedSymbols(loadDependency);
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

bool ServerDriver::createCompilation(std::shared_ptr<SlangDoc> doc, std::string_view top) {
    // Collect documents starting with the target document
    std::vector<std::shared_ptr<syntax::SyntaxTree>> syntaxTrees{doc->getSyntaxTree()};
    driver::SourceLoader::loadTrees(
        syntaxTrees,
        [this](std::string_view name) {
            auto paths = m_indexer.getFilesForSymbol(name);
            if (!paths.empty()) {
                auto maybeBuf = sm.readSource(paths[0], /* library */ nullptr);
                if (maybeBuf) {
                    return *maybeBuf;
                }
                else {
                    ERROR("Failed to read source for {}: {}", paths[0].string(),
                          maybeBuf.error().message());
                }
            }
            return SourceBuffer{};
        },
        sm, this->options);

    std::vector<std::shared_ptr<SlangDoc>> documents;
    documents.reserve(syntaxTrees.size());
    for (const auto& tree : syntaxTrees) {
        documents.push_back(SlangDoc::fromTree(*this, tree));
    }
    // insert the documents into the driver
    for (const auto& doc : documents) {
        docs[doc->getURI()] = doc;
    }

    comp = std::make_unique<ServerCompilation>(documents, this->options, sm, std::string(top));

    // Apply pragma mappings for all buffers (including newly loaded ones)
    diagEngine.setMappingsFromPragmas();

    // Publish initial diags
    for (const auto& doc : documents) {
        doc->issueParseDiagnostics(diagEngine);
    }
    comp->issueDiagnosticsTo(diagEngine);
    diagClient->pushDiags();

    return true;
}

bool ServerDriver::createCompilation() {
    // Collect all documents
    std::vector<std::shared_ptr<SlangDoc>> documents;

    for (const auto& [uri, doc] : docs) {
        if (doc->getSyntaxTree()) {
            documents.push_back(doc);
        }
        else {
            ERROR("Document {} has no syntax tree", uri.getPath());
        }
    }

    if (documents.empty()) {
        ERROR("No documents available for compilation");
        return false;
    }

    comp = std::make_unique<ServerCompilation>(std::move(documents), this->options, sm);

    // Apply pragma mappings for all buffers
    diagEngine.setMappingsFromPragmas();

    // Issue parse diagnostics for all documents + semantic diagnostics from compilation
    // This ensures that when a user opens a document later, the diagnostics don't disappear
    diagClient->clear();
    for (const auto& [uri, doc] : docs) {
        doc->issueParseDiagnostics(diagEngine);
    }

    // Issue semantic diagnostics from the compilation
    comp->issueDiagnosticsTo(diagEngine);
    diagClient->pushDiags();
    return true;
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
        auto macroUsageRange = SourceRange::NoLocation;
        if (sm.isMacroLoc(nameToken.location())) {
            auto tokenRange = SourceRange(nameToken.location(),
                                          nameToken.location() + nameToken.rawText().length());
            auto expansionRange = sm.getFullyExpandedRange(tokenRange);
            if (sm.getSourceText(expansionRange).empty()) {
                ERROR("Couldn't get original range for macro {}", nameToken.valueText());
            }
            else {
                macroUsageRange = expansionRange;
            }
        }

        DefinitionInfo::SyntaxTarget syntaxTarget{macroDef, nameToken, macroUsageRange};
        DefinitionInfo::MacroTarget::Definition macroDefinition = syntaxTarget;

        const auto defPath = sm.getFullPath(nameToken.location().buffer());
        const auto defPathStr = defPath.filename().string();
        if (defPathStr.empty() || defPathStr[0] == '<') {
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

    std::vector<std::pair<const ast::Symbol*, std::shared_ptr<ShallowAnalysis>>> symbols;
    auto addSymbol = [&](const ast::Symbol* symbol,
                         const std::shared_ptr<ShallowAnalysis>& symbolAnalysis) {
        if (!symbol)
            return;
        auto duplicate = std::ranges::any_of(symbols, [&](const auto& existing) {
            return (existing.first == symbol && existing.second == symbolAnalysis) ||
                   (existing.second != symbolAnalysis && existing.first->kind == symbol->kind &&
                    existing.first->location == symbol->location) ||
                   symbolsEquivalent(existing.first, symbol);
        });
        if (!duplicate)
            symbols.emplace_back(symbol, symbolAnalysis);
    };

    auto localSymbols = analysis->getSymbolsAtToken(declTok);
    for (auto* symbol : localSymbols)
        addSymbol(symbol, analysis);
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

    // A declaration in the queried document is authoritative. Externally resolved top-level
    // names still use the index to retain other possible definitions.
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

        std::vector<std::filesystem::path> visited;
        auto paths = m_indexer.getFilesForSymbol(declTok->valueText());
        if (paths.size() > 1)
            std::ranges::sort(paths);
        for (const auto& path : paths) {
            if (std::ranges::find(visited, path) != visited.end())
                continue;
            visited.push_back(path);

            auto symbolDoc = getDocument(URI::fromFile(path));
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

        auto macroUsageRange = SourceRange::NoLocation;
        if (sm.isMacroLoc(nameToken.location())) {
            auto tokenRange = SourceRange(nameToken.location(),
                                          nameToken.location() + nameToken.rawText().length());
            auto expansionRange = sm.getFullyExpandedRange(tokenRange);
            if (sm.getSourceText(expansionRange).empty()) {
                ERROR("Couldn't get original range for symbol {}", nameToken.valueText());
            }
            else {
                macroUsageRange = expansionRange;
            }
        }

        return DefinitionInfo::SyntaxTarget{symSyntax, nameToken, macroUsageRange};
    };

    auto makeSymbolTarget = [&](const ast::Symbol* symbol,
                                const std::shared_ptr<ShallowAnalysis>& symbolAnalysis)
        -> std::optional<DefinitionInfo::SymbolTarget> {
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
        return DefinitionInfo::SymbolTarget{.syntaxes = std::move(syntaxes),
                                            .symbol = symbol,
                                            .analysis = symbolAnalysis,
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
        if (!targets.empty())
            return DefinitionInfo{sm, std::move(targets)};
    }

    std::vector<DefinitionInfo::Target> targets;
    std::vector<const ast::Symbol*> foldedSymbols;
    for (const auto& [symbol, symbolAnalysis] : symbols) {
        if (std::ranges::find(foldedSymbols, symbol) != foldedSymbols.end())
            continue;

        auto target = makeSymbolTarget(symbol, symbolAnalysis);
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
                                       const ast::Symbol& targetSymbol, bool isTypeMember) {

    auto targetBuffer = sm.getFullyOriginalLoc(targetSymbol.location).buffer();
    auto targetDoc = getDocument(URI::fromFile(sm.getFullPath(targetBuffer)));
    auto targetName = targetSymbol.name;

    auto referencingFiles = m_indexer.getFilesReferencingSymbol(parentSymbol.name);
    for (auto& filePath : referencingFiles) {
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
                    auto tok = ref->parent->as<ScopedNameSyntax>().right->getFirstToken();
                    if (tok.valueText() == targetName) {
                        references.push_back(toOriginalLocation(tok.range(), sm));
                    }
                }
                continue;
            }
        }

        auto fileAnalysis = fileDoc->getAnalysis();
        fileAnalysis->addLocalReferences(references, targetSymbol.location, targetName);
    }
}

std::optional<std::vector<lsp::Location>> ServerDriver::getDocReferences(
    const URI& srcUri, const lsp::Position& position, bool includeDeclaration) {
    auto doc = getDocument(srcUri);
    if (!doc) {
        return std::nullopt;
    }

    // Get the symbol at the position. Hold the analysis via shared_ptr so that symbols remain
    // valid even if getAnalysis() is called on this doc again.
    auto analysis = doc->getAnalysis();
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

    for (auto* symbol : analysis->getSymbolsAtToken(declTok))
        addTargetSymbol(symbol, analysis, doc->getBuffer());

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
        auto target = targetSymbols[targetIndex];
        const auto* targetSymbol = target.symbol;
        const auto existingReferenceCount = references.size();
        auto targetLoc = sm.getFullyOriginalLoc(targetSymbol->location);
        auto targetDoc = getDocument(URI::fromFile(sm.getFullPath(targetLoc.buffer())));

        // Helper to process referencing files with a given finder function
        auto processReferencingFiles = [&](std::string_view name, auto&& finder) {
            for (const auto& filePath : m_indexer.getFilesReferencingSymbol(name)) {
                if (targetDoc && filePath == targetDoc->getURI().getPath())
                    continue;

                URI fileUri = URI::fromFile(filePath.string());
                auto fileDoc = getDocument(fileUri);
                if (fileDoc) {
                    finder(fileDoc->getSyntaxTree()->getMetadata(), fileUri);
                }
                else {
                    ERROR("No doc found for {}", filePath.string());
                }
            }
        };

        // Add refs in declaration file, and remove declaration if requested
        if (targetDoc) {
            auto targetAnalysis = targetDoc->getAnalysis();
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
                    ERROR("Target symbol {}: {} has no parent scope, missed kind case for global "
                          "symbol",
                          targetName, toString(targetSymbol->kind));
                    break;
                }
                auto& parentSymbol = targetSymbol->getParentScope()->asSymbol();
                auto& gParentSymbol = parentSymbol.getParentScope()->asSymbol();
                if (gParentSymbol.kind == ast::SymbolKind::CompilationUnit) {
                    // Package and module members
                    addMemberReferences(references, parentSymbol, *targetSymbol);
                }
                else if (gParentSymbol.kind == ast::SymbolKind::Package &&
                         ast::Type::isKind(parentSymbol.kind)) {
                    // submembers in the case of structs and enums
                    addMemberReferences(references, gParentSymbol, *targetSymbol, true);
                }
                else if (targetLoc.buffer() != target.analysisBuffer) {
                    target.analysis->addLocalReferences(references, targetSymbol->location,
                                                        targetName);
                }
            }
        }

        const auto newReferenceEnd = references.size();
        for (size_t i = existingReferenceCount; i < newReferenceEnd; i++) {
            auto referenceDoc = getDocument(references[i].uri);
            if (!referenceDoc || !referenceDoc->hasAnalysis())
                continue;

            auto referenceLoc = toSourceLocation(referenceDoc->getBuffer(),
                                                 references[i].range.start, sm);
            if (!referenceLoc)
                continue;

            auto referenceAnalysis = referenceDoc->getAnalysis();
            auto* referenceToken = referenceAnalysis->syntaxes.getWordTokenAt(*referenceLoc);
            if (!referenceToken)
                continue;

            auto referenceSymbols = referenceAnalysis->getSymbolsAtToken(referenceToken);
            if (referenceSymbols.size() < 2)
                continue;

            // A multi-symbol reference joins its symbols into the same reference component.
            for (auto* symbol : referenceSymbols) {
                addTargetSymbol(symbol, referenceAnalysis, referenceDoc->getBuffer());
            }
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

void ServerDriver::publishInactiveRegions(SlangDoc& doc) {
    if (!client.capabilities.inactiveRegionsSupported)
        return;

    auto regions = doc.getInactiveRegions();

    client.onTextDocumentInactiveRegions(lsp::InactiveRegionsParams{
        .uri = doc.getURI(),
        .regions = std::move(regions),
    });
}

} // namespace server
