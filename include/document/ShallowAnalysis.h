//------------------------------------------------------------------------------
// ShallowAnalysis.h
// Document analysis class for syntax and symbol analysis
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "document/SymbolIndexer.h"
#include "document/SymbolTreeVisitor.h"
#include "document/SyntaxIndexer.h"
#include "lsp/LspTypes.h"
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/Symbol.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"
namespace server {
using namespace slang;
struct DefinitionInfo {
    // The syntax that the token refers to
    const slang::syntax::SyntaxNode* node;
    // The exact name id in the syntax node, or the first token in the syntax if it wasn't found
    slang::parsing::Token nameToken;
    // Optional original source range; exists if it's behind a macro expansion
    slang::SourceRange macroUsageRange;

    bool operator==(const DefinitionInfo& other) const {
        return node == other.node && nameToken.location() == other.nameToken.location() &&
               macroUsageRange == other.macroUsageRange;
    }

    bool operator!=(const DefinitionInfo& other) const { return !(*this == other); }
};

class DocumentHandle;
class ShallowAnalysis {
public:
    /// @brief Constructs a DocumentAnalysis instance with syntax and symbol indexing
    /// @param sourceManager Reference to the source manager for file operations
    /// @param buffer The source buffer containing the document to analyze
    /// @param tree The syntax tree for the main document
    /// @param options Compilation options bag for semantic analysis
    /// @param trees Additional syntax trees that this document depends on
    ShallowAnalysis(const SourceManager& sourceManager, slang::BufferID buffer,
                    std::shared_ptr<slang::syntax::SyntaxTree> tree, slang::Bag options,
                    const std::vector<std::shared_ptr<slang::syntax::SyntaxTree>>& trees = {});

    /// @brief Retrieves document symbols for LSP outline view, called right after open
    /// @return Vector of LSP document symbols representing the document structure
    std::vector<lsp::DocumentSymbol> getDocSymbols();

    /// @brief Gets document links for include directives, called right after open
    /// @return Vector of LSP document links to included files
    std::vector<lsp::DocumentLink> getDocLinks() const;

    /// @brief Gets LSP definition links for a position
    /// @param position The LSP position to query
    /// @return Vector of location links to definitions
    std::vector<lsp::LocationLink> getDocDefinition(const lsp::Position& position);

    /// @brief Gets hover information for a symbol at an LSP position
    /// @param position The LSP position to query
    /// @return Optional hover information, or nullopt if none available
    std::optional<lsp::Hover> getDocHover(const lsp::Position& position, bool noDebug = false);

    /// @brief Gets the token at a specific source location
    /// @param loc The source location to query
    /// @return Pointer to the token at the location, or nullptr if none found
    const slang::parsing::Token* getTokenAt(slang::SourceLocation loc) const;

    /// @brief Gets the word token at a specific source location
    /// @param loc The source location to query
    /// @return Pointer to the word token at the location, or nullptr if none found
    const slang::parsing::Token* getWordTokenAt(slang::SourceLocation loc) const {
        return m_syntaxes.getWordTokenAt(loc);
    }

    /// @brief Gets the AST symbol at a specific source location
    /// @param loc The source location to query
    /// @return Pointer to the referenced symbol, or nullptr if not found
    const slang::ast::Symbol* getSymbolAt(slang::SourceLocation loc) const;

    /// @brief Gets the AST scope at a specific source location
    /// @param loc The source location to query
    /// @return Pointer to the scope, or nullptr if not found
    const slang::ast::Scope* getScopeAt(slang::SourceLocation loc) const;

    /// @brief Gets module declarations in this document
    /// @return Vector of module declaration syntax nodes
    std::vector<const slang::syntax::ModuleDeclarationSyntax*> getModules() const;

    /// @brief Return true if shallow compilation has the latest buffers in all it's syntax trees
    bool hasValidBuffers();

    const std::unique_ptr<slang::ast::Compilation>& getCompilation() const { return m_compilation; }

    /// @brief Gets definition information for a symbol at an LSP position (for testing)
    /// @param position The LSP position to query
    /// @return Optional definition information
    std::optional<DefinitionInfo> getDefinitionInfoAtPosition(const lsp::Position& position) {
        return getDefinitionInfoAt(position);
    }

    friend class DocumentHandle;

    /// @brief Gets the appropriate scope from a symbol for member access traversal
    /// @param symbol The symbol to get the scope from
    /// @return Pointer to the scope, or nullptr if the symbol doesn't have an accessible scope
    static const slang::ast::Scope* getScopeFromSym(const slang::ast::Symbol* symbol);

private:
    /// Reference to the source manager
    const SourceManager& m_sourceManager;

    /// Buffer ID for this document
    slang::BufferID m_buffer;

    /// The syntax tree being analyzed
    std::shared_ptr<slang::syntax::SyntaxTree> m_tree;

    /// Dependent syntax trees for cross-file analysis
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> m_dependentTrees;

    /// Compilation context for symbol resolution
    std::unique_ptr<slang::ast::Compilation> m_compilation;

    /// Symbol tree visitor for document symbols
    SymbolTreeVisitor m_symbolTreeVisitor;

    /// Symbol indexer for syntax->symbol mapping
    SymbolIndexer m_symbolIndexer;

    /// Syntax finder for location->syntax mapping
    SyntaxIndexer m_syntaxes;

    // For testing

    /// @brief Gets definition information for a symbol at an LSP position, called by hover and goto
    std::optional<DefinitionInfo> getDefinitionInfoAt(const lsp::Position& position);

    /// @brief Gets the definition link for a definition info object
    /// @param info The definition info to get the link for
    const std::vector<lsp::LocationLink> getDefinition(const DefinitionInfo& info) const;

    /// @brief Generates debug hover information for a syntax node, traversing up the parent
    /// syntax pointers
    std::string getDebugHover(const slang::parsing::Token& node) const;

    /// @brief Gets the AST symbol that a declared token refers to, if any
    const slang::ast::Symbol* getSymbolAtToken(const slang::parsing::Token* node) const;

    /// Map from macro name to macro definition
    slang::flat_hash_map<std::string_view, const slang::syntax::DefineDirectiveSyntax*> m_macros;

    /// @brief Helper method to check if a token is positioned over a selector
    bool isOverSelector(const slang::parsing::Token* node,
                        const slang::ast::LookupResult& result) const;

    /// @brief Helper method to handle lookup for scoped names (e.g., pkg::identifier)
    /// @param nameSyntax The name syntax to look up
    /// @param context The AST context for the lookup
    /// @param scope The scope to search in
    /// @return Pointer to the found symbol, or nullptr if not found
    const slang::ast::Symbol* handleScopedNameLookup(const slang::syntax::NameSyntax* nameSyntax,
                                                     const slang::ast::ASTContext& context,
                                                     const slang::ast::Scope* scope) const;

    /// @brief Helper method to handle symbol lookup for interface port headers
    /// @param node The token being queried
    /// @param syntax The syntax node context
    /// @param scope The scope to search in
    /// @return Pointer to the found symbol, or nullptr if not found
    const slang::ast::Symbol* handleInterfacePortHeader(const slang::parsing::Token* node,
                                                        const slang::syntax::SyntaxNode* syntax,
                                                        const slang::ast::Scope* scope) const;

    /// @brief Finds the name syntax node associated with a given syntax node
    /// @param node The syntax node to search from
    /// @return Pointer to the name syntax, or nullptr if none found
    const slang::syntax::NameSyntax* findNameSyntax(const slang::syntax::SyntaxNode& node) const;
};

} // namespace server
