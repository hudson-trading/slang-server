//------------------------------------------------------------------------------
// SymbolTreeVisitor.cpp
// Implementation of symbol tree visitor for document symbols
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/SymbolTreeVisitor.h"

#include "lsp/LspTypes.h"
#include "util/Converters.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "slang/parsing/Token.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {
using namespace slang::syntax;

SymbolTreeVisitor::SymbolTreeVisitor(const SourceManager& sourceManager) :
    m_sourceManager(sourceManager), m_symbols_ptr(&m_symbols) {};

std::vector<lsp::DocumentSymbol> SymbolTreeVisitor::get_symbols(std::shared_ptr<SyntaxTree> tree,
                                                                const bool macros = true) {
    if (m_symbols.empty()) {
        visit(tree->root());

        if (macros) {
            auto tree_macros = tree->getDefinedMacros();
            for (const DefineDirectiveSyntax* const macro : tree_macros) {
                if (macro && macro->name.range().start() != slang::SourceLocation::NoLocation) {
                    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Constant};
                    bool ok = extract_range(macro->name, symbol);
                    if (ok) {
                        m_symbols.push_back(symbol);
                    }
                }
            }
        }
    }
    return m_symbols;
}

// Map a SourceRange to the `range` and `selectionRange` of DocumentSymbol.
// LSP doens't allow empty names- so we must check for that.
[[nodiscard]] bool SymbolTreeVisitor::extract_range(const slang::parsing::Token& token,
                                                    lsp::DocumentSymbol& symbol,
                                                    std::optional<std::string> overrideName) {

    // Don't show symbols from includes
    if (m_sourceManager.isIncludedFileLoc(token.range().start())) {
        return false;
    }
    symbol.name = token.valueText();
    if (symbol.name.empty()) {
        if (overrideName) {
            symbol.name = *overrideName;
        }
        else {
            return false;
        }
    }
    symbol.range = toRange(token.range(), m_sourceManager);
    symbol.selectionRange = symbol.range;
    return true;
}

// Deal with recursing down through child nodes
void SymbolTreeVisitor::handle_recursive(const SyntaxNode& node, lsp::DocumentSymbol& symbol) {
    if (node.getChildCount() != 0) {
        // Store the pointer to the current hierarchy level
        std::vector<lsp::DocumentSymbol>* parent_ptr = m_symbols_ptr;

        // Walk down the hierarchy until a matching `handle` is hit
        std::vector<lsp::DocumentSymbol> children;
        m_symbols_ptr = &children;
        visitDefault(node);

        if (!children.empty())
            symbol.children = children;

        m_symbols_ptr = parent_ptr;
    }

    m_symbols_ptr->push_back(symbol);
}

std::string nodeStr(const SyntaxNode& node) {
    auto ret = SyntaxPrinter().setIncludeComments(false).print(node).str();
    // remove leading newlines
    auto firstChar = ret.find_first_not_of("\n\r ");
    if (firstChar != std::string::npos) {
        ret = ret.substr(firstChar);
    }
    return ret;
}

// Common method for iterating over lists of declarations.
void SymbolTreeVisitor::handle_decl_list(const auto& node, lsp::SymbolKind kind) {

    for (const DeclaratorSyntax* decl : node.declarators) {
        if (decl) {
            lsp::DocumentSymbol symbol{.detail = nodeStr(*node.type), .kind = kind};
            bool ok = extract_range(decl->name, symbol);
            if (ok) {
                handle_recursive(*decl, symbol);
            }
        }
    }
}

/*
 * Catches all types of begin : end generate blocks
 * The SV LRM allows block names on either side of the begin statement:
 *   myblock: begin
 *   ...
 * or
 *   begin: myblock
 *   ...
 * Slang allows either usage, but not both concurrently.
 * If neither are given, add the block to the symbol tree anonymously.
 */
