// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "Utils.h"
#include "lsp/LspTypes.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#define CATCH_CONFIG_RUNNER
#include "ServerHarness.h"
#include <fstream>

DocumentHandle ServerHarness::openFile(std::string fileName) {
    auto root = m_workspaceFolder ? (m_workspaceFolder->uri.getPath()) : findSlangRoot();
    std::ifstream file(root / fileName);
    std::string text;

    if (file) {
        std::string line;
        while (std::getline(file, line)) {
            text += line + "\n";
        }
    }
    else {
        throw std::runtime_error("Failed to open file: " + fileName);
    }

    auto uri = URI::fromFile(root / fileName);
    onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = uri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = text}});

    auto tree = getDocDocumentSymbol(
        lsp::DocumentSymbolParams{.textDocument = lsp::TextDocumentIdentifier{.uri = uri}});
    auto syms = rfl::get<std::vector<lsp::DocumentSymbol>>(tree);
    CHECK(!syms.empty());

    return DocumentHandle(*this, uri, text);
}

DocumentHandle ServerHarness::openFile(std::string fileName, std::string text) {
    auto root = m_workspaceFolder ? (m_workspaceFolder->uri.getPath()) : findSlangRoot();
    auto uri = URI::fromFile(root / fileName);

    onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = uri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = text}});

    return DocumentHandle(*this, uri, text);
}

bool ServerHarness::hasDefinition(const lsp::DefinitionParams& params) {
    auto result = getDocDefinition(params);
    return !rfl::holds_alternative<std::monostate>(result);
}

std::shared_ptr<server::SlangDoc> ServerHarness::getDoc(const URI& uri) {
    if (!m_driver) {
        return nullptr;
    }
    auto doc = m_driver->docs.find(uri);
    if (doc == m_driver->docs.end()) {
        return nullptr;
    }
    return doc->second;
}

// ------------------- DocumentHandle -------------------

DocumentHandle::DocumentHandle(ServerHarness& server, URI uri, std::string text) :
    m_text(std::move(text)), m_uri(uri), m_server(server) {
    doc = m_server.getDoc(m_uri);
}

std::string DocumentHandle::getText() const {
    return m_text;
}

void DocumentHandle::insert(lsp::uint offset, std::string text) {
    CHECK(state != DocState::Closed);

    m_text.insert(offset, text);
    auto pos = getPosition(offset);
    pending_changes.push_back(
        {lsp::TextDocumentContentChangePartial{.range = lsp::Range{pos, pos}, .text = text}});

    state = DocState::Dirty;
}

Cursor DocumentHandle::before(std::string before) {
    auto idx = m_text.find(before);
    if (idx == std::string::npos) {
        throw std::runtime_error(fmt::format("String '{}' not found in document", before));
    }
    return Cursor(*this, idx);
}

Cursor DocumentHandle::after(std::string after) {
    auto idx = m_text.find(after);
    if (idx == std::string::npos) {
        throw std::runtime_error(fmt::format("String '{}' not found in document", after));
    }
    return Cursor(*this, idx + after.size());
}

Cursor DocumentHandle::end() {
    return Cursor(*this, m_text.size());
}

Cursor DocumentHandle::begin() {
    return Cursor(*this, 0);
}

void DocumentHandle::append(std::string text) {
    insert(m_text.size(), text);
}

void DocumentHandle::erase(int start, int end) {
    CHECK(state != DocState::Closed);

    pending_changes.push_back({lsp::TextDocumentContentChangePartial{
        .range = lsp::Range{getPosition(start), getPosition(end)}, .text = ""}});
    m_text.erase(start, end - start);

    state = DocState::Dirty;
}

void DocumentHandle::publishChanges() {
    CHECK(state == DocState::Dirty);
    m_server.onDocDidChange(lsp::DidChangeTextDocumentParams{
        .textDocument = lsp::VersionedTextDocumentIdentifier{.uri = m_uri},
        .contentChanges = pending_changes});
    pending_changes.clear();

    state = DocState::Open;
}

void DocumentHandle::save() {
    if (!pending_changes.empty()) {
        publishChanges();
    }
    m_server.onDocDidSave(lsp::DidSaveTextDocumentParams{
        .textDocument = lsp::TextDocumentIdentifier{.uri = m_uri}, .text = m_text});
    state = DocState::Open;
}

void DocumentHandle::close() {
    CHECK(state == DocState::Open);
    m_server.onDocDidClose(
        lsp::DidCloseTextDocumentParams{.textDocument = lsp::TextDocumentIdentifier{.uri = m_uri}});
    state = DocState::Closed;
}

void DocumentHandle::open() {
    CHECK(state == DocState::Closed);

    m_server.onDocDidOpen(
        lsp::DidOpenTextDocumentParams{.textDocument = lsp::TextDocumentItem{
                                           .uri = m_uri,
                                           .languageId = lsp::LanguageKind::make<"systemverilog">(),
                                           .version = 1,
                                           .text = m_text}});
    state = DocState::Open;
}

