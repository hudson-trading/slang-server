//------------------------------------------------------------------------------
// SlangServer.h
// Language Server setup and event dispatching
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include "ServerDiagClient.h"
#include "ServerDriver.h"
#include "SlangLspClient.h"
#include "ast/HierarchicalView.h"
#include "ast/SlangServerWcp.h"
#include "ast/WcpClient.h"
#include "document/SlangDoc.h"
#include "lsp/LspServer.h"
#include "lsp/LspTypes.h"
#include <memory>
#include <rfl.hpp>
#include <rfl/Generic.hpp>
#include <rfl/Variant.hpp>
#include <rfl/json.hpp>
#include <variant>
#include <vector>

#include "slang/driver/Driver.h"
#include "slang/text/SourceManager.h"
namespace server {
class SlangServer : public lsp::LspServer<SlangServer>, public SlangServerWcp {
    /// The primary business logic for the server, in a type safe manner.
    /// To add an LSP method:
    /// - See routes here:
    /// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification
    /// - override method found in LspServer.h
    /// - register it in getInitialize()
    /// - Add options to the result of getInitialize() indicating it's available

protected:
    SlangLspClient& m_client;

    /// Manages open docuemnts and a single compilation
    /// Created each time config/flags are changed, including switching between explore/build mode
    std::unique_ptr<ServerDriver> m_driver;

    /// The diag client
    std::shared_ptr<ServerDiagClient> m_diagClient;

    /// Guard to get error prints from driver
    decltype(OS::captureOutput()) guard;

    /// The build file, if set. Requires the top level to already be set
    std::optional<std::string> m_buildfile;

    /// The top file when the top level is set
    std::optional<std::string> m_topFile;

    // The workspace folder, if set
    std::optional<lsp::WorkspaceFolder> m_workspaceFolder = std::nullopt;

    /// The config from .slang-server.json
    Config m_config;

    /// Indexes the workspace for top symbols and macros
    Indexer m_indexer;

    // The waveform viewer client
    std::optional<waves::WcpClient> m_wcpClient = std::nullopt;

public:
    SlangServer(SlangLspClient& client);

    ~SlangServer() = default;

    /// Load the configuration file from a cascading set of json config files:
    /// ~/.slang/server.json
    /// ./slang/server.json
    /// ./slang/local/server.json
    void loadConfig();

    /// Load the configuration from a given config object, reindex if needed or forced
    void loadConfig(const Config& config, bool forceIndexing = false);

    const Config& getConfig() const { return m_config; }

    /// @brief Configure the driver with flags from the config file
    void configureDriver(slang::driver::Driver& driver);

    void driverPrintCb(std::string_view text, bool isStdout) {
        if (isStdout) {
            m_client.showWarning(std::string(text));
        }
        else {
            m_client.showError(std::string(text));
        }
    }

    std::shared_ptr<SlangDoc> getDoc(const URI& path);

    SourceManager& sourceManager();

    ////////////////////////////////////////////////
    /// HDL Features
    ////////////////////////////////////////////////
    void setExplore();

    std::monostate setBuildFile(const std::string& path);

    std::monostate setTopLevel(const std::string&);

    // Returns the instances indexed by module. If just a single instance, is will have it, else
    // it will require another query
    std::vector<hier::InstanceSet> getScopesByModule(const std::monostate&);

    // Returns the instances of a module
    std::vector<hier::QualifiedInstance> getInstancesOfModule(const std::string moduleName);

    // Returns the modules defined in a file, used for the modules view
    std::vector<std::string> getModulesInFile(const std::string path);

    // Returns the files that contain a specific module, used for terminal links
    std::vector<std::string> getFilesContainingModule(const std::string moduleName);

    // Return the item at this path
    std::vector<hier::HierItem_t> getScope(const std::string& hierPath);

    struct ExpandMacroArgs {
        std::string src;
        std::string dst;
    };
    // Expand macros in a file
    bool expandMacros(ExpandMacroArgs args);

    ////////////////////////////////////////////////
    /// Server Lifecycle
    ////////////////////////////////////////////////

