//------------------------------------------------------------------------------
//! @file InstanceVisitor.h
//! @brief Finds instances of variables, modules and interfaces
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <set>
#include <type_traits>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Expression.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/SourceLocation.h"

namespace inst {

// TODO -- move some place more common
template<typename T>
class Restorer {
private:
    T& originalRef;
    T backupValue;

public:
    explicit Restorer(T& original) : originalRef(original), backupValue(original) {}
    ~Restorer() { originalRef = backupValue; }

    Restorer(const Restorer&) = delete;
    Restorer& operator=(const Restorer&) = delete;
};

class InstanceVisitor : public slang::ast::ASTVisitor<InstanceVisitor, true, true> {
public:
    explicit InstanceVisitor(slang::ast::Compilation& compilation,
                             slang::SourceManager& sourceManager,
                             const slang::SourceLocation& location) :
        compilation(compilation), sourceManager(sourceManager), location(location) {}

    template<typename T>
        requires(std::is_base_of_v<slang::ast::ValueSymbol, T> ||
                 std::is_base_of_v<slang::ast::InstanceSymbol, T> ||
                 std::is_base_of_v<slang::ast::InterfacePortSymbol, T>)
    void handle(const T& symbol) {
        if (containsLocation(symbol)) {
            addPath(symbol);
        }

        visitDefault(symbol);
    }

    template<typename T>
        requires std::is_base_of_v<slang::ast::ValueExpressionBase, T>
    void handle(const T& symbol) {
        if (symbol.kind == slang::ast::ExpressionKind::HierarchicalValue) {
            const auto& hierExpr = symbol.template as<slang::ast::HierarchicalValueExpression>();
            if (symbol.sourceRange.contains(location)) {
                auto ref = hierExpr.ref;
                if (ref.isViaIfacePort()) {
                    // TODO -- move stringification into library?
                    std::string hier;
                    bool first = true;
                    for (const auto& scope : ref.path) {
                        if (first) {
                            hier = scope.symbol->getHierarchicalPath();
                            first = false;
                        }
                        else if (const auto index = std::get_if<int32_t>(&scope.selector)) {
                            hier += fmt::format("[{}]", *index);
                        }
                        else if (const auto slice = std::get_if<std::pair<int32_t, int32_t>>(
                                     &scope.selector)) {
                            hier += fmt::format("[{}:{}]", slice->first, slice->second);
                        }
                        else if (const auto name = std::get_if<std::string_view>(&scope.selector)) {
                            hier += fmt::format(".{}", *name);
                        }
                    }
                    instances.push_back(fmt::format("{}{}", hier, access));
                }
                else {
                    addPath(symbol.symbol);
                }
            }
            return;
        }

        // TODO -- possible library bug: symbol.sourceRange reports entire expression (including
        // member selects) as opposed to just the named value's source rage
        slang::SourceLocation endLocation(symbol.sourceRange.start());
        endLocation += symbol.symbol.name.length();
        slang::SourceRange range(symbol.sourceRange.start(), endLocation);
        bool contains = range.contains(location);
        if (contains) {
            access.clear();
        }
        bool hasAccess = !access.empty();
        if (hasAccess || contains) {
            addPath(symbol.symbol);
        }
    }

    template<typename T>
        requires std::is_base_of_v<slang::ast::MemberAccessExpression, T>
    void handle(const T& symbol) {
        Restorer accessRestorer(access);
        if (symbol.sourceRange.contains(location)) {
            const slang::ast::Symbol& member = symbol.member;
            auto valueSymbol = member.as_if<slang::ast::ValueSymbol>();
            if (!valueSymbol) {
                return;
            }
            access = fmt::format(".{}", symbol.member.name);
        }
        else if (!access.empty()) {
            access = fmt::format(".{}{}", symbol.member.name, access);
        }
        visitDefault(symbol);
    }

    std::vector<std::string> getInstances() { return instances; }

private:
    slang::ast::Compilation& compilation;
    slang::SourceManager& sourceManager;
    const slang::SourceLocation location;
    std::string access;

    std::vector<std::string> instances;

    std::set<slang::ast::SymbolKind> waveformKinds = {
        slang::ast::SymbolKind::Parameter,     slang::ast::SymbolKind::Port,
        slang::ast::SymbolKind::Genvar,        slang::ast::SymbolKind::Net,
        slang::ast::SymbolKind::Variable,      slang::ast::SymbolKind::Instance,
        slang::ast::SymbolKind::InterfacePort, slang::ast::SymbolKind::ModportPort,
    };

    void addPath(const slang::ast::Symbol& symbol) {
        if (!waveformKinds.contains(symbol.kind)) {
            return;
        }
        std::string hier = symbol.getHierarchicalPath();
        instances.push_back(fmt::format("{}{}", hier, access));
    }

    bool containsLocation(const slang::ast::Symbol& symbol) {
        slang::SourceLocation endLocation(symbol.location);
        endLocation += symbol.name.length();
        slang::SourceRange range(symbol.location, endLocation);
        return range.contains(location);
    }
};
} // namespace inst