lsp::Position DocumentHandle::getPosition(lsp::uint offset) {
    lsp::uint line = 0, col = 0;
    for (lsp::uint i = 0; i < offset; i++) {
        if (m_text[i] == '\n') {
            line++;
            col = 0;
        }
        else {
            col++;
        }
    }
    return lsp::Position{line, col};
}

std::vector<lsp::DocumentSymbol> DocumentHandle::getSymbolTree() {
    auto params = lsp::DocumentSymbolParams{
        .textDocument = lsp::TextDocumentIdentifier{.uri = m_uri}};
    auto result = m_server.getDocDocumentSymbol(params);
    return rfl::get<std::vector<lsp::DocumentSymbol>>(result);
}

std::vector<lsp::Diagnostic> DocumentHandle::getDiagnostics() {
    return m_server.client.getDiagnostics(m_uri);
}

Cursor& Cursor::write(const std::string& text) {
    m_doc.insert(m_offset, text);
    m_offset += text.size();
    return *this;
}

std::vector<CompletionHandle> Cursor::getCompletions(std::optional<std::string> triggerChar) {
    auto ret = m_doc.m_server.getDocCompletion(lsp::CompletionParams{
        .context =
            lsp::CompletionContext{
                .triggerKind = triggerChar ? lsp::CompletionTriggerKind::TriggerCharacter
                                           : lsp::CompletionTriggerKind::Invoked,
                .triggerCharacter = triggerChar,
            },
        .textDocument = lsp::TextDocumentIdentifier{m_doc.m_uri},
        .position = m_doc.getPosition(m_offset),
    });

    if (rfl::holds_alternative<std::vector<lsp::CompletionItem>>(ret)) {
        auto res = rfl::get<std::vector<lsp::CompletionItem>>(ret);
        std::vector<CompletionHandle> handles;
        handles.reserve(res.size());
        for (auto& item : res) {
            handles.emplace_back(*this, item);
        }
        return handles;
    }
    else if (rfl::holds_alternative<lsp::CompletionList>(ret)) {
        SLANG_ASSERT(false && "CompletionList not supported in this context");
        return {};
    }
    else {
        return {};
    }
}

std::vector<lsp::CompletionItem> Cursor::getResolvedCompletions(
    std::optional<std::string> triggerChar) {
    auto completions = getCompletions(triggerChar);
    std::vector<lsp::CompletionItem> resolvedItems;
    resolvedItems.reserve(completions.size());

    for (auto& completion : completions) {
        completion.resolve();
        resolvedItems.push_back(completion.m_item);
    }

    return resolvedItems;
}

bool Cursor::hasDefinition() {
    auto defs = getDefinitions();
    return !defs.empty();
}

std::vector<lsp::LocationLink> Cursor::getDefinitions() {
    lsp::DefinitionParams params{.textDocument = {.uri = m_doc.m_uri},
                                 .position = m_doc.getPosition(m_offset)};
    auto res = m_doc.m_server.getDocDefinition(params);

    if (rfl::holds_alternative<std::monostate>(res)) {
        return {};
    }

    if (rfl::holds_alternative<std::vector<lsp::DefinitionLink>>(res)) {
        return rfl::get<std::vector<lsp::DefinitionLink>>(res);
    }

    if (rfl::holds_alternative<lsp::Definition>(res)) {
        auto def = rfl::get<lsp::Definition>(res);
        std::vector<lsp::LocationLink> result;

        if (rfl::holds_alternative<lsp::Location>(def)) {
            auto loc = rfl::get<lsp::Location>(def);
            result.push_back(lsp::LocationLink{.originSelectionRange = std::nullopt,
                                               .targetUri = loc.uri,
                                               .targetRange = loc.range,
                                               .targetSelectionRange = loc.range});
        }
        else if (rfl::holds_alternative<std::vector<lsp::Location>>(def)) {
            auto locs = rfl::get<std::vector<lsp::Location>>(def);
            for (const auto& loc : locs) {
                result.push_back(lsp::LocationLink{.originSelectionRange = std::nullopt,
                                                   .targetUri = loc.uri,
                                                   .targetRange = loc.range,
                                                   .targetSelectionRange = loc.range});
            }
        }
        return result;
    }

    return {};
}

std::optional<slang::SourceLocation> DocumentHandle::getLocation(lsp::uint offset) {
    return doc->getSourceManager().getSourceLocation(doc->getBuffer(), offset);
}

std::optional<lsp::Position> DocumentHandle::getLspLocation(lsp::uint offset) {
    auto loc = getLocation(offset);
    if (!loc)
        return std::nullopt;
    auto line = m_server.sourceManager().getLineNumber(*loc);
    auto col = m_server.sourceManager().getColumnNumber(*loc);
    return lsp::Position{static_cast<lsp::uint>(line - 1), static_cast<lsp::uint>(col - 1)};
}

std::optional<server::DefinitionInfo> DocumentHandle::getDefinitionInfoAt(lsp::uint offset) {
    auto loc = getLspLocation(offset);
    if (!loc) {
        return std::nullopt;
    }
    return doc->getAnalysis().getDefinitionInfoAtPosition(*loc);
}
