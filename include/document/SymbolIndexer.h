//------------------------------------------------------------------------------
// SymbolIndexer.h
// Symbol indexer for AST visitors
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/text/SourceLocation.h"

namespace server {

using Symdex = std::unordered_map<const slang::parsing::Token*, const slang::ast::Symbol*>;
using Syntex = std::unordered_map<const slang::syntax::SyntaxNode*, const slang::ast::Symbol*>;

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

    // These are not in the buffer, but should be visited
    void handle(const slang::ast::RootSymbol& sym);
    void handle(const slang::ast::CompilationUnitSymbol& sym);

    // Instance-like symbols

    void handle(const slang::ast::PackageSymbol& sym) {
        // For packages, only recurse if it's in our buffer
        indexSymbolName(sym);
        if (sym.location.buffer() == m_buffer) {
            visitDefault(sym);
        }
    }
    void handle(const slang::ast::InstanceSymbol& sym);
    void handle(const slang::ast::InstanceArraySymbol& sym);

    void handle(const slang::ast::GenerateBlockSymbol& sym) {
        if (!sym.isUnnamed) {
            indexSymbolName(sym);
        }
        visitDefault(sym);
    }

    // Types
    void handle(const slang::ast::EnumType& sym) {
        // Enum types' syntax doesn't include the name
        visitDefault(sym);
    }
    void handle(const slang::ast::TypeParameterSymbol& sym) { sym.getTypeAlias().visit(*this); }
    void handle(const slang::ast::TypeAliasType& sym);
    // Anonymous types (no typedef)
    void handle(const slang::ast::TransparentMemberSymbol& sym) { sym.wrapped.visit(*this); }
    // Special case for enum values, since name may not map
    void handle(const slang::ast::EnumValueSymbol& sym);

    /// Generic symbol handler
    template<typename T>
        requires std::is_base_of_v<slang::ast::Symbol, T>
    void handle(const T& astNode) {
        indexSymbolName(astNode);
        if (astNode.location.buffer() == m_buffer) {
            visitDefault(astNode);
        }
    }

private:
    static const uint32_t MAX_INSTANCE_DEPTH = 8;
    /// Helper to index instance syntax (shared by InstanceSymbol and InstanceArraySymbol)
    void indexInstanceSyntax(const slang::syntax::HierarchicalInstanceSyntax& instSyntax,
                             const slang::ast::InstanceBodySymbol& instanceSymbol,
                             const slang::ast::DefinitionSymbol& definition);

    void indexSymbolName(const slang::ast::Symbol& symbol);
};

} // namespace server
