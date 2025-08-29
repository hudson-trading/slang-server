//------------------------------------------------------------------------------
// LspServer.h
// LSP server template base class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include "JsonRpcServer.h"
#include "JsonTypes.h"
#include "LspTypes.h"
#include "URI.h"
#include "lsp/LspClient.h"
#include <iostream>
#include <optional>
#include <rfl/json.hpp>
#include <variant>
#include <vector>

namespace lsp {

template<typename Impl>
class LspServer : public JsonRpcServer<Impl> {
protected:
    std::unordered_map<std::string, std::function<rfl::Generic(rfl::Generic)>> m_commands;

    // LspClient& m_lspClient;

    // LspServer<Impl>(LspClient& lspClient) : m_lspClient(lspClient) {}

    /// Register an rpc method with the given Params, Return, and Method (name)
    template<typename P, typename R, auto Method>
    void registerCommand(const std::string& name) {
        m_commands[name] = [this](std::optional<rfl::Generic> paramsJson) -> rfl::Generic {
            // Deserialize params
            R result;
            if constexpr (!std::is_same_v<P, std::nullopt_t>) {
                rfl::Result<P> params = rfl::from_generic<P, rfl::UnderlyingEnums>(
                    paramsJson.value());
                if (!params) {
                    throw std::runtime_error(params.error().what());
                }
                result = (static_cast<Impl*>(this)->*Method)(params.value());
            }
            else {
                result = (static_cast<Impl*>(this)->*Method)(std::monostate{});
            }

            if constexpr (std::is_same_v<R, std::monostate>) {
                return std::nullopt;
            }
            else {
                return rfl::to_generic(result);
            }
        };
        std::cerr << "Registered command: " << name << "\n";
    }

    /// A request send from the client to the server to execute a command. The request might return
    /// a workspace edit which the client will apply to the workspace.
    std::optional<lsp::LSPAny> getWorkspaceExecuteCommand(const lsp::ExecuteCommandParams& params) {
        std::cerr << " <---" << params.command << "(" << rfl::json::write(params.arguments)
                  << ")\n";
        auto command = m_commands.find(params.command);
        if (command == m_commands.end()) {
            std::cerr << "Unknown command: " << params.command << "\n";
            std::cerr << " -/-> \n";
            return std::nullopt;
        }
        // returns are rearely used
        // convert args to correct typer
        rfl::Generic args = std::nullopt;
        if (params.arguments) {
            auto argList = params.arguments.value();
            if (argList.size() == 1) {
                args = argList[0];
            }
            else if (argList.size() == 0) {
                args = rfl::to_generic<>(std::monostate{});
            }
            else {
                throw std::runtime_error("Expected 0 or 1 argument for command");
            }
        }
        auto x = command->second(args);

        std::cerr << " ---> " << params.command << "\n";

        // INFO("Command {} returned: {}", params.command, rfl::json::write(x));
        return x;
    }

    std::vector<std::string> getCommandList() const {
        std::vector<std::string> result;
        result.reserve(m_commands.size());
        for (auto& [name, _] : m_commands) {
            result.push_back(name);
        }
        return result;
    }
    /// A request to resolve the range inside the workspace
    /// symbol's location.
    ///
    /// @since 3.17.0
    virtual WorkspaceSymbol getWorkspaceSymbolResolve(const WorkspaceSymbol&) {
        return WorkspaceSymbol{};
    }

    void registerWorkspaceSymbolResolve() {
        this->template registerMethod<WorkspaceSymbol, WorkspaceSymbol,
                                      &Impl::getWorkspaceSymbolResolve>("workspaceSymbol/resolve");
    };
    /// The will rename files request is sent from the client to the server before files are
    /// actually renamed as long as the rename is triggered from within the client.
    ///
    /// @since 3.16.0
    virtual std::optional<WorkspaceEdit> getWorkspaceWillRenameFiles(const RenameFilesParams&) {
        return std::optional<WorkspaceEdit>{};
    }

    void registerWorkspaceWillRenameFiles() {
        this->template registerMethod<RenameFilesParams, std::optional<WorkspaceEdit>,
                                      &Impl::getWorkspaceWillRenameFiles>(
            "workspace/willRenameFiles");
    };
    /// The did delete files notification is sent from the client to the server when
    /// files were deleted from within the client.
    ///
    /// @since 3.16.0
    virtual std::optional<WorkspaceEdit> getWorkspaceWillDeleteFiles(const DeleteFilesParams&) {
        return std::optional<WorkspaceEdit>{};
    }

    void registerWorkspaceWillDeleteFiles() {
        this->template registerMethod<DeleteFilesParams, std::optional<WorkspaceEdit>,
                                      &Impl::getWorkspaceWillDeleteFiles>(
            "workspace/willDeleteFiles");
    };
    /// The will create files request is sent from the client to the server before files are
    /// actually created as long as the creation is triggered from within the client.
    ///
    /// The request can return a `WorkspaceEdit` which will be applied to workspace before the
    /// files are created. Hence the `WorkspaceEdit` can not manipulate the content of the file
    /// to be created.
    ///
    /// @since 3.16.0
    virtual std::optional<WorkspaceEdit> getWorkspaceWillCreateFiles(const CreateFilesParams&) {
        return std::optional<WorkspaceEdit>{};
    }

