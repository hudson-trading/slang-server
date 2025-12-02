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
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
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

    void handle(const slang::ast::InstanceArraySymbol& sym);

    // Called via generic handle()
    void handleSym(const slang::ast::InstanceSymbol& sym);
    void handleSym(const slang::ast::ValueSymbol& sym);
    void handleSym(const slang::ast::TypeAliasType& value);

    // These are not in the buffer, but should be visited
    void handle(const slang::ast::RootSymbol& sym);
    void handle(const slang::ast::CompilationUnitSymbol& sym);

    void handle(const slang::ast::TypeParameterSymbol& sym) { sym.getTypeAlias().visit(*this); }

    // Index for inlay hints
    void handle(const slang::ast::CallExpression& sym);

    // Anonymous types (no typedef)
    void handle(const slang::ast::TransparentMemberSymbol& sym) { sym.wrapped.visit(*this); }

    // Special case for enum values, since name may not map
    void handle(const slang::ast::EnumValueSymbol& sym);

    /// Generic symbol handler with dispatch to specialized handlers
    template<typename T>
        requires std::is_base_of_v<slang::ast::Symbol, T>
    void handle(const T& astNode) {
        if (astNode.getSyntax()) {
            syntex[astNode.getSyntax()] = &astNode;
        }

        if constexpr (std::is_same_v<slang::ast::InstanceSymbol, T>) {
            handleSym(static_cast<const slang::ast::InstanceSymbol&>(astNode));
        }
        else if constexpr (std::is_base_of_v<slang::ast::ValueSymbol, T>) {
            handleSym(static_cast<const slang::ast::ValueSymbol&>(astNode));
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

        // Recurse for symbols other than top level symbols
        if constexpr (std::is_same_v<slang::ast::PackageSymbol, T>) {
            if (astNode.location.buffer() != m_buffer) {
                return;
            }
        }

        // unwrap typedefs (structs, enums, unions)
        if constexpr (std::is_same_v<slang::ast::TypeAliasType, T>) {
            handleSym(static_cast<const slang::ast::TypeAliasType&>(astNode));
        }
        else {
            visitDefault(astNode);
        }
    }

private:
    /// Helper to index instance syntax (shared by InstanceSymbol and InstanceArraySymbol)
    void indexInstanceSyntax(const slang::syntax::HierarchicalInstanceSyntax& instSyntax,
                             const slang::ast::InstanceBodySymbol& instanceSymbol,
                             const slang::ast::DefinitionSymbol& definition);
};

} // namespace server
