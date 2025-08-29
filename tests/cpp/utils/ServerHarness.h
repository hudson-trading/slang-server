// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "ClientHarness.h"
#include "SlangServer.h"
#include "Utils.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "lsp/SnippetString.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class DocumentHandle;

struct ClientOwner {
    /// This needs to be made before passing to SlangServer
    ClientHarness client;
};

class ServerHarness : private ClientOwner, public server::SlangServer {
public:
    using ClientOwner::client;

    // Constructor with custom initialization parameters, no workspace folder set
    explicit ServerHarness(lsp::InitializeParams params = {}) : ClientOwner(), SlangServer(client) {
        getInitialize(params);
        onInitialized(lsp::InitializedParams{});
    }

    // Constructor with repository root - equivalent to old initRepo()
    explicit ServerHarness(const std::string& repoRoot) : ClientOwner(), SlangServer(client) {
        auto repoDir = (findSlangRoot() / "tests/data" / repoRoot);
        fs::current_path(repoDir);
        getInitialize(lsp::InitializeParams{.workspaceFolders = {{lsp::WorkspaceFolder{
                                                .uri = URI::fromFile(repoDir), .name = "test"}}}});
        onInitialized(lsp::InitializedParams{});
    }

    DocumentHandle openFile(std::string fileName);
    DocumentHandle openFile(std::string fileName, std::string text);

    void expectError(const std::string& msg) { client.expectError(msg); }

    // Helper method for goto definition tests
    bool hasDefinition(const lsp::DefinitionParams& params);

    std::shared_ptr<server::SlangDoc> getDoc(const URI& uri);
};

enum DocState {
    Open,
    Closed,
    Dirty, // Changes to be published
};

class Cursor;
class CompletionHandle;

// Perform client actions on a document, and inspect the server-side document
class DocumentHandle {
public:
    DocumentHandle(ServerHarness& server, URI uri, std::string text);

    std::shared_ptr<server::SlangDoc> doc;

    DocState state = DocState::Open;
    std::vector<lsp::TextDocumentContentChangeEvent> pending_changes;

    std::string getText() const;

    // onChange functions
    void insert(uint offset, std::string text);
    void append(std::string text);
    void erase(int start, int end);

    Cursor before(std::string before);
    Cursor after(std::string after);
    Cursor end();
    Cursor begin();

    void publishChanges();

    void save();
    void close();
    void open();

    /// @brief Get the line at a given line number
    /// @param line The line number (0-based / Slang lines)
    std::string_view getLine(uint line) {
        return m_server.sourceManager().getLine(doc->getBuffer(), line);
    }

    lsp::Position getPosition(uint offset);
    std::vector<lsp::DocumentSymbol> getSymbolTree();
    std::vector<lsp::Diagnostic> getDiagnostics();

    /// @brief Get the source location for an offset
    /// @param offset The byte offset in the document
    /// @return Optional source location
    std::optional<slang::SourceLocation> getLocation(uint offset);

    /// @brief Get the LSP position for an offset
    std::optional<lsp::Position> getLspLocation(uint offset);

    std::optional<server::DefinitionInfo> getDefinitionInfoAt(uint offset);

    std::string m_text;
    URI m_uri;
    ServerHarness& m_server;
    uint m_version = 0;
};

class Cursor {
public:
    Cursor(DocumentHandle& doc, uint offset) : m_doc(doc), m_offset(offset) {}
    lsp::Position getPosition() const { return m_doc.getPosition(m_offset); }
    Cursor& write(const std::string& text);

    std::vector<CompletionHandle> getCompletions(
        std::optional<std::string> triggerChar = std::nullopt);
    
    // Get completions with automatic resolution of all items
    std::vector<lsp::CompletionItem> getResolvedCompletions(
        std::optional<std::string> triggerChar = std::nullopt);

    // Goto definition methods
    bool hasDefinition();
    std::vector<lsp::LocationLink> getDefinitions();

    DocumentHandle& m_doc;
    uint m_offset;
};

class CompletionHandle {
    // Handle that's returned from getCompletions(). Completions are returned in a list with
    // name/detail, then the remaining fields are "resolved" via later calls.
public:
    Cursor m_cursor;
    lsp::CompletionItem m_item;
    CompletionHandle(Cursor& cursor, lsp::CompletionItem item) :
        m_cursor(cursor), m_item(std::move(item)) {}

    void resolve() {
        m_item = m_cursor.m_doc.m_server.getCompletionItemResolve(m_item);
        // Convert the tabs to spaces, as a client would (tests need to be in spaces)
        if (m_item.insertTextFormat == lsp::InsertTextFormat::Snippet) {
            m_item.insertText = resolveTabsToSpaces(m_item.insertText.value_or(""), 4);
        }
    }

    void insert() { m_cursor.write(m_item.insertText.value_or(m_item.label)); }
};