    void registerWorkspaceWillCreateFiles() {
        this->template registerMethod<CreateFilesParams, std::optional<WorkspaceEdit>,
                                      &Impl::getWorkspaceWillCreateFiles>(
            "workspace/willCreateFiles");
    };
    /// The `workspace/textDocumentContent` request is sent from the client to the
    /// server to request the content of a text document.
    ///
    /// @since 3.18.0
    /// @proposed
    virtual TextDocumentContentResult getWorkspaceTextDocumentContent(
        const TextDocumentContentParams&) {
        return TextDocumentContentResult{};
    }

    void registerWorkspaceTextDocumentContent() {
        this->template registerMethod<TextDocumentContentParams, TextDocumentContentResult,
                                      &Impl::getWorkspaceTextDocumentContent>(
            "workspace/textDocumentContent");
    };
    /// A request to list project-wide symbols matching the query string given
    /// by the {@link WorkspaceSymbolParams}. The response is
    /// of type {@link SymbolInformation SymbolInformation[]} or a Thenable that
    /// resolves to such.
    ///
    /// @since 3.17.0 - support for WorkspaceSymbol in the returned data. Clients
    ///  need to advertise support for WorkspaceSymbols via the client capability
    ///  `workspace.symbol.resolveSupport`.
    ///
    virtual rfl::Variant<std::vector<SymbolInformation>, std::vector<WorkspaceSymbol>,
                         std::monostate>
    getWorkspaceSymbol(const WorkspaceSymbolParams&) {
        return rfl::Variant<std::vector<SymbolInformation>, std::vector<WorkspaceSymbol>,
                            std::monostate>{};
    }

    void registerWorkspaceSymbol() {
        this->template registerMethod<WorkspaceSymbolParams,
                                      rfl::Variant<std::vector<SymbolInformation>,
                                                   std::vector<WorkspaceSymbol>, std::monostate>,
                                      &Impl::getWorkspaceSymbol>("workspace/symbol");
    };

    void registerWorkspaceExecuteCommand() {
        this->template registerMethod<ExecuteCommandParams, std::optional<LSPAny>,
                                      &Impl::getWorkspaceExecuteCommand>(
            "workspace/executeCommand");
    };
    /// The did rename files notification is sent from the client to the server when
    /// files were renamed from within the client.
    ///
    /// @since 3.16.0
    virtual void onWorkspaceDidRenameFiles(const RenameFilesParams&) {}

    void registerWorkspaceDidRenameFiles() {
        this->template registerNotification<RenameFilesParams, &Impl::onWorkspaceDidRenameFiles>(
            "workspace/didRenameFiles");
    };
    /// The will delete files request is sent from the client to the server before files are
    /// actually deleted as long as the deletion is triggered from within the client.
    ///
    /// @since 3.16.0
    virtual void onWorkspaceDidDeleteFiles(const DeleteFilesParams&) {}

    void registerWorkspaceDidDeleteFiles() {
        this->template registerNotification<DeleteFilesParams, &Impl::onWorkspaceDidDeleteFiles>(
            "workspace/didDeleteFiles");
    };
    /// The did create files notification is sent from the client to the server when
    /// files were created from within the client.
    ///
    /// @since 3.16.0
    virtual void onWorkspaceDidCreateFiles(const CreateFilesParams&) {}

    void registerWorkspaceDidCreateFiles() {
        this->template registerNotification<CreateFilesParams, &Impl::onWorkspaceDidCreateFiles>(
            "workspace/didCreateFiles");
    };
    /// The `workspace/didChangeWorkspaceFolders` notification is sent from the client to the server
    /// when the workspace folder configuration changes.
    virtual void onWorkspaceDidChangeWorkspaceFolders(const DidChangeWorkspaceFoldersParams&) {}

    void registerWorkspaceDidChangeWorkspaceFolders() {
        this->template registerNotification<DidChangeWorkspaceFoldersParams,
                                            &Impl::onWorkspaceDidChangeWorkspaceFolders>(
            "workspace/didChangeWorkspaceFolders");
    };
    /// The watched files notification is sent from the client to the server when
    /// the client detects changes to file watched by the language client.
    virtual void onWorkspaceDidChangeWatchedFiles(const DidChangeWatchedFilesParams&) {}

    void registerWorkspaceDidChangeWatchedFiles() {
        this->template registerNotification<DidChangeWatchedFilesParams,
                                            &Impl::onWorkspaceDidChangeWatchedFiles>(
            "workspace/didChangeWatchedFiles");
    };
    /// The configuration change notification is sent from the client to the server
    /// when the client's configuration has changed. The notification contains
    /// the changed configuration as defined by the language client.
    virtual void onWorkspaceDidChangeConfiguration(const DidChangeConfigurationParams&) {}

    void registerWorkspaceDidChangeConfiguration() {
        this->template registerNotification<DidChangeConfigurationParams,
                                            &Impl::onWorkspaceDidChangeConfiguration>(
            "workspace/didChangeConfiguration");
    };
    /// The workspace diagnostic request definition.
    ///
    /// @since 3.17.0
    virtual WorkspaceDiagnosticReport getWorkspaceDiagnostic(const WorkspaceDiagnosticParams&) {
        return WorkspaceDiagnosticReport{};
    }

