//------------------------------------------------------------------------------
//! @file ConeTracer.h
//! @brief Cone tracer
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "util/ScopedRestore.h"
#include <functional>
#include <set>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Expression.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Statement.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/Util.h"

using server::utils::ScopedReset;
using server::utils::ScopedRestore;

class ConeLeaf {
public:
    ConeLeaf(const slang::ast::PortSymbol* port) : symbol(port->internalSymbol) {}
    ConeLeaf(const slang::ast::ValueExpressionBase* expr) : symbol(concreteSymbol(&expr->symbol)) {}

    std::string getHierarchicalPath() const {
        SLANG_ASSERT(symbol);
        return symbol->getHierarchicalPath();
    }

    slang::SourceRange getDeclarationRange() const {
        SLANG_ASSERT(symbol);
        auto startLoc = symbol->location;
        return slang::SourceRange(startLoc, startLoc + symbol->name.length());
    }

    bool isParameter() const { return symbol->kind == slang::ast::SymbolKind::Parameter; }

    bool operator<(const ConeLeaf& other) const {
        return std::less<const slang::ast::Symbol*>{}(symbol, other.symbol);
    }

    static const slang::ast::Symbol* concreteSymbol(const slang::ast::Symbol* symbol) {
        if (const auto modport = symbol->as_if<slang::ast::ModportPortSymbol>()) {
            return modport->internalSymbol;
        }
        return symbol;
    }

private:
    const slang::ast::Symbol* symbol;
};

template<typename TDerived>
struct ConeTracer : public slang::ast::ASTVisitor<TDerived, slang::ast::VisitFlags::AllGood> {
protected:
    const slang::ast::Symbol* root;

    void addLeaf(const ConeLeaf& leaf) {
        if (!leaf.isParameter()) {
            leaves.insert(leaf);
        }
    }

private:
    ConeTracer(const slang::ast::Symbol* root) : root(ConeLeaf::concreteSymbol(root)) {}

    std::set<ConeLeaf> leaves;

public:
    std::set<ConeLeaf> getLeaves() { return leaves; }
    friend TDerived;
};

struct DriversTracer : public ConeTracer<DriversTracer> {
private:
    // Signals that control whether the current assignment executes.
    std::set<ConeLeaf> conditionDrivers;

    // Whether the current expression is an assignment target.
    bool isLhs = false;
    // Whether visited value expressions should be recorded as drivers of the root.
    bool isDriven = false;
    // Whether visited value expressions control which assignments execute.
    bool inCondition = false;

    const slang::ast::PortSymbol* portSymbol = nullptr;

public:
    void handle(const slang::ast::ValueExpressionBase& symbol) {
        if (isLhs && ConeLeaf::concreteSymbol(&symbol.symbol) == root) {
            isDriven = true;
        }
        else if (!isLhs && isDriven) {
            addLeaf(&symbol);
        }
        if (inCondition) {
            conditionDrivers.insert(&symbol);
        }
    }

    void handle(const slang::ast::AssignmentExpression& expr) {
        ScopedReset drivenScope(isDriven);
        {
            ScopedReset lhsScope(isLhs, true);
            expr.left().visit(*this);
        }
        if (isDriven) {
            expr.right().visit(*this);
            if (portSymbol) {
                addLeaf(portSymbol);
            }
            for (const auto& driver : conditionDrivers) {
                addLeaf(driver);
            }
        }
    }

    void handle(const slang::ast::ConditionalStatement& stmt) {
        ScopedRestore conditionDriversScope(conditionDrivers);
        {
            ScopedReset conditionScope(inCondition, true);
            for (const auto condition : stmt.conditions) {
                condition.expr->visit(*this);
            }
        }
        stmt.ifTrue.visit(*this);
        if (stmt.ifFalse) {
            stmt.ifFalse->visit(*this);
        }
    }

