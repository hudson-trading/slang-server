//------------------------------------------------------------------------------
// SlangServer.cpp
// Main implementation of the Slang LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

// main.cpp
#include "SlangServer.h"

#include "Config.h"
#include "ast/WcpClient.h"
#include "completions/CompletionDispatch.h"
#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include "util/Logging.h"
#include <algorithm>
#include <filesystem>
#include <fmt/base.h>
#include <memory>
#include <optional>
#include <ranges>
#include <rfl/Variant.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/Scope.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/driver/Driver.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/OS.h"
#include "slang/util/TimeTrace.h"

namespace fs = std::filesystem;

namespace server {

SlangServer::SlangServer(SlangLspClient& client) :
    m_client(client), m_config(Config()),
    guard(OS::captureOutput([this](std::string_view text, bool isStdout) {
        driverPrintCb(text, isStdout);
    })) // This routes messages from the slang driver to lsp notifications
{

    /// Keep this short to get server started quickly
    registerInitialize();
    registerShutdown();
}

// InitializeResult initialize(InitializeParams)
lsp::InitializeResult SlangServer::getInitialize(const lsp::InitializeParams& params) {
    // TODO: may want to use raw strings here bc all these types make compile time longer

    // File Lifecycle
    registerDocDidOpen();
    registerDocDidChange();
    registerDocDidSave();
    registerDocDidClose();

    // Doc Features
    registerDocDefinition();
    registerDocHover();
    registerDocDocumentSymbol();
    registerDocDocumentLink();
    registerDocCompletion();
    registerCompletionItemResolve();

    // Cone tracing (drivers/loads)
    registerDocPrepareCallHierarchy();
    registerCallHierarchyIncomingCalls();
    registerCallHierarchyOutgoingCalls();

    // Workspace Features
    registerWorkspaceExecuteCommand();
    registerWorkspaceSymbol();

    // LSP Lifecycle
    registerInitialized();

    INFO("Server started with pid: {}", OS::getpid());

    // Top level setting- these are internal commands, the main command should be in the client
    registerCommand<std::string, std::monostate, &SlangServer::setTopLevel>("slang.setTopLevel");
    registerCommand<std::string, std::monostate, &SlangServer::setBuildFile>("slang.setBuildFile");

    // WCP Commands
    registerCommand<lsp::TextDocumentPositionParams, std::vector<std::string>,
                    &SlangServer::getInstances>("slang.getInstances");
    registerCommand<waves::ScopeToWaveform, std::monostate, &SlangServer::addToWaveform>(
        "slang.addToWaveform");
    registerCommand<std::string, std::monostate, &SlangServer::openWaveform>("slang.openWaveform");

    // Hierarchy View (sidebar)
    registerCommand<std::string, std::vector<hier::HierItem_t>, &SlangServer::getScope>(
        "slang.getScope");

    // Terminal Links
    registerCommand<std::string, std::vector<std::string>, &SlangServer::getFilesContainingModule>(
        "slang.getFilesContainingModule");

    // Instances View
    registerCommand<std::monostate, std::vector<hier::InstanceSet>,
                    &SlangServer::getScopesByModule>("slang.getScopesByModule");
    registerCommand<std::string, std::vector<hier::QualifiedInstance>,
                    &SlangServer::getInstancesOfModule>("slang.getInstancesOfModule");

    if (params.workspaceFolders.has_value()) {
        auto folders = params.workspaceFolders.value();
        if (folders.size() > 1) {

            m_client.showWarning(
                "Slang only supports a single workspace folder, using the first one");
        }
        m_workspaceFolder = params.workspaceFolders->at(0);
    }

    // TODO: watch for changes to config file
    loadConfig();

    auto result =
        lsp::InitializeResult{
            .capabilities =
                lsp::ServerCapabilities{
                    .textDocumentSync =
                        lsp::TextDocumentSyncOptions{
                            .openClose = true,
                            .change = lsp::TextDocumentSyncKind::Incremental,
                            .save =
                                lsp::SaveOptions{
                                    .includeText = true,
                                },
                        },
                    .completionProvider =
                        lsp::CompletionOptions{
                            .triggerCharacters =
                                std::vector<std::string>{
                                    "`", // macros
                                    "#", // hierarchial inst- modules and interfaces
                                    // ".", // params, ports, hierarchical references
                                    "(", // function calls
                                    ":", // pkg scope (::), wire width
                                    "[", // wire width, array indexing
                                    // "$"  // builtins
                                },
                            .resolveProvider = true,
                            .completionItem =
                                lsp::ServerCompletionItemOptions{
                                    .labelDetailsSupport = true,
                                }},
                    .hoverProvider = true,
                    .definitionProvider = true,
                    .documentSymbolProvider = true,
                    .documentLinkProvider =
                        lsp::DocumentLinkOptions{
                            .resolveProvider = false,
                            .workDoneProgress = false,
                        },
                    .workspaceSymbolProvider = true,
                    .executeCommandProvider =
                        lsp::ExecuteCommandOptions{
                            .commands = getCommandList(),
                        },
                    .callHierarchyProvider = true,
                }};

    INFO("Initialize result: {} ", rfl::json::write(result));

    return result;
}

void SlangServer::setExplore() {
    // Clear any existing diagnostics and set mode to explore
    m_buildfile = std::nullopt;
    m_topFile = std::nullopt;

    // Move data into the Server Driver
    m_driver = ServerDriver::create(m_indexer, m_client, m_config.flags.value(), {},
                                    m_driver.get());
    m_driver->diagClient->updateDiags();
}

std::monostate SlangServer::setTopLevel(const std::string& path) {
    if (path.empty()) {
        setExplore();
        return std::monostate{};
    }
    m_indexer.waitForIndexingCompletion();

    INFO("Setting top level to {}", path);
    auto uri = URI::fromFile(path);
    auto doc = m_driver->getDocument(uri);
    if (!doc) {
        m_client.showError("Document not found: " + path);
        return std::monostate{};
    }
    m_topFile = path;
    // Get top name from shallow parse
    {
        slang::ast::Compilation shallowCompilation;
        shallowCompilation.addSyntaxTree(doc->getSyntaxTree());
        if (shallowCompilation.getRoot().topInstances.empty()) {
            m_client.showError("No top modules found in: " + path);
            return std::monostate{};
        }
        for (auto& top : shallowCompilation.getRoot().topInstances.subspan(1)) {
            WARN("Extra top module: {}", top->name);
        }
        if (shallowCompilation.getRoot().topInstances.size() == 0) {
            m_client.showError("No top modules found in " + path);
            return std::monostate{};
        }

        auto top = shallowCompilation.getRoot().topInstances[0]->name;
        m_driver->createCompilation(uri, top);
    }

    return std::monostate{};
}

std::monostate SlangServer::setBuildFile(const std::string& path) {
    if (path.empty()) {
        setExplore();
        return std::monostate{};
    }
    m_buildfile = path;

    m_driver = ServerDriver::create(m_indexer, m_client, m_config.flags.value(),
                                    std::vector<std::string>{path}, m_driver.get());
    m_driver->createCompilation();
    return std::monostate{};
}

std::vector<hier::InstanceSet> SlangServer::getScopesByModule(const std::monostate&) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot get scopes by module");
        return {};
    }
    return m_driver->comp->getScopesByModule();
}