    void registerWorkspaceDiagnostic() {
        this->template registerMethod<WorkspaceDiagnosticParams, WorkspaceDiagnosticReport,
                                      &Impl::getWorkspaceDiagnostic>("workspace/diagnostic");
    };
    /// The `window/workDoneProgress/cancel` notification is sent from  the client to the server to
    /// cancel a progress initiated on the server side.
    virtual void onWindowWorkDoneProgressCancel(const WorkDoneProgressCancelParams&) {}

    void registerWindowWorkDoneProgressCancel() {
        this->template registerNotification<WorkDoneProgressCancelParams,
                                            &Impl::onWindowWorkDoneProgressCancel>(
            "window/workDoneProgress/cancel");
    };
    /// A request to resolve the supertypes for a given `TypeHierarchyItem`.
    ///
    /// @since 3.17.0
    virtual std::optional<std::vector<TypeHierarchyItem>> getTypeHierarchySupertypes(
        const TypeHierarchySupertypesParams&) {
        return std::optional<std::vector<TypeHierarchyItem>>{};
    }

    void registerTypeHierarchySupertypes() {
        this->template registerMethod<TypeHierarchySupertypesParams,
                                      std::optional<std::vector<TypeHierarchyItem>>,
                                      &Impl::getTypeHierarchySupertypes>(
            "typeHierarchy/supertypes");
    };
    /// A request to resolve the subtypes for a given `TypeHierarchyItem`.
    ///
    /// @since 3.17.0
    virtual std::optional<std::vector<TypeHierarchyItem>> getTypeHierarchySubtypes(
        const TypeHierarchySubtypesParams&) {
        return std::optional<std::vector<TypeHierarchyItem>>{};
    }

    void registerTypeHierarchySubtypes() {
        this->template registerMethod<TypeHierarchySubtypesParams,
                                      std::optional<std::vector<TypeHierarchyItem>>,
                                      &Impl::getTypeHierarchySubtypes>("typeHierarchy/subtypes");
    };
    /// A document will save request is sent from the client to the server before
    /// the document is actually saved. The request can return an array of TextEdits
    /// which will be applied to the text document before it is saved. Please note that
    /// clients might drop results if computing the text edits took too long or if a
    /// server constantly fails on this request. This is done to keep the save fast and
    /// reliable.
    virtual std::optional<std::vector<TextEdit>> getDocWillSaveWaitUntil(
        const WillSaveTextDocumentParams&) {
        return std::optional<std::vector<TextEdit>>{};
    }

    void registerDocWillSaveWaitUntil() {
        this->template registerMethod<WillSaveTextDocumentParams,
                                      std::optional<std::vector<TextEdit>>,
                                      &Impl::getDocWillSaveWaitUntil>(
            "textDocument/willSaveWaitUntil");
    };
    /// A document will save notification is sent from the client to the server before
    /// the document is actually saved.
    virtual void onDocWillSave(const WillSaveTextDocumentParams&) {}

    void registerDocWillSave() {
        this->template registerNotification<WillSaveTextDocumentParams, &Impl::onDocWillSave>(
            "textDocument/willSave");
    };
    /// A request to resolve the type definition locations of a symbol at a given text
    /// document position. The request's parameter is of type {@link TextDocumentPositionParams}
    /// the response is of type {@link Definition} or a Thenable that resolves to such.
    virtual rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>
    getDocTypeDefinition(const TypeDefinitionParams&) {
        return rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>{};
    }

    void registerDocTypeDefinition() {
        this->template registerMethod<
            TypeDefinitionParams,
            rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>,
            &Impl::getDocTypeDefinition>("textDocument/typeDefinition");
    };
    virtual std::optional<SignatureHelp> getDocSignatureHelp(const SignatureHelpParams&) {
        return std::optional<SignatureHelp>{};
    }

    void registerDocSignatureHelp() {
        this->template registerMethod<SignatureHelpParams, std::optional<SignatureHelp>,
                                      &Impl::getDocSignatureHelp>("textDocument/signatureHelp");
    };
    /// @since 3.16.0
    virtual std::optional<SemanticTokens> getDocSemanticTokensRange(
        const SemanticTokensRangeParams&) {
        return std::optional<SemanticTokens>{};
    }

    void registerDocSemanticTokensRange() {
        this->template registerMethod<SemanticTokensRangeParams, std::optional<SemanticTokens>,
                                      &Impl::getDocSemanticTokensRange>(
            "textDocument/semanticTokens/range");
    };
    /// @since 3.16.0
    virtual rfl::Variant<SemanticTokens, SemanticTokensDelta, std::monostate>
    getDocSemanticTokensFullDelta(const SemanticTokensDeltaParams&) {
        return rfl::Variant<SemanticTokens, SemanticTokensDelta, std::monostate>{};
    }

    void registerDocSemanticTokensFullDelta() {
        this->template registerMethod<
            SemanticTokensDeltaParams,
            rfl::Variant<SemanticTokens, SemanticTokensDelta, std::monostate>,
            &Impl::getDocSemanticTokensFullDelta>("textDocument/semanticTokens/full/delta");
    };
    /// @since 3.16.0
    virtual std::optional<SemanticTokens> getDocSemanticTokensFull(const SemanticTokensParams&) {
        return std::optional<SemanticTokens>{};
    }

