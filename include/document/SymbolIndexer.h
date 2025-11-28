//------------------------------------------------------------------------------
// SymbolIndexer.h
// Symbol indexer for AST visitors
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/text/SourceLocation.h"

namespace server {

using Symdex = std::unordered_map<const slang::parsing::Token*, const slang::ast::Symbol*>;
using Syntex = std::unordered_map<const slang::syntax::SyntaxNode*, const slang::ast::Symbol*>;

/// Find the name token in a syntax node, 1 layer deep.
const slang::parsing::Token* findName(std::string_view name, const slang::syntax::SyntaxNode& node);

struct SymbolIndexer : public slang::ast::ASTVisitor<SymbolIndexer, false, false, true> {
public:
    /// Token -> Symbol mapping
    Symdex symdex;

    /// Syntax -> Symbol mapping
    Syntex syntex;

    slang::BufferID m_buffer;

    SymbolIndexer(slang::BufferID buffer);

    const slang::ast::Symbol* getSymbol(const slang::parsing::Token* node) const;

    const slang::ast::Symbol* getSymbol(const slang::syntax::SyntaxNode* node) const;

    const slang::ast::Scope* getScopeForSyntax(const slang::syntax::SyntaxNode& syntax) const;

    /// Module instances- module name, parameters, ports
    void handle(const slang::ast::InstanceArraySymbol& sym);
    void handle(const slang::ast::InstanceSymbol& sym);

    /// Index ValueSymbol names
    void handle(const slang::ast::ValueSymbol& sym);

    // These are not in the buffer, but should be visited
    void handle(const slang::ast::RootSymbol& sym);
    void handle(const slang::ast::CompilationUnitSymbol& sym);

    // Index for inlay hints
    void handle(const slang::ast::CallExpression& sym);

    /// Generic symbol handler with dispatch to specialized handlers
    template<typename T>
        requires std::is_base_of_v<slang::ast::Symbol, T>
    void handle(const T& astNode) {
        if (astNode.getSyntax()) {
            syntex[astNode.getSyntax()] = &astNode;
        }

        if constexpr (std::is_base_of_v<slang::ast::ValueSymbol, T>) {
            handle(static_cast<const slang::ast::ValueSymbol&>(astNode));
        }

        if (astNode.getSyntax() != nullptr) {
            auto& syntax = *astNode.getSyntax();
            if (syntax.sourceRange().start().buffer() == m_buffer) {
                auto nameTok = findName(astNode.name, syntax);
                if (nameTok) {
                    symdex[nameTok] = &astNode;
                }
            }
        }

        // Index symbol name for other symbol types
        visitDefault(astNode);
    }

private:
    /// Helper to index instance syntax (shared by InstanceSymbol and InstanceArraySymbol)
    void indexInstanceSyntax(const slang::syntax::HierarchicalInstanceSyntax& instSyntax,
                             const slang::ast::InstanceBodySymbol& instanceSymbol,
                             const slang::ast::DefinitionSymbol& definition);
};

} // namespace server
