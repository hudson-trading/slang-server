// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "ClientHarness.h"
#include "GoldenTest.h"
#include "SlangServer.h"
#include "Utils.h"
#include "document/ShallowAnalysis.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "lsp/SnippetString.h"
#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "slang/parsing/Token.h"

class DocumentHandle;
class Cursor;

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

    // WCP helpers
    void checkGetInstances(const Cursor& cursor, const std::set<std::string>& expected);
    void checkGotoDeclaration(const std::string& path, const Cursor* expectedLocation = nullptr);

    // Cone helpers
    void checkPrepareCallHierarchy(const Cursor& cursor, const std::set<std::string>& expected);

    struct ExpectedHierResult {
        std::string name;
        const Cursor* cursor;

        auto operator<=>(const ExpectedHierResult& other) const {
            if (auto cmp = name <=> other.name; cmp != 0)
                return cmp;
            return cursor <=> other.cursor;
        }
        bool operator==(const ExpectedHierResult& other) const = default;
    };
    void checkIncomingCalls(const std::string& path, const std::set<ExpectedHierResult>& expected);
    void checkOutgoingCalls(const std::string& path, const std::set<ExpectedHierResult>& expected);

    std::shared_ptr<server::SlangDoc> getDoc(const URI& uri);

    // For access to isWcpVariable
    // TODO -- remove once isWcpVariable is removed
    using SlangServer::m_driver;
};

enum DocState {
    Open,
    Closed,
    Dirty, // Changes to be published
};

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
    void insert(lsp::uint offset, std::string text);
    void append(std::string text);
    void erase(size_t start, size_t end);

    Cursor before(std::string before, lsp::uint start_pos = 0);
    Cursor after(std::string after, lsp::uint start_pos = 0);
    Cursor end();
    Cursor begin();

    void publishChanges();
    void ensureSynced();

    void save();
    void close();
    void open();

    /// @brief Get the line at a given line number
    /// @param line The line number (0-based / Slang lines)
    std::string_view getLine(lsp::uint line) {
        return m_server.sourceManager().getLine(doc->getBuffer(), line);
    }

    lsp::Position getPosition(lsp::uint offset);
    std::vector<lsp::DocumentSymbol> getSymbolTree();
    std::vector<lsp::Diagnostic> getDiagnostics();

    /// @brief Get the source location for an offset
    /// @param offset The byte offset in the document
    /// @return Optional source location
    std::optional<slang::SourceLocation> getLocation(lsp::uint offset);

    /// @brief Get the LSP position for an offset
    std::optional<lsp::Position> getLspLocation(lsp::uint offset);

    std::optional<server::DefinitionInfo> getDefinitionInfoAt(lsp::uint offset);

    std::optional<lsp::Hover> getHoverAt(lsp::uint offset);

    /// @brief Get all inlay hints for the entire document
    std::vector<lsp::InlayHint> getAllInlayHints();

    /// @brief Apply text edits to the document and return the resulting text
    std::string withTextEdits(std::vector<lsp::TextEdit> edits);

    std::string m_text;
    URI m_uri;
    ServerHarness& m_server;
    lsp::uint m_version = 0;
};

class Cursor {
public:
    Cursor(DocumentHandle& doc, lsp::uint offset) : m_doc(doc), m_offset(offset) {}
    lsp::Position getPosition() const { return m_doc.getPosition(m_offset); }
    URI getUri() const { return m_doc.m_uri; }
    Cursor& write(const std::string& text);

    std::vector<CompletionHandle> getCompletions(
        std::optional<std::string> triggerChar = std::nullopt);

    // Get completions with automatic resolution of all items
    std::vector<lsp::CompletionItem> getResolvedCompletions(
        std::optional<std::string> triggerChar = std::nullopt);

    // Goto definition methods
    bool hasDefinition();
    std::vector<lsp::LocationLink> getDefinitions();

    // Chaining search methods
    Cursor before(const std::string& before);
    Cursor after(const std::string& after);

    DocumentHandle& m_doc;
    lsp::uint m_offset;

    Cursor& operator--() {
        --m_offset;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream&, const Cursor&);
};

static std::string resolveTabsToSpaces(std::string_view snippet, int tabSize = 4) {
    std::string result;
    result.reserve(snippet.length());
    for (char c : snippet) {
        if (c == '\t') {
            result.append(tabSize, ' ');
        }
        else {
            result += c;
        }
    }
    return result;
}

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