std::vector<hier::QualifiedInstance> SlangServer::getInstancesOfModule(
    const std::string moduleName) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot get instances of module {}", moduleName);
        return {};
    }
    auto result = m_driver->comp->getInstancesOfModule(moduleName);
    if (result.empty()) {
        m_client.showError(fmt::format("Module {} not found", moduleName));
    }
    return result;
}

std::vector<std::string> SlangServer::getFilesContainingModule(const std::string moduleName) {
    // lookup in index
    auto paths = m_indexer.getRelevantFilesForName(moduleName);
    auto transformed = paths | std::ranges::views::transform(
                                   [](const auto& path) { return path.string(); });
    return std::vector<std::string>(transformed.begin(), transformed.end());
}

std::vector<hier::HierItem_t> SlangServer::getScope(const std::string& hierPath) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot get scope for {}", hierPath);
        return {};
    }
    return m_driver->comp->getScope(hierPath);
}

// TODO -- Underlying ServerCompilation implementation is slow for larger designs -- fix
// Currently InstanceVisitor walks the entire design searching for symbols which match
// the provided location.  Instead we should use the ShallowAnalysis to find all the
// containing module instances.  This assumes that hierarchical references don't leave
// a given module.  We should make sure slang can warn for this so that people can lint
// their designs accordingly, fail gracefully when this happens and if really needed
// provided a slower / more thorough fallback when this rule is violated (last part is debatable).
// We won't be able to fully depend on the ShallowAnalysis to give us all instances because
// it won't actually know things such as how generate loops were evaluated.  Either we can
// crawl the AST limited just to the modules in question or we can attempt to use the
// provided hierarchical path to deduce where such points may be and then use the remainder
// of the provided path.
std::vector<std::string> SlangServer::getInstances(const lsp::TextDocumentPositionParams& params) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot get instances");
        return {};
    }
    return m_driver->comp->getInstances(params);
}

