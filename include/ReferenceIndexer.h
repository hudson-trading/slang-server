//------------------------------------------------------------------------------
//! @file ReferenceIndexer.h
//! @brief Variable reference index
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <type_traits>
#include <unordered_map>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

template<typename T, typename... Bases>
struct is_base_of_any : std::false_type {};

template<typename T, typename FirstBase, typename... Bases>
struct is_base_of_any<T, FirstBase, Bases...>
    : std::conditional_t<std::is_base_of_v<FirstBase, T>, std::true_type,
                         is_base_of_any<T, Bases...>> {};

struct ReferenceIndexer : public slang::ast::ASTVisitor<ReferenceIndexer, true, true> {
private:
    const slang::ast::Symbol* currentUse = nullptr;

public:
    template<typename T>
        requires std::is_base_of_v<slang::ast::ValueExpressionBase, T>
    void handle(const T& symbol) {
        if (currentUse) {
            const slang::ast::ValueSymbol* instantiatedSymbol = &(symbol.symbol);
            if (const auto modport = instantiatedSymbol->as_if<slang::ast::ModportPortSymbol>()) {
                instantiatedSymbol = modport->internalSymbol->as_if<slang::ast::ValueSymbol>();
            }
            if (instantiatedSymbol) {
                symbolToUses[instantiatedSymbol].insert(currentUse);
            }
        }
        visitDefault(symbol);
    }

    template<typename T>
    // NOCOMMIT -- port maps, other stuff?
        requires is_base_of_any<T, slang::ast::ContinuousAssignSymbol,
                                slang::ast::ProceduralBlockSymbol>::value
    void handle(const T& symbol) {
        SLANG_ASSERT(!currentUse);
        currentUse = &symbol;
        visitDefault(symbol);
        currentUse = nullptr;
    }

    void handle(const slang::ast::InstanceSymbol& symbol) {
        currentUse = &symbol;
        for (const auto connection : symbol.getPortConnections()) {
            const auto port = connection->port.as_if<slang::ast::PortSymbol>();
            if (port) {
                auto value = port->internalSymbol->as_if<slang::ast::ValueSymbol>();
                if (value) {
                    symbolToUses[value].insert(currentUse);
                }
                auto expr = connection->getExpression();
                if (expr) {
                    expr->visit(*this);
                }
            }
        }
        currentUse = nullptr;
        visitDefault(symbol);
    }

    void reset(const slang::ast::Symbol* root) {
        symbolToUses.clear();
        root->visit(*this);
    }

    std::unordered_map<const slang::ast::ValueSymbol*, std::set<const slang::ast::Symbol*>>
        symbolToUses;
};
