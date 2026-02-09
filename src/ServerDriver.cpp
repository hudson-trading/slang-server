//------------------------------------------------------------------------------
// ServerDriver.cpp
// Implementation of server driver class for processing syntax trees
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ServerDriver.h"

#include "Hovers.h"
#include "Indexer.h"
#include "ServerDiagClient.h"
#include "ast/ServerCompilation.h"
#include "completions/CompletionDispatch.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include "util/Markdown.h"
#include <memory>
#include <queue>
#include <string_view>

#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/driver/Driver.h"
#include "slang/driver/SourceLoader.h"
#include "slang/parsing/ParserMetadata.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {
using namespace slang;

ServerDriver::ServerDriver(Indexer& indexer, SlangLspClient& client, const Config& config,
                           std::vector<std::string> buildfiles) :
    sm(driver.sourceManager), diagEngine(driver.diagEngine), client(client),
    diagClient(std::make_shared<ServerDiagClient>(sm, client)),
    completions(*this, indexer, sm, options), m_indexer(indexer), m_config(config) {
    // Create and configure the driver with our source manager and diagnostic engine
    // SourceManager must not be moved, since diagEngine, syntax trees hold references to it
    driver.addStandardArgs();

    // Parse command line and process files
    slang::CommandLine::ParseOptions parseOpts;
    parseOpts.expandEnvVars = true;
    parseOpts.ignoreProgramName = true;
    parseOpts.supportComments = true;
    parseOpts.ignoreDuplicates = true;

    bool ok = driver.parseCommandLine(m_config.flags.value(), parseOpts);
    driver.options.errorLimit = 0;
    ok &= driver.processOptions(false);
    if (!ok) {
        client.showError(fmt::format("Failed to parse config flags: {}", m_config.flags.value()));
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

    // Configure diagnostic engine
    diagEngine.setIgnoreAllWarnings(false);
    diagEngine.setIgnoreAllNotes(false);
    diagEngine.addClient(diagClient);

    options = driver.createOptionBag();
    options.set(driver.getAnalysisOptions());
    ok = driver.parseAllSources();

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
}

std::unique_ptr<ServerDriver> ServerDriver::create(Indexer& indexer, SlangLspClient& client,
                                                   const Config& config,
                                                   std::vector<std::string> buildfiles,
                                                   const ServerDriver* oldDriver) {
    auto newDriver = std::make_unique<ServerDriver>(indexer, client, config, buildfiles);

    // Copy only open documents from old driver if provided
    if (oldDriver) {
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
    bool readText = true;
    if (docs.find(uri) != docs.end()) {
        if (docs[uri]->textMatches(text)) {
            readText = false;
        }
        else {
            WARN("Document {} text does not match, updating", uri.getPath());
        }
    }
    if (readText) {
        auto doc = SlangDoc::fromText(*this, uri, text);
        docs[uri] = doc;
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

        meta.visitReferencedSymbols([&](std::string_view name) {
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

                // Only add packages to the queue for recursive processing
                for (auto& [decl, _] : newdoc->getSyntaxTree()->getMetadata().nodeMeta) {
                    if (decl->kind == syntax::SyntaxKind::PackageDeclaration) {
                        treesToProcess.push(newdoc->getSyntaxTree());
                        break;
                    }
                }
            }
            else {
                ERROR("No doc found for {}", filePath);
            }
        });
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

std::optional<DefinitionInfo> ServerDriver::getDefinitionInfoAt(const URI& uri,
                                                                const lsp::Position& position) {
    auto doc = getDocument(uri);
    if (!doc) {
        return {};
    }
    auto& analysis = doc->getAnalysis();

    // Get location, token, and syntax node at position
    auto loc = sm.getSourceLocation(doc->getBuffer(), position.line, position.character);
    if (!loc) {
        return {};
    }
    const parsing::Token* declTok = analysis.syntaxes.getWordTokenAt(loc.value());
    if (!declTok) {
        return {};
    }
    const syntax::SyntaxNode* declSyntax = analysis.syntaxes.getTokenParent(declTok);
    if (!declSyntax) {
        return {};
    }

    std::optional<parsing::Token> nameToken;
    const syntax::SyntaxNode* symSyntax = nullptr;
    const ast::Symbol* symbol = nullptr;

    if (declTok->kind == parsing::TokenKind::Directive &&
        (declSyntax->kind == syntax::SyntaxKind::MacroUsage ||
         (declSyntax->kind == syntax::SyntaxKind::TokenList &&
          declSyntax->parent->kind == syntax::SyntaxKind::DefineDirective))) {
        // look in macro list
        auto macro = analysis.macros.find(declTok->rawText().substr(1));
        if (macro == analysis.macros.end()) {
            // TODO: Check workspace indexer for macro in other files
            auto files = m_indexer.getFilesForMacro(declTok->rawText().substr(1));
            if (files.empty()) {
                return {};
            }
            auto macroDoc = getDocument(URI::fromFile(files[0].string()));
            if (!macroDoc) {
                return {};
            }
            auto& macroAnalysis = macroDoc->getAnalysis();
            macro = macroAnalysis.macros.find(declTok->rawText().substr(1));
            if (macro == macroAnalysis.macros.end()) {
                return {};
            }
        }
        symSyntax = macro->second;
        nameToken = macro->second->name;
    }
    else {
        symbol = analysis.getSymbolAtToken(declTok);
        if (!symbol) {
            // check the index
            auto symbols = m_indexer.getFilesForSymbol(declTok->rawText());
            if (symbols.empty()) {
                return {};
            }
            auto symDoc = getDocument(URI::fromFile(symbols[0].string()));
            if (!symDoc) {
                return {};
            }
            auto& symAnalysis = symDoc->getAnalysis();
            auto result = symAnalysis.getCompilation()->tryGetDefinition(
                declTok->rawText(), symAnalysis.getCompilation()->getRoot());
            if (!result.definition) {
                return {};
            }
            symbol = result.definition;
        }
        symSyntax = symbol->getSyntax();

        if (!symSyntax) {
            ERROR("Failed to get syntax for symbol {} of kind {}", symbol->name,
                  toString(symbol->kind));
            return {};
        }

        // For some symbols we want to return the parent to get the data type
        if (symbol->kind == ast::SymbolKind::Modport ||
            symbol->kind == ast::SymbolKind::ModportPort) {
            symSyntax = symSyntax->parent;
        }
        nameToken = findNameToken(symSyntax, symbol->name);
        if (!nameToken) {
            ERROR("Failed to find name token for symbol '{}' of kind {} = {}", symbol->name,
                  toString(symbol->kind), symSyntax->toString());

            // TODO: figure out why this fails sometimes with all generates
            nameToken = symSyntax->getFirstToken();
        }
    }

    auto ret = DefinitionInfo{
        symSyntax,
        *nameToken,
        SourceRange::NoLocation,
        symbol,
    };

    // fill in original range if behind a macro
    if (ret.nameToken && sm.isMacroLoc(ret.nameToken.location())) {
        auto locs = sm.getMacroExpansions(ret.nameToken.location());
        // TODO: maybe include more expansion infos?
        auto macroInfo = sm.getMacroInfo(locs.back());
        auto text = macroInfo ? sm.getText(macroInfo->expansionRange) : "";
        if (text.empty()) {
            ERROR("Couldn't get original range for symbol {}", ret.nameToken.valueText());
        }
        else {
            ret.macroUsageRange = macroInfo->expansionRange;
        }
    }

    return ret;
}

std::optional<lsp::Hover> ServerDriver::getDocHover(const URI& uri, const lsp::Position& position) {
    auto doc = getDocument(uri);
    if (!doc) {
        return {};
    }
    auto loc = sm.getSourceLocation(doc->getBuffer(), position.line, position.character);
    if (!loc) {
        return {};
    }
    auto maybeInfo = getDefinitionInfoAt(uri, position);
    if (!maybeInfo) {
#ifdef SLANG_DEBUG
        // Shows debug info for the token under cursor when debugging
        auto& analysis = doc->getAnalysis();
        markup::Document markup;
        markup.addParagraph(analysis.getDebugHover(loc.value()));
        return lsp::Hover{.contents = markup.build()};
#endif
        return {};
    }
    auto info = *maybeInfo;
    return lsp::Hover{.contents = getHover(sm, doc->getBuffer(), info)};
}

std::vector<lsp::LocationLink> ServerDriver::getDocDefinition(const URI& uri,
                                                              const lsp::Position& position) {
    auto maybeInfo = getDefinitionInfoAt(uri, position);
    if (!maybeInfo) {
        return {};
    }
    auto info = *maybeInfo;
    auto targetRange = info.macroUsageRange != SourceRange::NoLocation ? info.macroUsageRange
                                                                       : info.nameToken.range();
    auto path = sm.getFullPath(targetRange.start().buffer());
    if (path.empty()) {
        ERROR("No path found for symbol {}", info.nameToken ? info.nameToken.valueText() : "");
        return {};
    }
    auto lspRange = toRange(targetRange, sm);

    return {lsp::LocationLink{
        .targetUri = URI::fromFile(path),
        // This is supposed to be the full source range- however the hover view already provides
        // that, leading to a worse UI
        .targetRange = lspRange,
        .targetSelectionRange = lspRange,
    }};
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
            bool hasWildcard = false;
            for (auto ref : meta.packageImports) {
                for (auto item : ref->items) {
                    if (item->package.valueText() == parentSymbol.name) {
                        if (item->item.kind == parsing::TokenKind::Star) {
                            hasWildcard = true;
                            break;
                        }
                    }
                }
                if (hasWildcard) {
                    break;
                }
            }
            if (!hasWildcard) {
                // no wildcard, just check cases of pkg::<targetName>
                for (auto ref : meta.classPackageNames) {
                    auto tok = ref->parent->as<ScopedNameSyntax>().right->getFirstToken();
                    if (tok.valueText() == targetName) {
                        references.push_back(lsp::Location{
                            .uri = fileUri,
                            .range = toOriginalRange(tok.range(), sm),
                        });
                    }
                }
                continue;
            }
        }

        auto& fileAnalysis = fileDoc->getAnalysis();
        fileAnalysis.addLocalReferences(references, targetSymbol.location, targetName);
    }
}

std::optional<std::vector<lsp::Location>> ServerDriver::getDocReferences(
    const URI& srcUri, const lsp::Position& position, bool includeDeclaration) {
    auto doc = getDocument(srcUri);
    if (!doc) {
        return std::nullopt;
    }

    // Get the symbol at the position
    auto& analysis = doc->getAnalysis();
    auto loc = sm.getSourceLocation(doc->getBuffer(), position.line, position.character);
    if (!loc) {
        return std::nullopt;
    }

    const parsing::Token* declTok = analysis.syntaxes.getWordTokenAt(loc.value());
    if (!declTok) {
        return std::nullopt;
    }

    const ast::Symbol* targetSymbol = analysis.getSymbolAtToken(declTok);
    if (!targetSymbol) {
        return std::nullopt;
    }

    // A top level of a shallow compilation is an instance body; get the definition instead
    if (targetSymbol->kind == ast::SymbolKind::InstanceBody) {
        targetSymbol = &targetSymbol->as<ast::InstanceBodySymbol>().getDefinition();
    }

    std::vector<lsp::Location> references;

    auto targetName = declTok->rawText();

    auto findPkgReferencesInDocument = [&](const parsing::ParserMetadata& meta, const URI& uri) {
        for (auto ref : meta.packageImports) {
            for (auto item : ref->items) {
                if (item->package.valueText() == targetName) {
                    references.push_back(lsp::Location{
                        .uri = uri,
                        .range = toRange(item->package.range(), sm),
                    });
                }
            }
        }
        for (auto ref : meta.classPackageNames) {
            if (ref->identifier.valueText() == targetName) {
                references.push_back(lsp::Location{
                    .uri = uri,
                    .range = toRange(ref->identifier.range(), sm),
                });
            }
        }
    };

    auto findModuleReferencesInDocument = [&](const parsing::ParserMetadata& meta,
                                              const URI& fileUri) {
        for (auto inst : meta.globalInstances) {
            if (inst->type.valueText() == targetName) {
                references.push_back(lsp::Location{
                    .uri = fileUri,
                    .range = toRange(inst->type.range(), sm),
                });
            }
        }
    };

    auto targetLoc = sm.getFullyOriginalLoc(targetSymbol->location);
    auto targetDoc = getDocument(URI::fromFile(sm.getFullPath(targetLoc.buffer())));

    // Helper to process referencing files with a given finder function
    auto processReferencingFiles = [&](std::string_view name, auto&& finder) {
        for (const auto& filePath : m_indexer.getFilesReferencingSymbol(name)) {
            if (filePath == targetDoc->getURI().getPath()) {
                continue;
            }
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
        auto& analysis = targetDoc->getAnalysis();
        analysis.addLocalReferences(references, targetSymbol->location, targetName);
        if (!includeDeclaration) {
            auto targetLspLoc = lsp::Location{
                .uri = URI::fromFile(sm.getFullPath(targetLoc.buffer())),
                .range = toRange(SourceRange(targetLoc, targetLoc + targetSymbol->name.size()), sm),
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
            processReferencingFiles(targetSymbol->as<ast::InstanceSymbol>().getDefinition().name,
                                    findModuleReferencesInDocument);
        } break;
        case ast::SymbolKind::InstanceBody: {
            processReferencingFiles(
                targetSymbol->as<ast::InstanceBodySymbol>().getDefinition().name,
                findModuleReferencesInDocument);
        } break;
        case ast::SymbolKind::Definition: {
            processReferencingFiles(targetSymbol->as<ast::DefinitionSymbol>().name,
                                    findModuleReferencesInDocument);
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
            else {
                // WARN("Skipping global refs for symbol {}: {} with parent {}: {}", targetName,
                //      toString(targetSymbol->kind), parentSymbol.name,
                //      toString(parentSymbol.kind));
                if (targetLoc.buffer() != doc->getBuffer()) {
                    analysis.addLocalReferences(references, targetSymbol->location, targetName);
                }
            }
        }
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

} // namespace server