    void registerDocSemanticTokensFull() {
        this->template registerMethod<SemanticTokensParams, std::optional<SemanticTokens>,
                                      &Impl::getDocSemanticTokensFull>(
            "textDocument/semanticTokens/full");
    };
    /// A request to provide selection ranges in a document. The request's
    /// parameter is of type {@link SelectionRangeParams}, the
    /// response is of type {@link SelectionRange SelectionRange[]} or a Thenable
    /// that resolves to such.
    virtual std::optional<std::vector<SelectionRange>> getDocSelectionRange(
        const SelectionRangeParams&) {
        return std::optional<std::vector<SelectionRange>>{};
    }

    void registerDocSelectionRange() {
        this->template registerMethod<SelectionRangeParams,
                                      std::optional<std::vector<SelectionRange>>,
                                      &Impl::getDocSelectionRange>("textDocument/selectionRange");
    };
    /// A request to rename a symbol.
    virtual std::optional<WorkspaceEdit> getDocRename(const RenameParams&) {
        return std::optional<WorkspaceEdit>{};
    }

    void registerDocRename() {
        this->template registerMethod<RenameParams, std::optional<WorkspaceEdit>,
                                      &Impl::getDocRename>("textDocument/rename");
    };
    /// A request to resolve project-wide references for the symbol denoted
    /// by the given text document position. The request's parameter is of
    /// type {@link ReferenceParams} the response is of type
    /// {@link Location Location[]} or a Thenable that resolves to such.
    virtual std::optional<std::vector<Location>> getDocReferences(const ReferenceParams&) {
        return std::optional<std::vector<Location>>{};
    }

    void registerDocReferences() {
        this->template registerMethod<ReferenceParams, std::optional<std::vector<Location>>,
                                      &Impl::getDocReferences>("textDocument/references");
    };
    /// A request to format ranges in a document.
    ///
    /// @since 3.18.0
    /// @proposed
    virtual std::optional<std::vector<TextEdit>> getDocRangesFormatting(
        const DocumentRangesFormattingParams&) {
        return std::optional<std::vector<TextEdit>>{};
    }

    void registerDocRangesFormatting() {
        this->template registerMethod<DocumentRangesFormattingParams,
                                      std::optional<std::vector<TextEdit>>,
                                      &Impl::getDocRangesFormatting>(
            "textDocument/rangesFormatting");
    };
    /// A request to format a range in a document.
    virtual std::optional<std::vector<TextEdit>> getDocRangeFormatting(
        const DocumentRangeFormattingParams&) {
        return std::optional<std::vector<TextEdit>>{};
    }

    void registerDocRangeFormatting() {
        this->template registerMethod<DocumentRangeFormattingParams,
                                      std::optional<std::vector<TextEdit>>,
                                      &Impl::getDocRangeFormatting>("textDocument/rangeFormatting");
    };
    /// A request to result a `TypeHierarchyItem` in a document at a given position.
    /// Can be used as an input to a subtypes or supertypes type hierarchy.
    ///
    /// @since 3.17.0
    virtual std::optional<std::vector<TypeHierarchyItem>> getDocPrepareTypeHierarchy(
        const TypeHierarchyPrepareParams&) {
        return std::optional<std::vector<TypeHierarchyItem>>{};
    }

    void registerDocPrepareTypeHierarchy() {
        this->template registerMethod<TypeHierarchyPrepareParams,
                                      std::optional<std::vector<TypeHierarchyItem>>,
                                      &Impl::getDocPrepareTypeHierarchy>(
            "textDocument/prepareTypeHierarchy");
    };
    /// A request to test and perform the setup necessary for a rename.
    ///
    /// @since 3.16 - support for default behavior
    virtual std::optional<PrepareRenameResult> getDocPrepareRename(const PrepareRenameParams&) {
        return std::optional<PrepareRenameResult>{};
    }

    void registerDocPrepareRename() {
        this->template registerMethod<PrepareRenameParams, std::optional<PrepareRenameResult>,
                                      &Impl::getDocPrepareRename>("textDocument/prepareRename");
    };
    /// A request to result a `CallHierarchyItem` in a document at a given position.
    /// Can be used as an input to an incoming or outgoing call hierarchy.
    ///
    /// @since 3.16.0
    virtual std::optional<std::vector<CallHierarchyItem>> getDocPrepareCallHierarchy(
        const CallHierarchyPrepareParams&) {
        return std::optional<std::vector<CallHierarchyItem>>{};
    }

    void registerDocPrepareCallHierarchy() {
        this->template registerMethod<CallHierarchyPrepareParams,
                                      std::optional<std::vector<CallHierarchyItem>>,
                                      &Impl::getDocPrepareCallHierarchy>(
            "textDocument/prepareCallHierarchy");
    };
    /// A request to format a document on type.
    virtual std::optional<std::vector<TextEdit>> getDocOnTypeFormatting(
        const DocumentOnTypeFormattingParams&) {
        return std::optional<std::vector<TextEdit>>{};
    }

    void registerDocOnTypeFormatting() {
        this->template registerMethod<DocumentOnTypeFormattingParams,
                                      std::optional<std::vector<TextEdit>>,
                                      &Impl::getDocOnTypeFormatting>(
            "textDocument/onTypeFormatting");
    };
    /// A request to get the moniker of a symbol at a given text document position.
    /// The request parameter is of type {@link TextDocumentPositionParams}.
    /// The response is of type {@link Moniker Moniker[]} or `null`.
    virtual std::optional<std::vector<Moniker>> getDocMoniker(const MonikerParams&) {
        return std::optional<std::vector<Moniker>>{};
    }

