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
#include "util/Logging.h"
#include <memory>
#include <queue>
#include <string_view>

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/driver/Driver.h"
#include "slang/driver/SourceLoader.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {

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

} // namespace server
