//------------------------------------------------------------------------------
// ShallowAnalysis.h
// Document analysis class for syntax and symbol analysis
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include "ast/ShallowCompilation.h"
#include "document/SymbolIndexer.h"
#include "document/SymbolTreeVisitor.h"
#include "document/SyntaxIndexer.h"
#include "lsp/LspTypes.h"
#include "util/Markdown.h"
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "slang/analysis/AnalysisManager.h"
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
namespace server {
using namespace slang;

class DocumentHandle;

/// Indexes and queries one source buffer within a shared shallow compilation.
class ShallowAnalysis {
public:
    /// Index the selected source buffer, retaining the compilation that owns its syntax and
    /// symbols.
    ShallowAnalysis(slang::BufferID buffer, std::shared_ptr<ShallowCompilation> compilation);

    /// @brief Retrieves document symbols for LSP outline view, called right after open
    /// @return Tree of LSP document symbols representing the document CST structure
    /// This also provides range info for "sticky scroll", where the open scopes stick at the top of
    /// the editor to provide context
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

    /// @brief Gets a definition symbol by name using the compilation's root scope
    /// @param name The name of the definition to look up
    /// @return Pointer to the symbol, or nullptr if not found
    const slang::ast::Symbol* getDefinition(std::string_view name) const;

    /// @brief Gets module declarations in this document
    /// @return Vector of module declaration syntax nodes
    std::vector<const slang::syntax::ModuleDeclarationSyntax*> getModules() const;

    /// Whether the shared compilation still uses the latest source buffers.
    bool hasValidBuffers() const { return m_shallowCompilation->hasValidBuffers(); }

    /// Shared owner of the syntax, symbols, and compilation-wide analysis state.
    const std::shared_ptr<ShallowCompilation>& getShallowCompilation() const {
        return m_shallowCompilation;
    }

    /// Slang compilation used for symbol lookup.
    const std::unique_ptr<slang::ast::Compilation>& getCompilation() const {
        return m_shallowCompilation->getCompilation();
    }

    /// @brief Gets semantic diagnostics after shallowly elaborating edited-file definitions.
    const Diagnostics& getSemanticDiagnostics() {
        return m_shallowCompilation->getSemanticDiagnostics();
    }

    /// @brief Ensures the shallow compilation has been analyzed and returns the slang
    /// `AnalysisManager`. Returns nullptr if analysis could not be run, for example no top
    /// instances.
    const slang::analysis::AnalysisManager* getAnalysisManager() {
        return m_shallowCompilation->getAnalysisManager();
    }

    /// @brief Gets a list of drivers for a given value symbol
    std::vector<const slang::analysis::ValueDriver*> getDrivers(
        const slang::ast::ValueSymbol& symbol) {
        return m_shallowCompilation->getDrivers(symbol);
    }

    /// @brief Return the corresponding symbol from the selected full-design instance, if any.
    const slang::ast::Symbol* getDesignSymbol(const slang::ast::Symbol& shallowSymbol) const {
        return m_shallowCompilation->getDesignSymbol(shallowSymbol);
    }

    /// Return the full-design instance referenced by a module, named parameter, or named port
    /// token at an instantiation site.
    std::optional<std::string> getDesignInstancePathAtToken(
        const slang::parsing::Token* token) const;

    /// Return the selected full-design connection for an interface port, if available.
    const InterfaceConnection* getActiveInterfaceConnection(
        const slang::ast::InterfacePortSymbol& port) const {
        return m_shallowCompilation->getActiveInterfaceConnection(port);
    }

    /// @brief Gets the source manager for this analysis
    SourceManager& getSourceManager() const { return m_sourceManager; }

    /// @brief Generates debug hover information for a syntax node, traversing up the parent
    /// syntax pointers
    markup::Paragraph getDebugHover(const SourceLocation& loc) const;

    /// @brief Gets the AST symbol that a declared token refers to, if any
    const slang::ast::Symbol* getSymbolAtToken(const slang::parsing::Token* node) const;