void SymbolTreeVisitor::handle(const GenerateBlockSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Struct};

    // Label after `begin` keyword
    bool ok;
    if (node.beginName) {
        ok = extract_range(node.beginName->name, symbol);
    }
    // Label before `begin` keyword
    else if (node.label) {
        ok = extract_range(node.label->name, symbol);
    }
    else {
        ok = extract_range(node.begin, symbol, "<anonymous block>");
    }

    handle_recursive(node, symbol);
}

// Handle module and external module declarations
void SymbolTreeVisitor::handle_module(const auto& node) {
    if (node.header) {
        lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Module};
        bool ok = extract_range(node.header->name, symbol);
        if (ok) {
            handle_recursive(node, symbol);
        }
    }
}

void SymbolTreeVisitor::handle(const ModuleDeclarationSyntax& node) {
    handle_module(node);
}

void SymbolTreeVisitor::handle(const ExternModuleDeclSyntax& node) {
    handle_module(node);
}

void SymbolTreeVisitor::handle(const ClassDeclarationSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Class};

    bool ok = extract_range(node.name, symbol);
    if (ok) {
        handle_recursive(node, symbol);
    }
}

// Handle hierarchical instantiations; e.g., module instances
void SymbolTreeVisitor::handle(const HierarchyInstantiationSyntax& node) {
    for (const HierarchicalInstanceSyntax* inst : node.instances) {

        if (!inst) {
            continue;
        }

        const InstanceNameSyntax* decl = inst->decl;
        if (!decl) {
            continue;
        }

        lsp::DocumentSymbol symbol{.detail = std::string{node.type.valueText()},
                                   .kind = lsp::SymbolKind::Object};

        bool ok = extract_range(decl->name, symbol);
        if (ok) {
            handle_recursive(*decl, symbol);
        }
    }
}

void SymbolTreeVisitor::handle(const FunctionDeclarationSyntax& node) {
    if (node.prototype) {
        // Target the whole NameSyntax node, as it could be a hierarchical
        // identifier of many Tokens
        const NameSyntax* name_syntax = node.prototype->name;

        if (name_syntax) {
            lsp::DocumentSymbol symbol{
                .name = name_syntax->toString(),
                .kind = lsp::SymbolKind::Function,
                .range = toRange(name_syntax->sourceRange(), m_sourceManager),
            };
            symbol.selectionRange = symbol.range;

            handle_recursive(node, symbol);
        }
    }
}

void SymbolTreeVisitor::handle(const NetDeclarationSyntax& node) {
    handle_decl_list(node, lsp::SymbolKind::Variable);
}

void SymbolTreeVisitor::handle(const LocalVariableDeclarationSyntax& node) {
    handle_decl_list(node, lsp::SymbolKind::Variable);
}

void SymbolTreeVisitor::handle(const DataDeclarationSyntax& node) {
    handle_decl_list(node, lsp::SymbolKind::Variable);
}

void SymbolTreeVisitor::handle(const PortDeclarationSyntax& node) {
    for (const DeclaratorSyntax* decl : node.declarators) {
        if (decl) {
            lsp::DocumentSymbol symbol{.detail = nodeStr(*node.header),
                                       .kind = lsp::SymbolKind::Interface};
            bool ok = extract_range(decl->name, symbol);
            if (ok) {
                handle_recursive(*decl, symbol);
            }
        }
    }
}

void SymbolTreeVisitor::handle(const ImplicitAnsiPortSyntax& node) {
    lsp::DocumentSymbol symbol{.detail = nodeStr(*node.header), .kind = lsp::SymbolKind::Interface};
    bool ok = extract_range(node.declarator->name, symbol);
    if (ok) {
        m_symbols_ptr->push_back(symbol);
    }
}

void SymbolTreeVisitor::handle(const ParameterDeclarationSyntax& node) {
    handle_decl_list(node, lsp::SymbolKind::TypeParameter);
}

} // namespace server