    lsp::InitializeResult getInitialize(const lsp::InitializeParams& params) override;

    void onInitialized(const lsp::InitializedParams&) override;

    std::monostate onShutdown(const std::nullopt_t&);

    ////////////////////////////////////////////////
    /// Workspace features
    ////////////////////////////////////////////////

    rfl::Variant<std::vector<lsp::SymbolInformation>, std::vector<lsp::WorkspaceSymbol>,
                 std::monostate>
    getWorkspaceSymbol(const lsp::WorkspaceSymbolParams&) override;

    ////////////////////////////////////////////////
    /// Open Document Lifecycle
    ////////////////////////////////////////////////
    void onDocDidOpen(const lsp::DidOpenTextDocumentParams&) override;

    void onDocDidClose(const lsp::DidCloseTextDocumentParams&) override;

    void onDocDidChange(const lsp::DidChangeTextDocumentParams&) override;

    void onDocDidSave(const lsp::DidSaveTextDocumentParams&) override;

    ////////////////////////////////////////////////
    /// Document features (core lsp feautres)
    ////////////////////////////////////////////////

    /// Symbol tree
    rfl::Variant<std::vector<lsp::SymbolInformation>, std::vector<lsp::DocumentSymbol>,
                 std::monostate>
    getDocDocumentSymbol(const lsp::DocumentSymbolParams&) override;

    /// Document links (include files)
    std::optional<std::vector<lsp::DocumentLink>> getDocDocumentLink(
        const lsp::DocumentLinkParams&) override;

    /// Hover
    std::optional<lsp::Hover> getDocHover(const lsp::HoverParams&) override;

    /// Goto Definition
    rfl::Variant<lsp::Definition, std::vector<lsp::DefinitionLink>, std::monostate>
    getDocDefinition(const lsp::DefinitionParams&) override;

    /// Completion (get list of ids and kinds)
    rfl::Variant<std::vector<lsp::CompletionItem>, lsp::CompletionList, std::monostate>
    getDocCompletion(const lsp::CompletionParams&) override;

    std::optional<std::vector<lsp::InlayHint>> getDocInlayHint(
        const lsp::InlayHintParams&) override;

    /// Completions resolve (get docs and snippet string)
    lsp::CompletionItem getCompletionItemResolve(const lsp::CompletionItem&) override;

    ////////////////////////////////////////////////
    /// Cone tracing
    ////////////////////////////////////////////////

    std::optional<std::vector<lsp::CallHierarchyItem>> getDocPrepareCallHierarchy(
        const lsp::CallHierarchyPrepareParams&) override;

    std::optional<std::vector<lsp::CallHierarchyIncomingCall>> getCallHierarchyIncomingCalls(
        const lsp::CallHierarchyIncomingCallsParams&) override;

    std::optional<std::vector<lsp::CallHierarchyOutgoingCall>> getCallHierarchyOutgoingCalls(
        const lsp::CallHierarchyOutgoingCallsParams&) override;

    ////////////////////////////////////////////////
    /// Wcp commands and related LSP methods
    ////////////////////////////////////////////////

    /// Get a list of RTL paths of instances given a text document position
    std::vector<std::string> getInstances(const lsp::TextDocumentPositionParams&);

    /// Add the given variable or scope to the waveform via WCP
    std::monostate addToWaveform(const waves::ItemToWaveform&);

    /// Open a given waveform file and establish a WCP connection
    std::monostate openWaveform(const std::string&);

    /// Given an RTL path, send the client to the declaration
    void onGotoDeclaration(const std::string&) final;

    /// Do housekeeping (e.g. compile) once a waveform is loaded (file name is given as a parameter)
    void onWaveformLoaded(const std::string&) final;

    /// Get a list of RTL paths of the drivers of a given RTL path
    std::vector<std::string> getDrivers(const std::string&) final;

    /// Get a list of RTL paths of the loads of a given RTL path
    std::vector<std::string> getLoads(const std::string&) final;

    /// Get the mutex to prevent collisions between LSP and WCP message handling
    std::mutex& getMutex() final { return mutex; };
};
} // namespace server
