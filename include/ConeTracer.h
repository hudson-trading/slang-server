//------------------------------------------------------------------------------
//! @file ConeTracer.h
//! @brief Cone tracer
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <set>
#include <string_view>
#include <variant>

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

class ConeLeaf {
public:
    template<typename T>
    ConeLeaf(T&& value) : variant(std::forward<T>(value)) {}

    std::string getHierarchicalPath() const {
        const slang::ast::Symbol* symbol = nullptr;

        if (const slang::ast::PortSymbol* const* port = std::get_if<const slang::ast::PortSymbol*>(
                &variant)) {
            symbol = (*port)->internalSymbol;
        }
        else if (const slang::ast::ValueExpressionBase* const* expr =
                     std::get_if<const slang::ast::ValueExpressionBase*>(&variant)) {
            symbol = concreteSymbol(&(*expr)->symbol);
        }
        else {
            SLANG_UNREACHABLE;
        }

        SLANG_ASSERT(symbol);
        return symbol->getHierarchicalPath();
    }

    slang::SourceRange getSourceRange() const {
        if (const slang::ast::PortSymbol* const* port = std::get_if<const slang::ast::PortSymbol*>(
                &variant)) {
            auto startLoc = (*port)->internalSymbol->location;
            slang::SourceLocation endLoc(
                startLoc.buffer(), startLoc.offset() + (*port)->internalSymbol->name.length());
            return slang::SourceRange(startLoc, endLoc);
        }
        else if (const slang::ast::ValueExpressionBase* const* expr =
                     std::get_if<const slang::ast::ValueExpressionBase*>(&variant)) {
            return (*expr)->sourceRange;
        }
        else {
            SLANG_UNREACHABLE;
        }
    }

    bool operator<(const ConeLeaf& other) const { return variant < other.variant; }

    static const slang::ast::Symbol* concreteSymbol(const slang::ast::Symbol* symbol) {
        if (const auto modport = symbol->as_if<slang::ast::ModportPortSymbol>()) {
            return modport->internalSymbol;
        }
        return symbol;
    }

private:
    const std::variant<const slang::ast::PortSymbol*, const slang::ast::ValueExpressionBase*>
        variant;
};

template<typename TDerived>
struct ConeTracer : public slang::ast::ASTVisitor<TDerived, true, true> {
protected:
    const slang::ast::Symbol* root;

    std::set<ConeLeaf> leaves;

public:
    ConeTracer(const slang::ast::Symbol* root) : root(ConeLeaf::concreteSymbol(root)) {}

    std::set<ConeLeaf> getLeaves() { return leaves; }
};

struct DriversTracer : public ConeTracer<DriversTracer> {
private:
    std::set<ConeLeaf> drivers;

    bool isLhs = false;
    bool isDriven = false;
    bool inCondition = false;

    const slang::ast::PortSymbol* portSymbol = nullptr;

public:
    void handle(const slang::ast::ValueExpressionBase& symbol) {
        if (isLhs && ConeLeaf::concreteSymbol(&symbol.symbol) == root) {
            isDriven = true;
        }
        else if (!isLhs && isDriven) {
            leaves.insert(&symbol);
        }
        if (inCondition) {
            drivers.insert(&symbol);
        }
    }

    void handle(const slang::ast::AssignmentExpression& expr) {
        isLhs = true;
        expr.left().visit(*this);
        isLhs = false;
        if (isDriven) {
            expr.right().visit(*this);
            if (portSymbol) {
                leaves.insert(portSymbol);
            }
            for (const auto driver : drivers) {
                leaves.insert(driver);
            }
        }
        isDriven = false;
    }

    void handle(const slang::ast::ConditionalStatement& stmt) {
        auto oldDrivers = drivers;
        inCondition = true;
        for (const auto condition : stmt.conditions) {
            condition.expr->visit(*this);
        }
        inCondition = false;
        stmt.ifTrue.visit(*this);
        if (stmt.ifFalse) {
            stmt.ifFalse->visit(*this);
        }
        std::swap(drivers, oldDrivers);
    }

    void handle(const slang::ast::CaseStatement& stmt) {
        auto oldDrivers = drivers;
        inCondition = true;
        stmt.expr.visit(*this);
        inCondition = false;
        for (const auto item : stmt.items) {
            inCondition = true;
            for (const auto expr : item.expressions) {
                expr->visit(*this);
            }
            inCondition = false;
            item.stmt->visit(*this);
        }
        if (stmt.defaultCase) {
            stmt.defaultCase->visit(*this);
        }
        std::swap(drivers, oldDrivers);
    }

    void handle(const slang::ast::InstanceSymbol& symbol) {
        for (auto const connection : symbol.getPortConnections()) {
            // TODO -- interfaces, etc.
            const auto port = connection->port.as_if<slang::ast::PortSymbol>();
            if (port) {
                if (port->direction == slang::ast::ArgumentDirection::In &&
                    port->internalSymbol == root) {
                    isDriven = true;
                    connection->getExpression()->visit(*this);
                    isDriven = false;
                }
                else if (port->direction == slang::ast::ArgumentDirection::Out) {
                    portSymbol = port;
                    connection->getExpression()->visit(*this);
                    portSymbol = nullptr;
                }
            }
        }
    }

    DriversTracer(const slang::ast::Symbol* root) : ConeTracer(root) {}
};

struct LoadsTracer : public ConeTracer<LoadsTracer> {
private:
    bool isLhs = false;
    bool foundRoot = false;

public:
    void handle(const slang::ast::ValueExpressionBase& symbol) {
        if (isLhs) {
            leaves.insert(&symbol);
        }
        else if (ConeLeaf::concreteSymbol(&symbol.symbol) == root) {
            foundRoot = true;
        }
    }

    void handle(const slang::ast::AssignmentExpression& expr) {
        bool oldFoundRoot = foundRoot;
        if (!foundRoot) {
            expr.right().visit(*this);
        }
        if (foundRoot) {
            isLhs = true;
            expr.left().visit(*this);
            isLhs = false;
        }
        foundRoot = oldFoundRoot;
    }

    void handle(const slang::ast::ConditionalStatement& stmt) {
        bool oldFoundRoot = foundRoot;
        for (const auto condition : stmt.conditions) {
            condition.expr->visit(*this);
        }
        stmt.ifTrue.visit(*this);
        if (stmt.ifFalse) {
            stmt.ifFalse->visit(*this);
        }
        foundRoot = oldFoundRoot;
    }

    void handle(const slang::ast::InstanceSymbol& symbol) {
        for (auto const connection : symbol.getPortConnections()) {
            // TODO -- interfaces, etc.
            const auto port = connection->port.as_if<slang::ast::PortSymbol>();
            if (port) {
                if (port->direction == slang::ast::ArgumentDirection::Out &&
                    port->internalSymbol == root) {
                    auto oldFoundRoot = std::exchange(foundRoot, true);
                    connection->getExpression()->visit(*this);
                    foundRoot = oldFoundRoot;
                }
                else if (port->direction == slang::ast::ArgumentDirection::In) {
                    connection->getExpression()->visit(*this);
                    if (foundRoot) {
                        leaves.insert(port);
                    }
                    foundRoot = false;
                }
            }
        }
    }

    LoadsTracer(const slang::ast::Symbol* root) : ConeTracer(root) {}
};