    void registerDocMoniker() {
        this->template registerMethod<MonikerParams, std::optional<std::vector<Moniker>>,
                                      &Impl::getDocMoniker>("textDocument/moniker");
    };
    /// A request to provide ranges that can be edited together.
    ///
    /// @since 3.16.0
    virtual std::optional<LinkedEditingRanges> getDocLinkedEditingRange(
        const LinkedEditingRangeParams&) {
        return std::optional<LinkedEditingRanges>{};
    }

    void registerDocLinkedEditingRange() {
        this->template registerMethod<LinkedEditingRangeParams, std::optional<LinkedEditingRanges>,
                                      &Impl::getDocLinkedEditingRange>(
            "textDocument/linkedEditingRange");
    };
    /// A request to provide inline values in a document. The request's parameter is of
    /// type {@link InlineValueParams}, the response is of type
    /// {@link InlineValue InlineValue[]} or a Thenable that resolves to such.
    ///
    /// @since 3.17.0
    virtual std::optional<std::vector<InlineValue>> getDocInlineValue(const InlineValueParams&) {
        return std::optional<std::vector<InlineValue>>{};
    }

    void registerDocInlineValue() {
        this->template registerMethod<InlineValueParams, std::optional<std::vector<InlineValue>>,
                                      &Impl::getDocInlineValue>("textDocument/inlineValue");
    };
    /// A request to provide inline completions in a document. The request's parameter is of
    /// type {@link InlineCompletionParams}, the response is of type
    /// {@link InlineCompletion InlineCompletion[]} or a Thenable that resolves to such.
    ///
    /// @since 3.18.0
    /// @proposed
    virtual rfl::Variant<InlineCompletionList, std::vector<InlineCompletionItem>, std::monostate>
    getDocInlineCompletion(const InlineCompletionParams&) {
        return rfl::Variant<InlineCompletionList, std::vector<InlineCompletionItem>,
                            std::monostate>{};
    }

    void registerDocInlineCompletion() {
        this->template registerMethod<
            InlineCompletionParams,
            rfl::Variant<InlineCompletionList, std::vector<InlineCompletionItem>, std::monostate>,
            &Impl::getDocInlineCompletion>("textDocument/inlineCompletion");
    };
    /// A request to provide inlay hints in a document. The request's parameter is of
    /// type {@link InlayHintsParams}, the response is of type
    /// {@link InlayHint InlayHint[]} or a Thenable that resolves to such.
    ///
    /// @since 3.17.0
    virtual std::optional<std::vector<InlayHint>> getDocInlayHint(const InlayHintParams&) {
        return std::optional<std::vector<InlayHint>>{};
    }

    void registerDocInlayHint() {
        this->template registerMethod<InlayHintParams, std::optional<std::vector<InlayHint>>,
                                      &Impl::getDocInlayHint>("textDocument/inlayHint");
    };
    /// A request to resolve the implementation locations of a symbol at a given text
    /// document position. The request's parameter is of type {@link TextDocumentPositionParams}
    /// the response is of type {@link Definition} or a Thenable that resolves to such.
    virtual rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>
    getDocImplementation(const ImplementationParams&) {
        return rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>{};
    }

    void registerDocImplementation() {
        this->template registerMethod<
            ImplementationParams,
            rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>,
            &Impl::getDocImplementation>("textDocument/implementation");
    };
    /// Request to request hover information at a given text document position. The request's
    /// parameter is of type {@link TextDocumentPosition} the response is of
    /// type {@link Hover} or a Thenable that resolves to such.
    virtual std::optional<Hover> getDocHover(const HoverParams&) { return std::optional<Hover>{}; }

    void registerDocHover() {
        this->template registerMethod<HoverParams, std::optional<Hover>, &Impl::getDocHover>(
            "textDocument/hover");
    };
    /// A request to format a whole document.
    virtual std::optional<std::vector<TextEdit>> getDocFormatting(const DocumentFormattingParams&) {
        return std::optional<std::vector<TextEdit>>{};
    }

    void registerDocFormatting() {
        this->template registerMethod<DocumentFormattingParams,
                                      std::optional<std::vector<TextEdit>>,
                                      &Impl::getDocFormatting>("textDocument/formatting");
    };
    /// A request to provide folding ranges in a document. The request's
    /// parameter is of type {@link FoldingRangeParams}, the
    /// response is of type {@link FoldingRangeList} or a Thenable
    /// that resolves to such.
    virtual std::optional<std::vector<FoldingRange>> getDocFoldingRange(const FoldingRangeParams&) {
        return std::optional<std::vector<FoldingRange>>{};
    }

    void registerDocFoldingRange() {
        this->template registerMethod<FoldingRangeParams, std::optional<std::vector<FoldingRange>>,
                                      &Impl::getDocFoldingRange>("textDocument/foldingRange");
    };
    /// A request to list all symbols found in a given text document. The request's
    /// parameter is of type {@link TextDocumentIdentifier} the
    /// response is of type {@link SymbolInformation SymbolInformation[]} or a Thenable
    /// that resolves to such.
    virtual rfl::Variant<std::vector<SymbolInformation>, std::vector<DocumentSymbol>,
                         std::monostate>
    getDocDocumentSymbol(const DocumentSymbolParams&) {
        return rfl::Variant<std::vector<SymbolInformation>, std::vector<DocumentSymbol>,
                            std::monostate>{};
    }