template<typename ElementT>
class DocumentScanner {
public:
    void scanDocument(DocumentHandle hdl) {
        auto doc = hdl.doc;
        if (!doc) {
            FAIL("Failed to get SlangDoc");
            return;
        }
        auto& sm = doc->getSourceManager();

        // Record the first line
        test.record(hdl.getLine(0));

        SourceLocation prevLoc;

        lsp::uint colNum = 0;

        auto data = doc->getText();

        for (lsp::uint offset = 0; offset < data.size() - 1; offset++) {
            auto locOpt = hdl.getLocation(offset);
            auto loc = *locOpt;
            auto line = sm.getLineNumber(loc);

            bool newLine = line != sm.getLineNumber(prevLoc);

            // Get current element
            std::optional<std::variant<ElementT, std::string>> currentElement;
            try {
                currentElement = getElementAt(&hdl, offset);
            }
            catch (...) {
                currentElement = "Exception occurred";
            }
            bool newElement = currentElement != prevElement;

            // Process element transition
            if (prevElement.has_value() && (newLine || newElement)) {
                if (std::holds_alternative<std::string>(*prevElement)) {
                    test.record(std::get<std::string>(*prevElement) + "\n");
                }
                else {
                    processElementTransition(&hdl, sm, offset - 1);
                }
            }

            // Handle new line
            if (offset == 0 || newLine) {
                test.record("\n");
                test.record(hdl.getLine(line));
                colNum = 0;
            }

            // Record marker if needed
            if (currentElement.has_value()) {
                if (newElement) {
                    test.record(std::string(colNum, ' '));
                }
                test.record("^");
            }

            // Update for next iteration
            colNum++;
            prevLoc = loc;
            prevElement = currentElement;
        }
    }

protected:
    GoldenTest test;
    std::optional<std::variant<ElementT, std::string>> prevElement;

    // These methods should be overridden by derived classes
    virtual std::optional<ElementT> getElementAt(DocumentHandle* hdl, lsp::uint offset) = 0;
    virtual void processElementTransition(DocumentHandle* hdl, SourceManager& sm,
                                          lsp::uint offset) = 0;
};

class SyntaxScanner : public DocumentScanner<parsing::Token> {
public:
    SyntaxScanner() : DocumentScanner<parsing::Token>() {}

protected:
    std::optional<parsing::Token> getElementAt(DocumentHandle* hdl, lsp::uint offset) override {
        auto doc = hdl->doc;
        auto tok = doc->getTokenAt(slang::SourceLocation(doc->getBuffer(), offset));
        if (!tok) {
            return std::nullopt;
        }
        return *tok;
    }

    void processElementTransition(DocumentHandle*, SourceManager&, lsp::uint) override {
        test.record(fmt::format(" {}\n", toString(std::get<parsing::Token>(*prevElement).kind)));
    }
};

class SymbolRefScanner : public DocumentScanner<server::DefinitionInfo> {
public:
    SymbolRefScanner() : DocumentScanner<server::DefinitionInfo>() {}

protected:
    std::optional<server::DefinitionInfo> getElementAt(DocumentHandle* hdl,
                                                       lsp::uint offset) override {
        return hdl->getDefinitionInfoAt(offset);
    }

    void processElementTransition(DocumentHandle* hdl, SourceManager&, lsp::uint offset) override {
        // Get the current syntax node at the symbol's location
        auto doc = hdl->doc;
        auto tok = doc->getWordTokenAt(slang::SourceLocation(doc->getBuffer(), offset));

        auto pElem = std::get<server::DefinitionInfo>(*prevElement);
        if (tok && pElem.nameToken.location() == tok->location()) {
            test.record(fmt::format(" Sym {} : {}\n", pElem.nameToken.valueText(),
                                    toString(pElem.node->kind)));
        }
        else {
            test.record(fmt::format(" Ref -> "));
            // Print hover, but turn newlines into \n
            // auto maybeHover = doc->getAnalysis().getDocHover(hdl->getPosition(offset), true);
            auto maybeHover = hdl->getHoverAt(offset);
            if (!maybeHover) {
                test.record(" No Hover\n");
                return;
            }
            auto hover = rfl::get<lsp::MarkupContent>(maybeHover->contents);
            auto hoverText = hover.value;
            // Make the code blocks more readable
            auto replace = [&](std::string& str, const std::string& from, const std::string& to) {
                size_t start_pos = 0;
                while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                    str.replace(start_pos, from.length(), to);
                    start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
                }
            };
            replace(hoverText, "````systemverilog\n", "`");
            replace(hoverText, "\n````", "`");
            std::string singleLine;
            for (char c : hoverText) {
                if (c == '\n' || c == '\r') {
                    singleLine += "\\n\\";
                }
                else {
                    singleLine += c;
                }
            }
            test.record(singleLine + "\n");
        }
    }
};

struct ExpectedStart {
    std::string name;
    std::string uri;
    lsp::Position start;

    auto operator<=>(const ExpectedStart& other) const {
        if (auto cmp = name <=> other.name; cmp != 0)
            return cmp;
        if (auto cmp = uri <=> other.uri; cmp != 0)
            return cmp;
        if (auto cmp = start.line <=> other.start.line; cmp != 0)
            return cmp;
        return start.character <=> other.start.character;
    }
    bool operator==(const ExpectedStart& other) const = default;

    friend std::ostream& operator<<(std::ostream&, const struct ExpectedStart&);
};