    /// @brief Gets all AST symbols that a token refers to. A source token can denote multiple
    /// elaborated symbols, or both sides of an implicit connection.
    slang::SmallVector<const slang::ast::Symbol*, 2> getSymbolsAtToken(
        const slang::parsing::Token* node) const;

    /// @brief Gets the implicit per-iteration parameters for a loop-header genvar token.
    std::span<const slang::ast::ParameterSymbol* const> getGenvarIterationParametersAtToken(
        const slang::parsing::Token* node) const;

    /// Syntax finder for location->syntax mapping
    SyntaxIndexer syntaxes;

    /// Map from macro name to macro definition (last active definition)
    slang::flat_hash_map<std::string_view, const slang::syntax::DefineDirectiveSyntax*> macros;

    /// Map from macro usage syntax to the definition that was active at expansion time
    slang::flat_hash_map<const slang::syntax::SyntaxNode*,
                         const slang::syntax::DefineDirectiveSyntax*>
        macroUsageDefinitions;

    friend class DocumentHandle;
    friend class InlayHintCollector;

    /// @brief Gets the appropriate scope from a symbol for member access traversal
    /// @param symbol The symbol to get the scope from
    /// @return Pointer to the scope, or nullptr if the symbol doesn't have an accessible scope
    const slang::ast::Scope* getScopeFromSym(const slang::ast::Symbol* symbol) const;

    /// Gets the target struct scope when the location is an assignment pattern key.
    const slang::ast::Scope* getAssignmentPatternKeyScope(slang::SourceLocation loc) const;

    /// Gets the target struct scope at a possible assignment pattern key completion site.
    const slang::ast::Scope* getAssignmentPatternCompletionScope(slang::SourceLocation loc) const;

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
    Diagnostics getAnalysisDiags() { return m_shallowCompilation->getAnalysisDiags(); }

private:
    /// Reference to the source manager. Not const because we may need to parse macro args.
    SourceManager& m_sourceManager;

    /// Buffer ID for this document
    slang::BufferID m_buffer;

    /// Keeps the syntax and symbols referenced by this file's indexes alive.
    std::shared_ptr<ShallowCompilation> m_shallowCompilation;

    /// Symbol tree visitor for /documentSymbols
    /// Currently this is relies on syntax, but we should switch it to use the shallow compilation
    /// when symbols exist
    SymbolTreeVisitor m_symbolTreeVisitor;

    /// Symbol indexer for syntax->symbol mappings of definitions; Used for lookups
    SymbolIndexer m_symbolIndexer;

    struct GenvarElaboration {
        const slang::ast::GenvarSymbol* source = nullptr;
        slang::SmallVector<const slang::ast::ParameterSymbol*> parameters;
        bool initialized = false;
    };
    mutable slang::flat_hash_map<const slang::syntax::LoopGenerateSyntax*, GenvarElaboration>
        m_genvarElaborations;

    mutable slang::flat_hash_map<const slang::ast::DefinitionSymbol*, const slang::ast::Scope*>
        m_interfaceFallbackScopes;

    const GenvarElaboration* getGenvarElaborationAtToken(const slang::parsing::Token* node) const;

    const slang::ast::Symbol* getAssignmentPatternTargetSymbol(
        const slang::syntax::AssignmentPatternExpressionSyntax& pattern) const;

    const slang::ast::Scope* getAssignmentPatternTargetScope(
        const slang::syntax::AssignmentPatternExpressionSyntax& pattern) const;

    const slang::ast::Scope* getAssignmentPatternScopeAt(slang::SourceLocation loc,
                                                         bool allowIncompleteKey) const;

    /// @brief Helper method to check if a token is positioned over a selector
    bool isOverSelector(const slang::parsing::Token* node,
                        const slang::ast::LookupResult& result) const;

    /// @brief Helper method to handle lookup for scoped names (e.g., pkg::identifier)
    /// @param nameSyntax The name syntax to look up
    /// @param context The AST context for the lookup
    /// @return Pointer to the found symbol, or nullptr if not found
    const slang::ast::Symbol* handleScopedNameLookup(const slang::syntax::NameSyntax* nameSyntax,
                                                     const slang::ast::ASTContext& context) const;

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