    void registerDocDocumentSymbol() {
        this->template registerMethod<DocumentSymbolParams,
                                      rfl::Variant<std::vector<SymbolInformation>,
                                                   std::vector<DocumentSymbol>, std::monostate>,
                                      &Impl::getDocDocumentSymbol>("textDocument/documentSymbol");
    };
    /// A request to provide document links
    virtual std::optional<std::vector<DocumentLink>> getDocDocumentLink(const DocumentLinkParams&) {
        return std::optional<std::vector<DocumentLink>>{};
    }

    void registerDocDocumentLink() {
        this->template registerMethod<DocumentLinkParams, std::optional<std::vector<DocumentLink>>,
                                      &Impl::getDocDocumentLink>("textDocument/documentLink");
    };
    /// Request to resolve a {@link DocumentHighlight} for a given
    /// text document position. The request's parameter is of type {@link TextDocumentPosition}
    /// the request response is an array of type {@link DocumentHighlight}
    /// or a Thenable that resolves to such.
    virtual std::optional<std::vector<DocumentHighlight>> getDocDocumentHighlight(
        const DocumentHighlightParams&) {
        return std::optional<std::vector<DocumentHighlight>>{};
    }

    void registerDocDocumentHighlight() {
        this->template registerMethod<DocumentHighlightParams,
                                      std::optional<std::vector<DocumentHighlight>>,
                                      &Impl::getDocDocumentHighlight>(
            "textDocument/documentHighlight");
    };
    /// A request to list all color symbols found in a given text document. The request's
    /// parameter is of type {@link DocumentColorParams} the
    /// response is of type {@link ColorInformation ColorInformation[]} or a Thenable
    /// that resolves to such.
    virtual std::vector<ColorInformation> getDocDocumentColor(const DocumentColorParams&) {
        return std::vector<ColorInformation>{};
    }

    void registerDocDocumentColor() {
        this->template registerMethod<DocumentColorParams, std::vector<ColorInformation>,
                                      &Impl::getDocDocumentColor>("textDocument/documentColor");
    };
    /// The document save notification is sent from the client to the server when
    /// the document got saved in the client.
    virtual void onDocDidSave(const DidSaveTextDocumentParams&) {}

    void registerDocDidSave() {
        this->template registerNotification<DidSaveTextDocumentParams, &Impl::onDocDidSave>(
            "textDocument/didSave");
    };
    /// The document open notification is sent from the client to the server to signal
    /// newly opened text documents. The document's truth is now managed by the client
    /// and the server must not try to read the document's truth using the document's
    /// uri. Open in this sense means it is managed by the client. It doesn't necessarily
    /// mean that its content is presented in an editor. An open notification must not
    /// be sent more than once without a corresponding close notification send before.
    /// This means open and close notification must be balanced and the max open count
    /// is one.
    virtual void onDocDidOpen(const DidOpenTextDocumentParams&) {}

    void registerDocDidOpen() {
        this->template registerNotification<DidOpenTextDocumentParams, &Impl::onDocDidOpen>(
            "textDocument/didOpen");
    };
    /// The document close notification is sent from the client to the server when
    /// the document got closed in the client. The document's truth now exists where
    /// the document's uri points to (e.g. if the document's uri is a file uri the
    /// truth now exists on disk). As with the open notification the close notification
    /// is about managing the document's content. Receiving a close notification
    /// doesn't mean that the document was open in an editor before. A close
    /// notification requires a previous open notification to be sent.
    virtual void onDocDidClose(const DidCloseTextDocumentParams&) {}

    void registerDocDidClose() {
        this->template registerNotification<DidCloseTextDocumentParams, &Impl::onDocDidClose>(
            "textDocument/didClose");
    };
    /// The document change notification is sent from the client to the server to signal
    /// changes to a text document.
    virtual void onDocDidChange(const DidChangeTextDocumentParams&) {}

    void registerDocDidChange() {
        this->template registerNotification<DidChangeTextDocumentParams, &Impl::onDocDidChange>(
            "textDocument/didChange");
    };
    /// The document diagnostic request definition.
    ///
    /// @since 3.17.0
    virtual DocumentDiagnosticReport getDocDiagnostic(const DocumentDiagnosticParams&) {
        return DocumentDiagnosticReport{};
    }

    void registerDocDiagnostic() {
        this->template registerMethod<DocumentDiagnosticParams, DocumentDiagnosticReport,
                                      &Impl::getDocDiagnostic>("textDocument/diagnostic");
    };
    /// A request to resolve the definition location of a symbol at a given text
    /// document position. The request's parameter is of type {@link TextDocumentPosition}
    /// the response is of either type {@link Definition} or a typed array of
    /// {@link DefinitionLink} or a Thenable that resolves to such.
    virtual rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate> getDocDefinition(
        const DefinitionParams&) {
        return rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>{};
    }

