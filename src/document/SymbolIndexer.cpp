//------------------------------------------------------------------------------
// SymbolIndexer.cpp
// Implementation of symbol indexer for AST visitors
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/SymbolIndexer.h"

#include "util/Logging.h"
#include "util/SlangExtensions.h"
#include <algorithm>

#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/util/SmallVector.h"
#include "slang/util/Util.h"

namespace server {

using namespace slang;

/// Find the name token in a syntax node, 1 layer deep.
void findNames(SmallVector<const parsing::Token*>& result, std::string_view name,
               const slang::syntax::SyntaxNode& node) {
    // Look through tokens first
    for (size_t childInd = 0; childInd < node.getChildCount(); childInd++) {
        auto child = const_cast<slang::syntax::SyntaxNode&>(node).childTokenPtr(childInd);
        if (child && child->kind == slang::parsing::TokenKind::Identifier) {
            if (child->valueText() == name) {
                result.push_back(child);
            }
        }
    }
    // Look through syntax nodes
    for (size_t childInd = 0; childInd < node.getChildCount(); childInd++) {
        auto child = node.childNode(childInd);
        if (child) {
            if (result.size() == 0 || child->kind == slang::syntax::SyntaxKind::NamedBlockClause) {
                findNames(result, name, *child);
            }
        }
    }
}

SymbolIndexer::SymbolIndexer(slang::BufferID buffer) : m_buffer(buffer) {
}

const slang::ast::Symbol* SymbolIndexer::getSymbol(const slang::parsing::Token* node) const {
    auto symbols = getSymbols(node);
    if (symbols.empty()) {
        return nullptr;
    }
    return symbols.back();
}

const slang::ast::Symbol* SymbolIndexer::getSymbol(const slang::syntax::SyntaxNode* node) const {
    auto it = syntex.find(node);
    if (it == syntex.end()) {
        return nullptr;
    }
    return it->second.back();
}

std::span<const slang::ast::Symbol* const> SymbolIndexer::getSymbols(
    const slang::parsing::Token* node) const {
    auto it = symdex.find(node);
    if (it == symdex.end()) {
        return {};
    }
    return {it->second.data(), it->second.size()};
}

void SymbolIndexer::addSymbol(const slang::parsing::Token* token,
                              const slang::ast::Symbol& symbol) {
    auto& symbols = symdex[token];
    if (std::ranges::find(symbols, &symbol) == symbols.end())
        symbols.push_back(&symbol);
}

void SymbolIndexer::addSymbol(const slang::syntax::SyntaxNode* syntax,
                              const slang::ast::Symbol& symbol) {
    auto& symbols = syntex[syntax];
    if (std::ranges::find(symbols, &symbol) == symbols.end())
        symbols.push_back(&symbol);
}

const slang::ast::Scope* SymbolIndexer::getScopeForSyntax(
    const slang::syntax::SyntaxNode& syntax) const {

    auto scopes = getScopesForSyntax(syntax);
    return scopes.empty() ? nullptr : scopes.back();
}

slang::SmallVector<const slang::ast::Scope*, 2> SymbolIndexer::getScopesForSyntax(
    const slang::syntax::SyntaxNode& syntax) const {

    const slang::syntax::SyntaxNode* current = &syntax;
    while (current != nullptr) {
        auto it = syntex.find(current);
        if (it != syntex.end()) {
            slang::SmallVector<const slang::ast::Scope*, 2> result;
            for (const auto* symbol : it->second) {
                const slang::ast::Scope* scope = nullptr;
                if (symbol->isScope()) {
                    scope = &symbol->as<slang::ast::Scope>();
                }
                else
                    scope = symbol->getParentScope();

                if (scope && std::ranges::find(result, scope) == result.end())
                    result.push_back(scope);
            }
            if (!result.empty())
                return result;
        }
        current = current->parent;
    }

    return {};
}

void SymbolIndexer::indexInstanceSyntax(const slang::syntax::HierarchicalInstanceSyntax& instSyntax,
                                        const slang::ast::InstanceBodySymbol& body,
                                        const slang::ast::DefinitionSymbol& definition,
                                        const slang::ast::InstanceSymbol* instance,
                                        const slang::ast::Scope* parentScope) {

    // Mark ports
    for (auto port : instSyntax.connections) {
        switch (port->kind) {
            case slang::syntax::SyntaxKind::NamedPortConnection: {
                auto& portSyntax = port->as<slang::syntax::NamedPortConnectionSyntax>();
                auto name = portSyntax.name.valueText();
                if (name.empty()) {
                    continue;
                }
                const slang::ast::Symbol* maybePortSym = body.findPort(portSyntax.name.valueText());
                if (!maybePortSym) {
                    continue;
                }

                // `.name` is shorthand for `.name(name)`, so its single token denotes both the
                // formal port and the value in the instance's parent scope.
                if (!portSyntax.openParen) {
                    auto addConnection = [&](const slang::ast::Symbol* outer,
                                             const slang::ast::Symbol* inner) {
                        if (!outer || !inner)
                            return false;

                        auto& symbols = symdex[&portSyntax.name];
                        for (size_t i = 0; i + 1 < symbols.size(); i += 2) {
                            if (symbols[i] == outer && symbols[i + 1] == inner)
                                return true;
                        }
                        symbols.push_back(outer);
                        symbols.push_back(inner);
                        return true;
                    };

                    bool indexedConnection = false;
                    if (instance && instance->getSyntax()) {
                        for (auto* connection : instance->getPortConnections()) {
                            bool matchesFormal = &connection->port == maybePortSym;
                            if (auto* multiPort =
                                    maybePortSym->as_if<slang::ast::MultiPortSymbol>()) {
                                matchesFormal = std::ranges::find(multiPort->ports,
                                                                  &connection->port) !=
                                                multiPort->ports.end();
                            }
                            if (!matchesFormal)
                                continue;

                            const slang::ast::Symbol* outer = nullptr;
                            if (parentScope) {
                                outer = parentScope->lookupName(
                                    name, slang::ast::LookupLocation::after(*instance));
                            }
                            if (!outer) {
                                if (auto* expression = connection->getExpression())
                                    outer = expression->getSymbolReference();
                            }
                            if (!outer &&
                                connection->port.kind == slang::ast::SymbolKind::InterfacePort) {
                                outer = connection->getIfaceConn().first;
                            }

                            const slang::ast::Symbol* inner = &connection->port;
                            if (auto* port = connection->port.as_if<slang::ast::PortSymbol>();
                                port && port->internalSymbol) {
                                inner = port->internalSymbol;
                            }
                            indexedConnection |= addConnection(outer, inner);
                        }
                    }
                    else {
                        const slang::ast::Symbol* connected = nullptr;
                        if (parentScope) {
                            auto location = instance && instance->getParentScope() == parentScope
                                                ? slang::ast::LookupLocation::after(*instance)
                                                : slang::ast::LookupLocation::max;
                            connected = parentScope->lookupName(name, location);
                        }

                        const slang::ast::Symbol* inner = maybePortSym;
                        if (auto* port = maybePortSym->as_if<slang::ast::PortSymbol>();
                            port && port->internalSymbol) {
                            inner = port->internalSymbol;
                        }
                        indexedConnection = addConnection(connected, inner);
                    }

                    if (indexedConnection)
                        continue;
                }

                // Keep the formal last because single-symbol lookup selects the last entry.
                addSymbol(&portSyntax.name, *maybePortSym);

            } break;
            default:
                break;
        }
    }

    if (instSyntax.parent == nullptr) {
        return;
    }

    // Mark module type and parameters. The definition is normally shared, while elaborated
    // parameter symbols can differ for each instance that shares this syntax.
    auto& paramInst = instSyntax.parent->as<slang::syntax::HierarchyInstantiationSyntax>();
    addSymbol(&paramInst.type, definition);

    if (paramInst.parameters == nullptr) {
        return;
    }

    // Mark parameters
    for (auto param : paramInst.parameters->parameters) {
        switch (param->kind) {
            case slang::syntax::SyntaxKind::NamedParamAssignment: {
                auto& paramSyntax = param->as<slang::syntax::NamedParamAssignmentSyntax>();
                auto name = paramSyntax.name.valueText();
                if (!name.empty()) {
                    const slang::ast::Symbol* paramSym = body.lookupName(
                        paramSyntax.name.valueText());
                    if (paramSym) {
                        addSymbol(&paramSyntax.name, *paramSym);
                    }
                }
            } break;
            default:
                break;
        }
    }
}

void SymbolIndexer::indexSymbolName(const slang::ast::Symbol& symbol, bool isUnnamed) {
    if (symbol.getSyntax() != nullptr) {
        addSymbol(symbol.getSyntax(), symbol);

        auto& syntax = *symbol.getSyntax();
        if (!isUnnamed && syntax.sourceRange().start().buffer() == m_buffer &&
            !symbol.name.empty()) {
            SmallVector<const parsing::Token*> tokens;
            findNames(tokens, symbol.name, syntax);
            if (tokens.empty()) {
                // We want to avoid this case, since we may recurse through many layers
                WARN("No tokens found for symbol '{} : {}' with syntax kind {}", symbol.name,
                     toString(symbol.kind), toString(syntax.kind));
            }

            for (auto* tok : tokens) {
                addSymbol(tok, symbol);
            }
        }
    }
}

void SymbolIndexer::handle(const slang::ast::InstanceArraySymbol& sym) {
    // Index based on syntax, looking up definition from parent scope if needed
    if (sym.getSyntax() == nullptr || sym.location.buffer() != m_buffer ||
        sym.getSyntax()->kind != slang::syntax::SyntaxKind::HierarchicalInstance) {
        return;
    }

    auto& instSyntax = sym.getSyntax()->as<slang::syntax::HierarchicalInstanceSyntax>();
    addSymbol(&instSyntax.decl->name, sym);

    // Get definition and body - either from first element or by lookup
    if (!sym.elements.empty() && sym.elements[0]->kind == slang::ast::SymbolKind::Instance) {
        // Use first element if available
        auto& firstInst = sym.elements[0]->as<slang::ast::InstanceSymbol>();
        indexInstanceSyntax(instSyntax, firstInst.body, firstInst.getDefinition(), &firstInst,
                            firstInst.getParentScope());
    }
    else if (instSyntax.parent != nullptr &&
             instSyntax.parent->kind == slang::syntax::SyntaxKind::HierarchyInstantiation) {
        // Create an invalid instance to use for lookups
        auto& paramInst = instSyntax.parent->as<slang::syntax::HierarchyInstantiationSyntax>();
        auto parentScope = sym.getParentScope();
        if (!parentScope) {
            return;
        }
        auto result = parentScope->getCompilation().tryGetDefinition(paramInst.type.valueText(),
                                                                     *parentScope);
        if (!result.definition) {
            return;
        }
        auto& definition = result.definition->as<slang::ast::DefinitionSymbol>();
        auto& inst = slang::ast::InstanceSymbol::createInvalid(parentScope->getCompilation(),
                                                               definition);
        indexInstanceSyntax(instSyntax, inst.body, definition, &inst, sym.getParentScope());
    }
}

/// Module instances- module name, parameters, ports
void SymbolIndexer::handle(const slang::ast::InstanceSymbol& sym) {
    if (sym.getSyntax() == nullptr) {
        // This means it's the top level- just index the module name
        auto& modName =
            sym.getDefinition().getSyntax()->as<slang::syntax::ModuleDeclarationSyntax>();
        addSymbol(&modName.header->name, sym.getDefinition());
        visitDefault(sym);
        return;
    }
    else if (sym.location.buffer() != m_buffer) {
        return;
    }

    indexSymbolName(sym);

    switch (sym.getSyntax()->kind) {
        case slang::syntax::SyntaxKind::HierarchicalInstance: {
            auto& instSyntax = sym.getSyntax()->as<slang::syntax::HierarchicalInstanceSyntax>();
            addSymbol(&instSyntax.decl->name, sym);
            indexInstanceSyntax(instSyntax, sym.body, sym.getDefinition(), &sym,
                                sym.getParentScope());
        } break;
        default: {
            WARN("Unknown instance symbol kind: {}", toString(sym.getSyntax()->kind));
        }
    }
    if (sym.getDefinition().location.buffer() == m_buffer &&
        sym.instanceDepth < MAX_INSTANCE_DEPTH) {
        visitDefault(sym);
    }
}

void SymbolIndexer::handle(const slang::ast::EnumValueSymbol& sym) {
    auto& syntax = sym.getSyntax()->as<slang::syntax::DeclaratorSyntax>();
    if (syntax.dimensions.empty()) {
        addSymbol(&syntax.name, sym);
    }
}

void SymbolIndexer::handle(const slang::ast::TypeAliasType& value) {
    if (value.location.buffer() != m_buffer) {
        return;
    }
    // Check if already indexed to prevent infinite loops with circular type references
    if (value.getSyntax() && syntex.find(value.getSyntax()) != syntex.end()) {
        return;
    }
    indexSymbolName(value);
    unwrapErrorType(value.getDeclaredType()->getType()).visit(*this);
}

// These are not in the buffer, but should be visited
void SymbolIndexer::handle(const slang::ast::RootSymbol& sym) {
    visitDefault(sym);
}

void SymbolIndexer::handle(const slang::ast::CompilationUnitSymbol& sym) {
    visitDefault(sym);
}

} // namespace server