    void handle(const slang::ast::CaseStatement& stmt) {
        ScopedRestore conditionDriversScope(conditionDrivers);
        {
            ScopedReset conditionScope(inCondition, true);
            stmt.expr.visit(*this);
        }
        for (const auto item : stmt.items) {
            {
                ScopedReset conditionScope(inCondition, true);
                for (const auto expr : item.expressions) {
                    expr->visit(*this);
                }
            }
            item.stmt->visit(*this);
        }
        if (stmt.defaultCase) {
            stmt.defaultCase->visit(*this);
        }
    }

    void handle(const slang::ast::InstanceSymbol& symbol) {
        for (auto const connection : symbol.getPortConnections()) {
            // TODO -- interfaces, etc.
            const auto port = connection->port.as_if<slang::ast::PortSymbol>();
            const auto expr = connection->getExpression();
            if (!expr) {
                continue;
            }
            if (port) {
                if (port->direction == slang::ast::ArgumentDirection::In &&
                    port->internalSymbol == root) {
                    ScopedReset drivenScope(isDriven, true);
                    expr->visit(*this);
                }
                else if (port->direction == slang::ast::ArgumentDirection::Out) {
                    ScopedReset portScope(portSymbol, port);
                    expr->visit(*this);
                }
            }
        }
    }

    DriversTracer(const slang::ast::Symbol* root) : ConeTracer(root) {}
};

struct LoadsTracer : public ConeTracer<LoadsTracer> {
private:
    // Whether visited value expressions should be recorded as loads of the root.
    bool isLhs = false;
    // Whether the root was encountered on the current data or control path.
    bool foundRoot = false;

public:
    void handle(const slang::ast::ValueExpressionBase& symbol) {
        if (isLhs) {
            addLeaf(&symbol);
        }
        else if (ConeLeaf::concreteSymbol(&symbol.symbol) == root) {
            foundRoot = true;
        }
    }

    void handle(const slang::ast::AssignmentExpression& expr) {
        ScopedRestore foundRootScope(foundRoot);
        if (!foundRoot) {
            expr.right().visit(*this);
        }
        if (foundRoot) {
            ScopedReset lhsScope(isLhs, true);
            expr.left().visit(*this);
        }
    }

    void handle(const slang::ast::ConditionalStatement& stmt) {
        ScopedRestore foundRootScope(foundRoot);
        for (const auto condition : stmt.conditions) {
            condition.expr->visit(*this);
        }
        stmt.ifTrue.visit(*this);
        if (stmt.ifFalse) {
            stmt.ifFalse->visit(*this);
        }
    }

    void handle(const slang::ast::CaseStatement& stmt) {
        ScopedRestore foundRootScope(foundRoot);
        stmt.expr.visit(*this);
        bool selectorFoundRoot = foundRoot;
        for (const auto item : stmt.items) {
            foundRoot = selectorFoundRoot;
            for (const auto expr : item.expressions) {
                expr->visit(*this);
            }
            item.stmt->visit(*this);
        }
        if (stmt.defaultCase) {
            foundRoot = selectorFoundRoot;
            stmt.defaultCase->visit(*this);
        }
    }

    void handle(const slang::ast::InstanceSymbol& symbol) {
        for (auto const connection : symbol.getPortConnections()) {
            // TODO -- interfaces, etc.
            const auto port = connection->port.as_if<slang::ast::PortSymbol>();
            if (port) {
                if (port->direction == slang::ast::ArgumentDirection::Out &&
                    port->internalSymbol == root) {
                    ScopedReset foundRootScope(foundRoot, true);
                    connection->getExpression()->visit(*this);
                }
                else if (port->direction == slang::ast::ArgumentDirection::In) {
                    ScopedReset foundRootScope(foundRoot);
                    connection->getExpression()->visit(*this);
                    if (foundRoot) {
                        addLeaf(port);
                    }
                }
            }
        }
    }

    LoadsTracer(const slang::ast::Symbol* root) : ConeTracer(root) {}
};