    void registerDocDefinition() {
        this->template registerMethod<
            DefinitionParams, rfl::Variant<Definition, std::vector<DefinitionLink>, std::monostate>,
            &Impl::getDocDefinition>("textDocument/definition");
    };
    /// A request to resolve the type definition locations of a symbol at a given text
    /// document position. The request's parameter is of type {@link TextDocumentPositionParams}
    /// the response is of type {@link Declaration} or a typed array of {@link DeclarationLink}
    /// or a Thenable that resolves to such.
    virtual rfl::Variant<Declaration, std::vector<DeclarationLink>, std::monostate>
    getDocDeclaration(const DeclarationParams&) {
        return rfl::Variant<Declaration, std::vector<DeclarationLink>, std::monostate>{};
    }

    void registerDocDeclaration() {
        this->template registerMethod<
            DeclarationParams,
            rfl::Variant<Declaration, std::vector<DeclarationLink>, std::monostate>,
            &Impl::getDocDeclaration>("textDocument/declaration");
    };
    /// Request to request completion at a given text document position. The request's
    /// parameter is of type {@link TextDocumentPosition} the response
    /// is of type {@link CompletionItem CompletionItem[]} or {@link CompletionList}
    /// or a Thenable that resolves to such.
    ///
    /// The request can delay the computation of the {@link CompletionItem.detail `detail`}
    /// and {@link CompletionItem.documentation `documentation`} properties to the
    /// `completionItem/resolve` request. However, properties that are needed for the initial
    /// sorting and filtering, like `sortText`, `filterText`, `insertText`, and `textEdit`, must not
    /// be changed during resolve.
    virtual rfl::Variant<std::vector<CompletionItem>, CompletionList, std::monostate>
    getDocCompletion(const CompletionParams&) {
        return rfl::Variant<std::vector<CompletionItem>, CompletionList, std::monostate>{};
    }

    void registerDocCompletion() {
        this->template registerMethod<
            CompletionParams,
            rfl::Variant<std::vector<CompletionItem>, CompletionList, std::monostate>,
            &Impl::getDocCompletion>("textDocument/completion");
    };
    /// A request to list all presentation for a color. The request's
    /// parameter is of type {@link ColorPresentationParams} the
    /// response is of type {@link ColorInformation ColorInformation[]} or a Thenable
    /// that resolves to such.
    virtual std::vector<ColorPresentation> getDocColorPresentation(const ColorPresentationParams&) {
        return std::vector<ColorPresentation>{};
    }

    void registerDocColorPresentation() {
        this->template registerMethod<ColorPresentationParams, std::vector<ColorPresentation>,
                                      &Impl::getDocColorPresentation>(
            "textDocument/colorPresentation");
    };
    /// A request to provide code lens for the given text document.
    virtual std::optional<std::vector<CodeLens>> getDocCodeLens(const CodeLensParams&) {
        return std::optional<std::vector<CodeLens>>{};
    }

    void registerDocCodeLens() {
        this->template registerMethod<CodeLensParams, std::optional<std::vector<CodeLens>>,
                                      &Impl::getDocCodeLens>("textDocument/codeLens");
    };
    /// A request to provide commands for the given text document and range.
    virtual std::optional<std::vector<rfl::Variant<Command, CodeAction>>> getDocCodeAction(
        const CodeActionParams&) {
        return std::optional<std::vector<rfl::Variant<Command, CodeAction>>>{};
    }

    void registerDocCodeAction() {
        this->template registerMethod<CodeActionParams,
                                      std::optional<std::vector<rfl::Variant<Command, CodeAction>>>,
                                      &Impl::getDocCodeAction>("textDocument/codeAction");
    };
    /// A shutdown request is sent from the client to the server.
    /// It is sent once when the client decides to shutdown the
    /// server. The only notification that is sent after a shutdown request
    /// is the exit event.
    virtual std::monostate getShutdown(std::monostate) { return std::monostate{}; }

    void registerShutdown() {
        this->template registerMethod<std::nullopt_t, std::monostate, &Impl::getShutdown>(
            "shutdown");
    };
    /// A notification sent when a notebook document is saved.
    ///
    /// @since 3.17.0
    virtual void onNotebookDidSave(const DidSaveNotebookDocumentParams&) {}

    void registerNotebookDidSave() {
        this->template registerNotification<DidSaveNotebookDocumentParams,
                                            &Impl::onNotebookDidSave>("notebookDocument/didSave");
    };
    /// A notification sent when a notebook opens.
    ///
    /// @since 3.17.0
    virtual void onNotebookDidOpen(const DidOpenNotebookDocumentParams&) {}

    void registerNotebookDidOpen() {
        this->template registerNotification<DidOpenNotebookDocumentParams,
                                            &Impl::onNotebookDidOpen>("notebookDocument/didOpen");
    };
    /// A notification sent when a notebook closes.
    ///
    /// @since 3.17.0
    virtual void onNotebookDidClose(const DidCloseNotebookDocumentParams&) {}

    void registerNotebookDidClose() {
        this->template registerNotification<DidCloseNotebookDocumentParams,
                                            &Impl::onNotebookDidClose>("notebookDocument/didClose");
    };
    virtual void onNotebookDidChange(const DidChangeNotebookDocumentParams&) {}