std::monostate SlangServer::addToWaveform(const waves::ScopeToWaveform& params) {
    // TODO -- once WCP variables and scopes are squashed we won't actually need the compilation
    // here
    if (!m_driver->comp || !m_wcpClient.has_value() || !m_wcpClient->running()) {
        ERROR("No compilation or WCP session available, cannot add items");
        return std::monostate{};
    }

    if (m_driver->comp->isWcpVariable(params.path)) {
        m_wcpClient->addVariable(params.path);
    }
    else {
        m_wcpClient->addScope(params);
    }

    return std::monostate{};
}

std::monostate SlangServer::openWaveform(const std::string& path) {
    auto wcpCommand = m_config.wcpCommand.value();
    if (wcpCommand.empty()) {
        wcpCommand = "surfer --wcp-initiate {}";
    }
    if (m_wcpClient.has_value() && m_wcpClient->running()) {
        m_client.showInfo(fmt::format("Opening waveform from {} (reusing WCP)", path));
    }
    else {
        m_client.showInfo(fmt::format("Opening waveform from {} (creating WCP)", path));
        m_wcpClient.emplace(this, wcpCommand);
    }
    m_wcpClient->loadWaveform(path);

    return std::monostate{};
}

void SlangServer::gotoDeclaration(const std::string& path) {
    if (m_driver->comp) {
        auto showDocParams = m_driver->comp->gotoDeclaration(path);
        if (showDocParams) {
            m_client.onShowDocument(*showDocParams);
        }
    }
}

void SlangServer::waveformLoaded(const std::string& path) {
    if (m_config.buildPattern.value().has_value()) {
        fs::path waveform{path};
        std::string waveStem{waveform.stem().string()};
        std::string buildFile{
            fmt::vformat(*m_config.buildPattern.value(), fmt::make_format_args(waveStem))};
        if (fs::exists(buildFile)) {
            setBuildFile(buildFile);
        }
    }
}

std::optional<std::vector<lsp::CallHierarchyItem>> SlangServer::getDocPrepareCallHierarchy(
    const lsp::CallHierarchyPrepareParams& params) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot trace cones");
        return std::nullopt;
    }
    return m_driver->comp->getDocPrepareCallHierarchy(params);
}

std::optional<std::vector<lsp::CallHierarchyIncomingCall>> SlangServer::
    getCallHierarchyIncomingCalls(const lsp::CallHierarchyIncomingCallsParams& params) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot trace cones");
        return std::nullopt;
    }
    return m_driver->comp->getCallHierarchyCalls<lsp::CallHierarchyIncomingCallsParams,
                                                 lsp::CallHierarchyIncomingCall>(params);
}

std::optional<std::vector<lsp::CallHierarchyOutgoingCall>> SlangServer::
    getCallHierarchyOutgoingCalls(const lsp::CallHierarchyOutgoingCallsParams& params) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot trace cones");
        return std::nullopt;
    }
    return m_driver->comp->getCallHierarchyCalls<lsp::CallHierarchyOutgoingCallsParams,
                                                 lsp::CallHierarchyOutgoingCall>(params);
}

std::vector<std::string> SlangServer::getDrivers(const std::string& path) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot trace cones");
        return {};
    }
    return m_driver->comp->getConePaths<true>(path);
}

