//------------------------------------------------------------------------------
// LspClient.h
// Client-side LSP protocol implementations and notifications
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include "JsonRpc.h"
#include "JsonTypes.h"
#include "LspTypes.h"
#include <optional>
#include <rfl/UnderlyingEnums.hpp>
#include <rfl/json.hpp>
#include <variant>
#include <vector>

namespace lsp {

class LspClient {
public:
    void showInfo(const std::string& message) {
        std::cerr << "Info Notif: " << message << '\n';
        onWindowShowMessage(lsp::ShowMessageParams{
            .type = lsp::MessageType::Info,
            .message = message,
        });
    }

    void showWarning(const std::string& message) {
        std::cerr << "Warning Notif: " << message << '\n';
        onWindowShowMessage(lsp::ShowMessageParams{
            .type = lsp::MessageType::Warning,
            .message = message,
        });
    }

    virtual void showError(const std::string& message) {
        std::cerr << "Error Notif: " << message << '\n';
        onWindowShowMessage(lsp::ShowMessageParams{
            .type = lsp::MessageType::Error,
            .message = message,
        });
    }

    void executeCommand(const std::string& name, const rfl::Generic& params) {
        // TODO: add return values for executeCommand
        // TODO: test this bc I think it's not technically in the spec
        auto param_list = std::vector<lsp::LSPAny>{params};
        auto ex_params = lsp::ExecuteCommandParams{.command = name, .arguments = param_list};
        lsp::sendNotification("workspace/executeCommand", rfl::to_generic(ex_params));
    }
    /// The `workspace/workspaceFolders` is sent from the server to the client to fetch the open
    /// workspace folders.
    virtual std::optional<std::vector<WorkspaceFolder>> getWorkspaceWorkspaceFolders(
        std::monostate) {
        return std::optional<std::vector<WorkspaceFolder>>{};
    }

    /// The `workspace/textDocumentContent` request is sent from the server to the client to refresh
    /// the content of a specific text document.
    ///
    /// @since 3.18.0
    /// @proposed
    virtual std::monostate getWorkspaceTextDocumentContentRefresh(
        const TextDocumentContentRefreshParams&) {
        return std::monostate{};
    }

    /// @since 3.16.0
    virtual std::monostate getWorkspaceSemanticTokensRefresh(std::monostate) {
        return std::monostate{};
    }

    /// @since 3.17.0
    virtual std::monostate getWorkspaceInlineValueRefresh(std::monostate) {
        return std::monostate{};
    }

    /// @since 3.17.0
    virtual std::monostate getWorkspaceInlayHintRefresh(std::monostate) { return std::monostate{}; }

    /// @since 3.18.0
    /// @proposed
    virtual std::monostate getWorkspaceFoldingRangeRefresh(std::monostate) {
        return std::monostate{};
    }

    /// The diagnostic refresh request definition.
    ///
    /// @since 3.17.0
    virtual std::monostate getWorkspaceDiagnosticRefresh(std::monostate) {
        return std::monostate{};
    }

    /// The 'workspace/configuration' request is sent from the server to the client to fetch a
    /// certain configuration setting.
    ///
    /// This pull model replaces the old push model were the client signaled configuration change
    /// via an event. If the server still needs to react to configuration changes (since the server
    /// caches the result of `workspace/configuration` requests) the server should register for an
    /// empty configuration change event and empty the cache if such an event is received.
    virtual std::vector<LSPAny> getWorkspaceConfiguration(const ConfigurationParams&) {
        return std::vector<LSPAny>{};
    }

    /// A request to refresh all code actions
    ///
    /// @since 3.16.0
    virtual std::monostate getWorkspaceCodeLensRefresh(std::monostate) { return std::monostate{}; }

    /// A request sent from the server to the client to modified certain resources.
    virtual ApplyWorkspaceEditResult getWorkspaceApplyEdit(const ApplyWorkspaceEditParams&) {
        return ApplyWorkspaceEditResult{};
    }

    /// The `window/workDoneProgress/create` request is sent from the server to the client to
    /// initiate progress reporting from the server.
    virtual std::monostate getWindowWorkDoneProgressCreate(const WorkDoneProgressCreateParams&) {
        return std::monostate{};
    }

    /// The show message request is sent from the server to the client to show a message
    /// and a set of options actions to the user.
    virtual std::optional<MessageActionItem> getWindowShowMessageRequest(
        const ShowMessageRequestParams&) {
        return std::optional<MessageActionItem>{};
    }

    /// The show message notification is sent from a server to a client to ask
    /// the client to display a particular message in the user interface.
    void onWindowShowMessage(const ShowMessageParams& params) {
        sendNotification("window/showMessage", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    /// A request to show a document. This request might open an
    /// external program depending on the value of the URI to open.
    /// For example a request to open `https://code.visualstudio.com/`
    /// will very likely open the URI in a WEB browser.
    ///
    /// @since 3.16.0
    virtual ShowDocumentResult getWindowShowDocument(const ShowDocumentParams&) {
        return ShowDocumentResult{};
    }

    /// The log message notification is sent from the server to the client to ask
    /// the client to log a particular message.
    void onWindowLogMessage(const LogMessageParams& params) {
        sendNotification("window/logMessage", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    /// Diagnostics notification are sent from the server to the client to signal
    /// results of validation runs.
    virtual void onDocPublishDiagnostics(const PublishDiagnosticsParams& params) {
        sendNotification("textDocument/publishDiagnostics",
                         rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    /// The telemetry event notification is sent from the server to the client to ask
    /// the client to log telemetry data.
    void onTelemetryEvent(const LSPAny& params) {
        sendNotification("telemetry/event", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    /// The `client/unregisterCapability` request is sent from the server to the client to
    /// unregister a previously registered capability handler on the client side.
    virtual std::monostate getClientUnregisterCapability(const UnregistrationParams&) {
        return std::monostate{};
    }

    /// The `client/registerCapability` request is sent from the server to the client to register a
    /// new capability handler on the client side.
    virtual std::monostate getClientRegisterCapability(const RegistrationParams&) {
        return std::monostate{};
    }

    void onProgress(const ProgressParams& params) {
        sendNotification("$/progress", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    void onLogTrace(const LogTraceParams& params) {
        sendNotification("$/logTrace", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    void onCancelRequest(const CancelParams& params) {
        sendNotification("$/cancelRequest", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };

    // TODO -- generate this, also actually handle responses from the client -- maybe async / await
    void onShowDocument(const ShowDocumentParams& params) {
        sendRequest("window/showDocument", rfl::to_generic<rfl::UnderlyingEnums>(params));
    };
};
} // namespace lsp