    void registerNotebookDidChange() {
        this->template registerNotification<DidChangeNotebookDocumentParams,
                                            &Impl::onNotebookDidChange>(
            "notebookDocument/didChange");
    };
    /// A request to resolve additional properties for an inlay hint.
    /// The request's parameter is of type {@link InlayHint}, the response is
    /// of type {@link InlayHint} or a Thenable that resolves to such.
    ///
    /// @since 3.17.0
    virtual InlayHint getInlayHintResolve(const InlayHint&) { return InlayHint{}; }

    void registerInlayHintResolve() {
        this->template registerMethod<InlayHint, InlayHint, &Impl::getInlayHintResolve>(
            "inlayHint/resolve");
    };
    /// The initialized notification is sent from the client to the
    /// server after the client is fully initialized and the server
    /// is allowed to send requests from the server to the client.
    virtual void onInitialized(const InitializedParams&) {}

    void registerInitialized() {
        this->template registerNotification<InitializedParams, &Impl::onInitialized>("initialized");
    };
    /// The initialize request is sent from the client to the server.
    /// It is sent once as the request after starting up the server.
    /// The requests parameter is of type {@link InitializeParams}
    /// the response if of type {@link InitializeResult} of a Thenable that
    /// resolves to such.
    virtual InitializeResult getInitialize(const InitializeParams&) { return InitializeResult{}; }

    void registerInitialize() {
        this->template registerMethod<InitializeParams, InitializeResult, &Impl::getInitialize>(
            "initialize");
    };
    /// The exit event is sent from the client to the server to
    /// ask the server to exit its process.
    virtual void onExit(std::monostate) {}

    void registerExit() {
        this->template registerNotification<std::nullopt_t, &Impl::onExit>("exit");
    };
    /// Request to resolve additional information for a given document link. The request's
    /// parameter is of type {@link DocumentLink} the response
    /// is of type {@link DocumentLink} or a Thenable that resolves to such.
    virtual DocumentLink getDocumentLinkResolve(const DocumentLink&) { return DocumentLink{}; }

    void registerDocumentLinkResolve() {
        this->template registerMethod<DocumentLink, DocumentLink, &Impl::getDocumentLinkResolve>(
            "documentLink/resolve");
    };
    /// Request to resolve additional information for a given completion item.The request's
    /// parameter is of type {@link CompletionItem} the response
    /// is of type {@link CompletionItem} or a Thenable that resolves to such.
    virtual CompletionItem getCompletionItemResolve(const CompletionItem&) {
        return CompletionItem{};
    }

    void registerCompletionItemResolve() {
        this->template registerMethod<CompletionItem, CompletionItem,
                                      &Impl::getCompletionItemResolve>("completionItem/resolve");
    };
    /// A request to resolve a command for a given code lens.
    virtual CodeLens getCodeLensResolve(const CodeLens&) { return CodeLens{}; }

    void registerCodeLensResolve() {
        this->template registerMethod<CodeLens, CodeLens, &Impl::getCodeLensResolve>(
            "codeLens/resolve");
    };
    /// Request to resolve additional information for a given code action.The request's
    /// parameter is of type {@link CodeAction} the response
    /// is of type {@link CodeAction} or a Thenable that resolves to such.
    virtual CodeAction getCodeActionResolve(const CodeAction&) { return CodeAction{}; }

    void registerCodeActionResolve() {
        this->template registerMethod<CodeAction, CodeAction, &Impl::getCodeActionResolve>(
            "codeAction/resolve");
    };
    /// A request to resolve the outgoing calls for a given `CallHierarchyItem`.
    ///
    /// @since 3.16.0
    virtual std::optional<std::vector<CallHierarchyOutgoingCall>> getCallHierarchyOutgoingCalls(
        const CallHierarchyOutgoingCallsParams&) {
        return std::optional<std::vector<CallHierarchyOutgoingCall>>{};
    }

    void registerCallHierarchyOutgoingCalls() {
        this->template registerMethod<CallHierarchyOutgoingCallsParams,
                                      std::optional<std::vector<CallHierarchyOutgoingCall>>,
                                      &Impl::getCallHierarchyOutgoingCalls>(
            "callHierarchy/outgoingCalls");
    };
    /// A request to resolve the incoming calls for a given `CallHierarchyItem`.
    ///
    /// @since 3.16.0
    virtual std::optional<std::vector<CallHierarchyIncomingCall>> getCallHierarchyIncomingCalls(
        const CallHierarchyIncomingCallsParams&) {
        return std::optional<std::vector<CallHierarchyIncomingCall>>{};
    }

    void registerCallHierarchyIncomingCalls() {
        this->template registerMethod<CallHierarchyIncomingCallsParams,
                                      std::optional<std::vector<CallHierarchyIncomingCall>>,
                                      &Impl::getCallHierarchyIncomingCalls>(
            "callHierarchy/incomingCalls");
    };
    virtual void onSetTrace(const SetTraceParams&) {}

    void registerSetTrace() {
        this->template registerNotification<SetTraceParams, &Impl::onSetTrace>("$/setTrace");
    };
    virtual void onProgress(const ProgressParams&) {}

    void registerProgress() {
        this->template registerNotification<ProgressParams, &Impl::onProgress>("$/progress");
    };
    virtual void onCancelRequest(const CancelParams&) {}

    void registerCancelRequest() {
        this->template registerNotification<CancelParams, &Impl::onCancelRequest>(
            "$/cancelRequest");
    };
};
} // namespace lsp
