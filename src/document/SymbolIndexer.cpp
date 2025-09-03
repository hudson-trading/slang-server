//------------------------------------------------------------------------------
// SymbolIndexer.cpp
// Implementation of symbol indexer for AST visitors
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/SymbolIndexer.h"

#include "util/Logging.h"

#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/util/Util.h"

namespace server {

/// Find the name token in a syntax node, 1 layer deep.
const slang::parsing::Token* findName(std::string_view name,
                                      const slang::syntax::SyntaxNode& node) {
    // Look through tokens first
    for (int childInd = 0; childInd < node.getChildCount(); childInd++) {
        auto child = const_cast<slang::syntax::SyntaxNode&>(node).childTokenPtr(childInd);
        if (child && child->kind == slang::parsing::TokenKind::Identifier) {
            if (child->valueText() == name) {
                return child;
            }
        }
    }
    // Look through syntax nodes
    for (int childInd = 0; childInd < node.getChildCount(); childInd++) {
        auto child = node.childNode(childInd);
        if (child) {
            auto nameTok = findName(name, *child);
            if (nameTok) {
                return nameTok;
            }
        }
    }
    return nullptr;
}

SymbolIndexer::SymbolIndexer(slang::BufferID buffer) : m_buffer(buffer) {
}

const slang::ast::Symbol* SymbolIndexer::getSymbol(const slang::parsing::Token* node) const {
    auto it = symdex.find(node);
    if (it == symdex.end()) {
        return nullptr;
    }
    return it->second;
}

const slang::ast::Symbol* SymbolIndexer::getSymbol(const slang::syntax::SyntaxNode* node) const {
    auto it = syntex.find(node);
    if (it == syntex.end()) {
        return nullptr;
    }
    return it->second;
}

const slang::ast::Scope* SymbolIndexer::getScopeForSyntax(
    const slang::syntax::SyntaxNode& syntax) const {

    const slang::syntax::SyntaxNode* current = &syntax;
    while (current != nullptr) {
        auto it = syntex.find(current);
        if (it != syntex.end()) {
            const slang::ast::Symbol* symbol = it->second;
            if (symbol) {
                if (symbol->isScope()) {
                    return &symbol->as<slang::ast::Scope>();
                }
                else {
                    return symbol->getParentScope();
                }
            }
        }
        current = current->parent;
    }

    return nullptr;
}

/// Module instances- module name, parameters, ports
void SymbolIndexer::handle(const slang::ast::InstanceSymbol& sym) {

    if (sym.getSyntax() == nullptr) {
        // This means it's the top level- just index the module name
        auto& modName =
            sym.getDefinition().getSyntax()->as<slang::syntax::ModuleDeclarationSyntax>();
        symdex[&modName.header->name] = &sym.getDefinition();
        visitDefault(sym);
        return;
    }
    else if (sym.getSyntax()->sourceRange().start().buffer() != m_buffer) {
        return;
    }

    switch (sym.getSyntax()->kind) {
        case slang::syntax::SyntaxKind::HierarchicalInstance: {
            auto& instSyntax = sym.getSyntax()->as<slang::syntax::HierarchicalInstanceSyntax>();

            // Mark ports
            for (auto port : instSyntax.connections) {
                switch (port->kind) {
                    case slang::syntax::SyntaxKind::NamedPortConnection: {
                        auto& portSyntax = port->as<slang::syntax::NamedPortConnectionSyntax>();
                        const slang::ast::Symbol* portSym = sym.body.lookupName(
                            portSyntax.name.valueText());
                        if (portSym) {
                            symdex[&portSyntax.name] = portSym;
                        }
                    } break;
                    default:
                        WARN("Unknown port symbol kind: {}", toString(port->kind));
                        break;
                }
            }

            if (instSyntax.parent == nullptr) {
                break;
            }

            // Mark param decl if we haven't already
            auto& paramInst = instSyntax.parent->as<slang::syntax::HierarchyInstantiationSyntax>();
            if (symdex.find(&paramInst.type) != symdex.end()) {
                break;
            }

            // Mark instance type
            symdex[&paramInst.type] = &sym.getDefinition();

            if (paramInst.parameters == nullptr) {
                break;
            }

            // Mark parameters
            for (auto param : paramInst.parameters->parameters) {
                switch (param->kind) {
                    case slang::syntax::SyntaxKind::NamedParamAssignment: {
                        auto& paramSyntax = param->as<slang::syntax::NamedParamAssignmentSyntax>();
                        const slang::ast::Symbol* paramSym = sym.body.lookupName(
                            paramSyntax.name.valueText());
                        if (paramSym) {
                            symdex[&paramSyntax.name] = paramSym;
                        }
                        else {
                            WARN("Failed to find parameter symbol: {} in body: {}",
                                 paramSyntax.name.valueText(), sym.body.name);
                        }
                    } break;
                    default:
                        WARN("Unknown parameter kind: {}", toString(param->kind));
                        break;
                }
            }
        } break;
        default: {
            WARN("Unknown instance symbol kind: {}", toString(sym.getSyntax()->kind));
        }
    }
    auto defSyn = sym.getDefinition().getSyntax();
    if (defSyn && defSyn->sourceRange().start().buffer() == m_buffer) {
        visitDefault(sym);
    }
}

/// Index ValueSymbol names
void SymbolIndexer::handle(const slang::ast::ValueSymbol& sym) {
    if (!sym.getSyntax()) {
        return;
    }

    auto nameTok = findName(sym.name, *sym.getSyntax());
    if (nameTok == nullptr) {
        INFO("Value Sym has no name token: {}: {}", sym.name, toString(sym.kind));
        return;
    }

    symdex[nameTok] = &sym;
}

// These are not in the buffer, but should be visited
void SymbolIndexer::handle(const slang::ast::RootSymbol& sym) {
    visitDefault(sym);
}

void SymbolIndexer::handle(const slang::ast::CompilationUnitSymbol& sym) {
    visitDefault(sym);
}

} // namespace server
