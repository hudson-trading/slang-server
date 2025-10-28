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
#include "ast/ServerCompilation.h"
#include "completions/CompletionDispatch.h"
#include "document/SlangDoc.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include <memory>
#include <queue>
#include <string_view>

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/driver/Driver.h"
#include "slang/driver/SourceLoader.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {
using namespace slang;

ServerDriver::ServerDriver(Indexer& indexer, SlangLspClient& client, std::string_view flags,
                           std::vector<std::string> buildfiles) :
    sm(driver.sourceManager), diagEngine(driver.diagEngine), client(client),
    diagClient(std::make_shared<ServerDiagClient>(sm, client)), completions(indexer, sm, options),
    indexer(indexer) {
    // Create and configure the driver with our source manager and diagnostic engine
    // SourceManager must not be moved, since diagEngine, syntax trees hold references to it
    driver.addStandardArgs();

    // Parse command line and process files
    slang::CommandLine::ParseOptions parseOpts;
    parseOpts.expandEnvVars = true;
    parseOpts.ignoreProgramName = true;
    parseOpts.supportComments = true;
    parseOpts.ignoreDuplicates = true;

    bool ok = driver.parseCommandLine(flags, parseOpts);
    driver.options.errorLimit = 0;
    ok &= driver.processOptions(false);
    if (!ok) {
        client.showError(fmt::format("Failed to parse config flags: {}", flags));
    }

    for (auto& buildfile : buildfiles) {
        ok = driver.processCommandFiles(buildfile, false, false);
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
    ok = driver.parseAllSources();

    // Create documents from syntax trees
    INFO("Creating ServerDriver with {} trees", driver.syntaxTrees.size());
    for (auto& tree : driver.syntaxTrees) {
        auto uri = URI::fromFile(sm.getFullPath(tree->getSourceBufferIds()[0]));
        auto doc = SlangDoc::fromTree(std::move(tree), sm, options);
        docs[uri] = doc;
    }

    // Set dependent documents for all documents after they're all created
    for (auto& [uri, doc] : docs) {
        auto dependentDocs = getDependentDocs(doc->getSyntaxTree());
        doc->setDependentDocuments(dependentDocs);
    }
}

// Doc updates (open, change, save)
void ServerDriver::updateDoc(SlangDoc& doc, FileUpdateType type) {
    indexer.waitForIndexingCompletion();
    diagClient->clear(doc.getURI());
    // Grab dependent documents
    doc.setDependentDocuments(getDependentDocs(doc.getSyntaxTree()));
    // send out parse and shallow compilation diagnostics
    // TODO: Send parse, then shallow compilation?
    doc.issueDiagnosticsTo(diagEngine);
    diagClient->updateDiags();
    INFO("Published diags for {}", doc.getURI().getPath());

    if (comp) {
        // Slower diags, elaboration
        if (type == FileUpdateType::SAVE) {
            comp->refresh();
            INFO("Publishing Comp diags")
            diagClient->updateDiags();
            sm.clearOldBuffers();
        }
    }
    else {
        sm.clearOldBuffers();
    }
}

std::unique_ptr<ServerDriver> ServerDriver::create(Indexer& indexer, SlangLspClient& client,
                                                   std::string_view flags,
                                                   std::vector<std::string> buildfiles,
                                                   const ServerDriver* oldDriver) {
    auto newDriver = std::make_unique<ServerDriver>(indexer, client, flags, buildfiles);

    // Copy only open documents from old driver if provided
    if (oldDriver) {
        oldDriver->diagClient->clear();
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

SlangDoc& ServerDriver::openDocument(const URI& uri, const std::string_view text) {
    auto doc = SlangDoc::fromText(uri, sm, this->options, text);
    docs[uri] = doc;
    // Track this as an open document
    m_openDocs.insert(uri);
    updateDoc(*doc, FileUpdateType::OPEN);
    return *doc;
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
            auto paths = indexer.getRelevantFilesForName(name);
            if (!paths.empty()) {
                auto filePath = paths[0].string();

                // Check if we've already processed this file to avoid cycles
                if (processedFiles.find(filePath) != processedFiles.end())
                    return;

                processedFiles.insert(filePath);

                auto newdoc = getDocument(URI::fromFile(filePath));
                if (newdoc) {
                    result.push_back(newdoc);
                    docs[newdoc->getURI()] = newdoc;

                    // Only add packages to the queue for recursive processing
                    for (auto& [n, _] : newdoc->getSyntaxTree()->getMetadata().nodeMeta) {
                        auto decl = &n->as<syntax::ModuleDeclarationSyntax>();
                        if (decl->kind == syntax::SyntaxKind::PackageDeclaration) {
                            treesToProcess.push(newdoc->getSyntaxTree());
                            break;
                        }
                    }
                }
                else {
                    ERROR("No doc found for {}", filePath);
                }
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

bool ServerDriver::createCompilation(const URI& uri, std::string_view top) {
    // Find the document
    auto it = docs.find(uri);
    if (it == docs.end()) {
        return false; // Document not found
    }

    auto& doc = it->second;

    // Collect documents starting with the target document
    std::vector<std::shared_ptr<syntax::SyntaxTree>> syntaxTrees{doc->getSyntaxTree()};
    driver::SourceLoader::loadTrees(
        syntaxTrees,
        [this](std::string_view name) {
            auto paths = indexer.getRelevantFilesForName(name);
            SourceBuffer buffer;
            if (!paths.empty()) {
                auto maybeBuf = sm.readSource(paths[0], /* library */ nullptr);
                if (maybeBuf) {
                    buffer = *maybeBuf;
                }
                else {
                    ERROR("Failed to read source for {}: {}", paths[0].string(),
                          maybeBuf.error().message());
                }
            }
            return buffer;
        },
        sm, this->options);

    std::vector<std::shared_ptr<SlangDoc>> documents;
    documents.reserve(syntaxTrees.size());
    for (const auto& tree : syntaxTrees) {
        documents.push_back(SlangDoc::fromTree(tree, sm, this->options));
    }
    // insert the documents into the driver
    for (const auto& doc : documents) {
        docs[doc->getURI()] = doc;
    }
    // Copy the const options bag, set top
    slang::Bag localOptions = this->options;
    auto& cOptions = localOptions.insertOrGet<slang::ast::CompilationOptions>();
    cOptions.topModules = {top};

    comp = std::make_unique<ServerCompilation>(std::move(documents), localOptions, sm);

    // refresh
    for (auto& uri : m_openDocs) {
        auto docIt = docs.find(uri);
        if (docIt != docs.end()) {
            updateDoc(*docIt->second, FileUpdateType::OPEN);
        }
    }
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
    const syntax::SyntaxNode* declSyntax = analysis.syntaxes.getSyntaxAt(declTok);
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
            return {};
        }
        symSyntax = macro->second;
        nameToken = macro->second->name;
        // symbol remains nullptr for macros
    }
    else {
        symbol = analysis.getSymbolAtToken(declTok);
        if (!symbol) {
            return {};
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
    auto& analysis = doc->getAnalysis();
    auto loc = sm.getSourceLocation(doc->getBuffer(), position.line, position.character);
    if (!loc) {
        return {};
    }
    auto maybeInfo = getDefinitionInfoAt(uri, position);
    if (!maybeInfo) {
#ifdef SLANG_DEBUG
        // Shows debug info for the token under cursor when debugging
        auto tok = analysis.getTokenAt(loc.value());
        if (tok == nullptr) {
            return {};
        }
        return lsp::Hover{
            .contents = lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(),
                                           .value = analysis.getDebugHover(*tok)}};
#endif
        return {};
    }
    auto info = *maybeInfo;

    auto md = svCodeBlockString(*info.node);

    if (info.macroUsageRange != SourceRange::NoLocation) {
        auto text = sm.getText(info.macroUsageRange);
        md += fmt::format("\n Expanded from\n {}", svCodeBlockString(text));
    }

    if (info.symbol) {
        // Show hierarchical path if:
        // 1. Symbol is in a different scope than the current position
        // 2. and Symbol's scope is not the root scope ($unit)
        auto symbolScope = info.symbol->getParentScope();
        auto lookupScope = analysis.getScopeAt(loc.value());

        if (lookupScope && symbolScope && lookupScope != symbolScope) {
            auto& parentSym = symbolScope->asSymbol();
            auto hierPath = parentSym.getLexicalPath();
            // The typedef name needs to be appended; it's not attached to the type
            if (parentSym.kind == ast::SymbolKind::PackedStructType ||
                parentSym.kind == ast::SymbolKind::UnpackedStructType) {
                auto syntax = parentSym.getSyntax();
                if (syntax && syntax->parent &&
                    syntax->parent->kind == syntax::SyntaxKind::TypedefDeclaration) {
                    hierPath += "::";
                    hierPath +=
                        syntax->parent->as<syntax::TypedefDeclarationSyntax>().name.valueText();
                }
            }
            if (!hierPath.empty()) {
                md = fmt::format("{}\n\n---\n\n{}",
                                 svCodeBlockString(fmt::format("// In {}", hierPath)), md);
            }
        }
    }
    else {
        // show file for macros
        auto macroBuf = info.nameToken.location().buffer();
        if (macroBuf != doc->getBuffer() && sm.isValid(macroBuf)) {
            auto path = sm.getFullPath(macroBuf);
            if (!path.empty()) {
                md = fmt::format(
                    "{}\n\n---\n\n{}",
                    svCodeBlockString(fmt::format("// From {}", path.filename().string())), md);
            }
        }
    }

    return lsp::Hover{.contents = markdown(md)};
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

} // namespace server