std::vector<std::string> SlangServer::getLoads(const std::string& path) {
    if (!m_driver->comp) {
        ERROR("No compilation available, cannot trace cones");
        return {};
    }
    return m_driver->comp->getConePaths<false>(path);
}

void SlangServer::loadConfig(const Config& config, bool forceIndexing) {
    auto old_config = m_config;
    m_config = Config(config);

    if (m_config.build.value().has_value()) {
        m_client.showInfo("Using build file: " + *m_config.build.value());
        setBuildFile(*m_config.build.value());
    }
    else {
        setExplore();
    }

    if (forceIndexing || (old_config.indexGlobs.value() != m_config.indexGlobs.value() ||
                          (old_config.excludeDirs.value() != m_config.excludeDirs.value()))) {
        INFO("Updating index globs");
        auto indexGlobs = m_config.indexGlobs.value();
        if (!m_workspaceFolder) {
            // Filter to abs path globs if there's no workspace folder
            std::vector<std::string> filtered;
            std::copy_if(indexGlobs.begin(), indexGlobs.end(), std::back_inserter(filtered),
                         [](const auto& glob) {
                             return std::filesystem::path(glob).is_absolute();
                         });
            indexGlobs = std::move(filtered);
        }
        m_indexer.startIndexing(indexGlobs, m_config.excludeDirs.value(),
                                m_config.indexingThreads.value());
    }

    // Send config to editor client if it needs to parse general configs
    m_client.setConfig(m_config);
}

void SlangServer::loadConfig() {
    std::vector<std::string> confPaths;
    // In order from lowest to highest precedence:
    // - workspace conf (tracked)
    // - home dir
    // - workspace local conf (untracked)
    if (m_workspaceFolder) {
        std::string workspacePath(m_workspaceFolder.value().uri.getPath());
        confPaths.push_back(workspacePath + "/.slang-server.json");
    }
    if (getenv("HOME") && !getenv("SLANG_SERVER_TESTS")) {
        confPaths.push_back(std::string(getenv("HOME")) + "/.config/slang-server/config.json");
    }
    if (m_workspaceFolder) {
        std::string workspacePath(m_workspaceFolder.value().uri.getPath());
        confPaths.push_back(workspacePath + "/.slang-server.local.json");
    }

    loadConfig(Config::fromFiles(confPaths, m_client), true);
}

rfl::Variant<lsp::Definition, std::vector<lsp::DefinitionLink>, std::monostate> SlangServer::
    getDocDefinition(const lsp::DefinitionParams& params) {

    auto doc = m_driver->getDocument(params.textDocument.uri);
    if (!doc) {
        ERROR("Document {} not found", params.textDocument.uri.getPath());
        return std::monostate{};
    }
    return doc->getDocDefinition(params.position);
}

std::optional<lsp::Hover> SlangServer::getDocHover(const lsp::HoverParams& params) {
    auto doc = m_driver->getDocument(params.textDocument.uri);
    if (!doc) {
        return std::nullopt;
    }
    return doc->getDocHover(params.position);
}

std::optional<std::vector<lsp::DocumentLink>> SlangServer::getDocDocumentLink(
    const lsp::DocumentLinkParams& params) {
    auto doc = m_driver->getDocument(params.textDocument.uri);
    if (!doc) {
        return std::nullopt;
    }
    return doc->getDocLinks();
}

rfl::Variant<std::vector<lsp::SymbolInformation>, std::vector<lsp::DocumentSymbol>, std ::monostate>
SlangServer::getDocDocumentSymbol(const lsp::DocumentSymbolParams& params) {
    auto doc = m_driver->getDocument(params.textDocument.uri);
    if (doc) {
        return doc->getSymbols();
    }
    ERROR("Document {} not found", params.textDocument.uri.getPath());
    return std::vector<lsp::SymbolInformation>{};
}

void SlangServer::onInitialized(const lsp::InitializedParams&) {
    INFO("Server initialized at {}", m_workspaceFolder ? m_workspaceFolder->uri.getPath() : "none");
    m_indexer.waitForIndexingCompletion();
    m_client.setConfig(m_config);
}

std::monostate SlangServer::onShutdown(const std::nullopt_t&) {
    INFO("Server shutting down");
    return std::monostate{};
}

