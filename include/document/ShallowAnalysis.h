//------------------------------------------------------------------------------
// ShallowAnalysis.h
// Document analysis class for syntax and symbol analysis
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include "document/SymbolIndexer.h"
#include "document/SymbolTreeVisitor.h"
#include "document/SyntaxIndexer.h"
#include "lsp/LspTypes.h"
#include "util/Markdown.h"
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "slang/analysis/AnalysisOptions.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/Symbol.h"
#include "slang/diagnostics/Diagnostics.h"
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
    // The symbol this token refers to (if any)
    const slang::ast::Symbol* symbol;

    bool operator==(const DefinitionInfo& other) const {
        return node == other.node && nameToken.location() == other.nameToken.location() &&
               macroUsageRange == other.macroUsageRange && symbol == other.symbol;
    }

    bool operator!=(const DefinitionInfo& other) const { return !(*this == other); }
};

class DocumentHandle;
class ShallowAnalysis {
public:
    /// @brief Constructs a DocumentAnalysis instance with syntax and symbol indexing
    ///
    /// An instance is created on every document open and change.
    /// It's designed to provide index structures for performing lookups, and all the data that's
    /// immediately queried by the client following an open or change.
    /// @param sourceManager Reference to the source manager for file operations
    /// @param buffer The source buffer containing the document to analyze
    /// @param tree The syntax tree for the document
    /// @param options Compilation options bag for semantic analysis
    /// @param trees Additional syntax trees that this document depends on
    ShallowAnalysis(SourceManager& sourceManager, slang::BufferID buffer,
                    std::shared_ptr<slang::syntax::SyntaxTree> tree, slang::Bag options,
                    const std::vector<std::shared_ptr<slang::syntax::SyntaxTree>>& allTrees = {});

    /// @brief Retrieves document symbols for LSP outline view, called right after open
    /// @return Vector of LSP document symbols representing the document structure
    std::vector<lsp::DocumentSymbol> getDocSymbols();

    /// @brief Gets document links for include directives, called right after open
    /// @return Vector of LSP document links to included files
    std::vector<lsp::DocumentLink> getDocLinks() const;

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
        return syntaxes.getWordTokenAt(loc);
    }

    // @brief Gets the AST symbol at a specific source location
    // @param loc The source location to query
    // @return *Pointer* to the referenced symbol, or nullptr if not found
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

    /// @brief Gets the source manager for this analysis
    SourceManager& getSourceManager() const { return m_sourceManager; }

    /// @brief Generates debug hover information for a syntax node, traversing up the parent
    /// syntax pointers
    markup::Paragraph getDebugHover(const SourceLocation& loc) const;

    /// @brief Gets the AST symbol that a declared token refers to, if any
    const slang::ast::Symbol* getSymbolAtToken(const slang::parsing::Token* node) const;

    /// Syntax finder for location->syntax mapping
    SyntaxIndexer syntaxes;

    /// Map from macro name to macro definition
    slang::flat_hash_map<std::string_view, const slang::syntax::DefineDirectiveSyntax*> macros;

    friend class DocumentHandle;
    friend class InlayHintCollector;

    /// @brief Gets the appropriate scope from a symbol for member access traversal
    /// @param symbol The symbol to get the scope from
    /// @return Pointer to the scope, or nullptr if the symbol doesn't have an accessible scope
    static const slang::ast::Scope* getScopeFromSym(const slang::ast::Symbol* symbol);

    std::vector<lsp::InlayHint> getInlayHints(lsp::Range range,
                                              const struct Config::InlayHints& config);

    /// @brief Finds all references to a symbol in this document and adds them to the vector
    /// @param references Vector to append references to
    /// @param targetLocation The source location of the target symbol
    /// @param targetName The name of the symbol to match
    void addLocalReferences(std::vector<lsp::Location>& references,
                            slang::SourceLocation targetLocation,
                            std::string_view targetName) const;

    /// @brief Runs analysis on the shallow compilation and returns diagnostics
    /// @return The analysis diagnostics (owned by internal AnalysisManager)
    Diagnostics getAnalysisDiags();

private:
    /// Reference to the source manager. Not const because we may need to parse macro args.
    SourceManager& m_sourceManager;

    /// Buffer ID for this document
    slang::BufferID m_buffer;

    /// The syntax tree being analyzed
    std::shared_ptr<slang::syntax::SyntaxTree> m_tree;

    /// All syntax trees needed for the shallow compilation
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> m_allTrees;

    /// Compilation context for symbol resolution
    std::unique_ptr<slang::ast::Compilation> m_compilation;

    /// Analysis options for driver analysis (numThreads=1 to avoid persistent threads)
    slang::analysis::AnalysisOptions m_analysisOptions;

    /// Symbol tree visitor for /documentSymbols
    /// Currently this is relies on syntax, but we should switch it to use the shallow compilation
    /// when symbols exist
    SymbolTreeVisitor m_symbolTreeVisitor;

    /// Symbol indexer for syntax->symbol mappings of definitions; Used for lookups
    SymbolIndexer m_symbolIndexer;

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
