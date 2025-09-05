//------------------------------------------------------------------------------
// InstanceIndexer.h
// AST visitor for indexing instance symbols by definition name
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"

struct InstanceIndexer : public slang::ast::ASTVisitor<InstanceIndexer, false, false> {
public:
    std::map<std::string, std::vector<const slang::ast::InstanceSymbol*>> moduleToInstances;
    void handle(const slang::ast::InstanceSymbol& symbol) {
        moduleToInstances[std::string{symbol.getDefinition().name}].push_back(&symbol);
        visitDefault(symbol.body);
    }

    void reset(const slang::ast::Symbol* root) {
        moduleToInstances.clear();
        root->visit(*this);
    }

    void clear() { moduleToInstances.clear(); }

    bool empty() const { return moduleToInstances.empty(); }
};