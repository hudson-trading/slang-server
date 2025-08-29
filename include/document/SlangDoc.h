//------------------------------------------------------------------------------
// SlangDoc.h
// Document container for managing SystemVerilog syntax trees and analysis
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include <memory>
#include <optional>
#include <sys/types.h>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"
namespace server {
/// Container around an open document, syntax tree, and shallow analysis. Isn't aware of broader
/// compilation context.

using namespace slang;

class DocumentHandle;
class SlangDoc {
private:
    /// Reference to the source manager
    slang::SourceManager& m_sourceManager;

    /// Options bag for compilation and analysis
    slang::Bag m_options;

    /// The URI of the document
    URI m_uri;

    /// The buffer of the actual source text (no expansions)
    slang::BufferID m_buffer;

    /// The syntax tree for this document
    std::shared_ptr<slang::syntax::SyntaxTree> m_tree;

    /// List of weak pointers to documents that this one depends on. Owned by
    std::vector<std::shared_ptr<SlangDoc>> m_dependentDocuments;

    /// Document analysis for syntax and symbol analysis
    std::unique_ptr<ShallowAnalysis> m_analysis;

    // For testing
    friend class DocumentHandle;

public:
    SlangDoc(URI uri, SourceManager& SourceManager, slang::Bag options,
             std::shared_ptr<slang::syntax::SyntaxTree> tree);

    SlangDoc(URI uri, SourceManager& SourceManager, slang::Bag options, std::string_view text);

    SlangDoc(URI uri, SourceManager& SourceManager, slang::Bag options, slang::BufferID buffer);

    // Open a Document from a syntax tree (parsed from slang Driver)
    static std::shared_ptr<SlangDoc> fromTree(std::shared_ptr<slang::syntax::SyntaxTree> tree,
                                              SourceManager& SourceManager,
                                              const slang::Bag& options = {});

    // Open a Document from text (LSP open)
    static std::shared_ptr<SlangDoc> fromText(const URI& uri, SourceManager& SourceManager,
                                              const slang::Bag& options, std::string_view text);

    // Open a Document from file
    static std::shared_ptr<SlangDoc> open(const URI& uri, SourceManager& SourceManager,
                                          const slang::Bag& options);

    SourceManager& getSourceManager() const { return m_sourceManager; }
    const slang::BufferID getBuffer() const { return m_buffer; }
    const std::string_view getText() const;
    const URI& getURI() { return m_uri; }
    std::string_view getPath() const { return m_uri.getPath(); }

    /// @brief Get the syntax tree, creating it if necessary
    std::shared_ptr<slang::syntax::SyntaxTree> getSyntaxTree();

    /// @brief Get the analysis, creating it if necessary
    ShallowAnalysis& getAnalysis();

    ////////////////////////////////////////////////
    /// Indexed Syntax Tree Methods
    ////////////////////////////////////////////////

    /// Methods dependent on the indexed syntax tree
    const slang::parsing::Token* getTokenAt(slang::SourceLocation loc) {
        return getAnalysis().getTokenAt(loc);
    }

    const slang::parsing::Token* getWordTokenAt(slang::SourceLocation loc) {
        return getAnalysis().getWordTokenAt(loc);
    }

    ////////////////////////////////////////////////
    /// Shallow Compilation Methods
    ////////////////////////////////////////////////

    const std::unique_ptr<slang::ast::Compilation>& getCompilation() {
        return getAnalysis().getCompilation();
    }

    /// Return the scope at this location, if any. Does not return the root scope.
    const slang::ast::Scope* getScopeAt(slang::SourceLocation loc) {
        return getAnalysis().getScopeAt(loc);
    }
    ////////////////////////////////////////////////
    /// File Lifecycle
    ////////////////////////////////////////////////
    /// @brief Set dependent documents for this document, updated by driver after document changes
    void setDependentDocuments(const std::vector<std::shared_ptr<SlangDoc>>& dependentDocs) {
        m_dependentDocuments = dependentDocs;
    }

    void onChange(const std::vector<lsp::TextDocumentContentChangeEvent>& contentChanges);

    bool textMatches(std::string_view text);
    ////////////////////////////////////////////////
    /// Lsp Functions
    ////////////////////////////////////////////////
    /// @brief Issue all diagnostics from this document to the given diagnostic engine
    void issueDiagnosticsTo(slang::DiagnosticEngine& diagEngine);

    /// @brief For the document symbols request
    // TODO: should this use the shallow compilation instead of syntax tree?
    std::vector<lsp::DocumentSymbol> getSymbols() { return getAnalysis().getDocSymbols(); }

    std::optional<slang::SourceLocation> getLocation(const lsp::Position& position) {
        return m_sourceManager.getSourceLocation(m_buffer, position.line, position.character);
    }

    // Previous text on and before a position
    std::string getPrevText(const lsp::Position& position);

    std::vector<lsp::LocationLink> getDocDefinition(const lsp::Position& position) {
        return getAnalysis().getDocDefinition(position);
    }

    std::vector<lsp::DocumentLink> getDocLinks() { return getAnalysis().getDocLinks(); }

    std::optional<lsp::Hover> getDocHover(const lsp::Position& position) {
        return getAnalysis().getDocHover(position);
    }
};

} // namespace server
template<>
struct fmt::formatter<server::SlangDoc> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    constexpr auto format(const server::SlangDoc& doc, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", doc.getPath());
    }
};