void SlangServer::onDocDidOpen(const lsp::DidOpenTextDocumentParams& params) {
    m_indexer.waitForIndexingCompletion();
    /// @brief Cache syntax tree of the document
    auto& doc = m_driver->openDocument(params.textDocument.uri, params.textDocument.text);
}

void SlangServer::onDocDidChange(const lsp::DidChangeTextDocumentParams& params) {
    m_driver->onDocDidChange(params);
}

void SlangServer::onDocDidSave(const lsp::DidSaveTextDocumentParams& params) {
    m_indexer.waitForIndexingCompletion();

    auto doc = m_driver->getDocument(params.textDocument.uri);
    if (!doc) {
        throw std::runtime_error(
            fmt::format("Document {} not found", params.textDocument.uri.getPath()));
    }

    // Validate that our view of the document is accurate
    if (params.text.has_value()) {
        std::string_view text = params.text.value();

        if (!doc->textMatches(text)) {
            // Recover by overwriting the buffer with the saved text
            INFO("Document text does not match on save, overwriting");
            m_driver->openDocument(params.textDocument.uri, text);
        }
    }
    m_driver->updateDoc(*doc, FileUpdateType::SAVE);

    // Update the indexer with new symbols
    m_indexer.indexTree(*doc->getSyntaxTree());
}

void SlangServer::onDocDidClose(const lsp::DidCloseTextDocumentParams& params) {
    // TODO: Add method in ServerDriver to check that the rc of the document is 1 before
    // removing (non-compilation mode)
    m_driver->closeDocument(params.textDocument.uri);
}

rfl::Variant<std::vector<lsp::SymbolInformation>, std::vector<lsp::WorkspaceSymbol>, std::monostate>
SlangServer::getWorkspaceSymbol(const lsp::WorkspaceSymbolParams&) {
    slang::TimeTraceScope _timeScope("getWorkspaceSymbol", "");

    return m_indexer.getAllWorkspaceSymbols();
}

rfl::Variant<std::vector<lsp::CompletionItem>, lsp::CompletionList, std::monostate> SlangServer::
    getDocCompletion(const lsp::CompletionParams& params) {
    std::vector<lsp::CompletionItem> results;

    auto doc = m_driver->getDocument(params.textDocument.uri);
    if (!doc) {
        m_client.showError(fmt::format("Document {} not found", params.textDocument.uri.getPath()));
        return std::monostate{};
    }
    char triggerChar = params.context->triggerCharacter
                           ? params.context->triggerCharacter.value()[0]
                           : ' ';

    // Prev text including the char that was just written
    auto prevText = doc->getPrevText(params.position);
    char prevChar = prevText.empty() ? ' ' : prevText[prevText.size() - 2];
    INFO("Completion triggered by: ['{}','{}']", prevChar, triggerChar);
    auto maybeLoc = doc->getLocation(params.position);

    // TODO: be more precise with this, this is just a heuristic atm
    bool isLhs = std::all_of(prevText.begin(), prevText.end() - 1, isspace);
    if (!maybeLoc) {
        WARN("No location found for position {},{}", params.position.line,
             params.position.character);
        return std::monostate{};
    }
    auto loc = maybeLoc.value();
    if (params.context->triggerKind == lsp::CompletionTriggerKind::Invoked) {
        m_driver->completions.getInvokedCompletions(results, doc, isLhs, loc);
    }
    else {
        m_driver->completions.getTriggerCompletions(triggerChar, prevChar, doc, loc, results);
    }

    // TODO: rank results using order- the lsp is pretty stupid with this.
    // We need to hack around with client side middileware like in clangd-
    // https://github.com/clangd/vscode-clangd/blob/master/src/clangd-context.ts

    return results;
}

lsp::CompletionItem SlangServer::getCompletionItemResolve(const lsp::CompletionItem& item) {
    if (item.documentation.has_value()) {
        // Already resolved
        return item;
    }

    lsp::CompletionItem ret = item;
    m_driver->completions.getCompletionItemResolve(ret);
    return ret;
}

SourceManager& SlangServer::sourceManager() {
    return m_driver->sm;
}
} // namespace server
