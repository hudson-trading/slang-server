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

SymbolTreeVisitor::SymbolTreeVisitor(const slang::SourceManager& sourceManager) :
    m_sourceManager(sourceManager) {
}

std::vector<lsp::DocumentSymbol> SymbolTreeVisitor::getSymbols(
    std::shared_ptr<slang::syntax::SyntaxTree> tree, const bool macros) {
    if (m_symbols.empty()) {
        visit(tree->root());

        if (macros) {
            auto tree_macros = tree->getDefinedMacros();
            for (const DefineDirectiveSyntax* const macro : tree_macros) {
                if (macro && macro->name.range().start() != slang::SourceLocation::NoLocation) {
                    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Constant};
                    bool ok = extractRange(macro->name, symbol, std::nullopt,
                                           /* allowMacroLocation */ true);
                    if (ok) {
                        symbol.range = toRange(macro->sourceRange(), m_sourceManager);
                        m_symbols.push_back(symbol);
                    }
                }
            }
        }
    }
    return m_symbols;
}

// Initialize the symbol from its name token; callers widen `range` to the full declaration.
[[nodiscard]] bool SymbolTreeVisitor::extractRange(const slang::parsing::Token& token,
                                                   lsp::DocumentSymbol& symbol,
                                                   std::optional<std::string> overrideName,
                                                   bool allowMacroLocation) {

    // Don't show syntax-derived symbols from includes or macro expansions.
    if (m_sourceManager.isIncludedFileLoc(token.range().start()) ||
        (!allowMacroLocation && m_sourceManager.isMacroLoc(token.range().start()))) {
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
void SymbolTreeVisitor::handleRecursive(const SyntaxNode& node, lsp::DocumentSymbol& symbol) {
    if (m_sourceManager.isMacroLoc(node.sourceRange().start())) {
        return;
    }

    symbol.range = toRange(node.sourceRange(), m_sourceManager);

    if (node.getChildCount() != 0) {
        // Store the pointer to the current hierarchy level
        std::vector<lsp::DocumentSymbol>* parentSymbols = m_currentSymbols;

        // Walk down the hierarchy until a matching `handle` is hit
        std::vector<lsp::DocumentSymbol> children;
        m_currentSymbols = &children;
        visitDefault(node);

        if (!children.empty())
            symbol.children = children;

        m_currentSymbols = parentSymbols;
    }

    // LSP rejects symbols with empty names
    if (symbol.name.empty()) {
        return;
    }
    m_currentSymbols->push_back(symbol);
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
void SymbolTreeVisitor::handleDeclList(const auto& node, lsp::SymbolKind kind) {

    for (const DeclaratorSyntax* decl : node.declarators) {
        if (decl) {
            lsp::DocumentSymbol symbol{.detail = nodeStr(*node.type), .kind = kind};
            bool ok = extractRange(decl->name, symbol);
            if (ok) {
                handleRecursive(*decl, symbol);
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
        ok = extractRange(node.beginName->name, symbol);
    }
    // Label before `begin` keyword
    else if (node.label) {
        ok = extractRange(node.label->name, symbol);
    }
    else {
        ok = extractRange(node.begin, symbol, "<anonymous block>");
    }

    if (ok) {
        handleRecursive(node, symbol);
    }
}

// Handle module and external module declarations
void SymbolTreeVisitor::handleModule(const auto& node) {
    if (node.header) {
        lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Module};
        bool ok = extractRange(node.header->name, symbol);
        if (ok) {
            handleRecursive(node, symbol);
        }
    }
}

void SymbolTreeVisitor::handle(const ModuleDeclarationSyntax& node) {
    handleModule(node);
}

void SymbolTreeVisitor::handle(const ExternModuleDeclSyntax& node) {
    handleModule(node);
}

void SymbolTreeVisitor::handle(const ClassDeclarationSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Class};

    bool ok = extractRange(node.name, symbol);
    if (ok) {
        handleRecursive(node, symbol);
    }
}

void SymbolTreeVisitor::handleTypedef(const auto& node, lsp::SymbolKind kind) {
    lsp::DocumentSymbol symbol{.kind = kind};
    bool ok = extractRange(node.name, symbol);
    if (ok) {
        symbol.selectionRange = toRange(node.typedefKeyword.range(), m_sourceManager);
        handleRecursive(node, symbol);
    }
}

void SymbolTreeVisitor::handle(const TypedefDeclarationSyntax& node) {
    handleTypedef(node, node.type->kind == SyntaxKind::EnumType ? lsp::SymbolKind::Enum
                                                                : lsp::SymbolKind::Struct);
}

void SymbolTreeVisitor::handle(const ForwardTypedefDeclarationSyntax& node) {
    handleTypedef(node, lsp::SymbolKind::Struct);
}

// Handle hierarchical instantiations; e.g., module instances
void SymbolTreeVisitor::handle(const HierarchyInstantiationSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Module};
    if (node.parameters) {
        symbol.detail = nodeStr(*node.parameters);
    }
    bool ok = extractRange(node.type, symbol);
    if (ok) {
        handleRecursive(node, symbol);
    }
}

void SymbolTreeVisitor::handle(const HierarchicalInstanceSyntax& node) {
    if (node.decl) {
        lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Object};
        bool ok = extractRange(node.decl->name, symbol);
        if (ok) {
            handleRecursive(node, symbol);
        }
    }
}

void SymbolTreeVisitor::handle(const ProceduralBlockSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Event};
    bool ok = extractRange(node.keyword, symbol);
    if (ok) {
        handleRecursive(node, symbol);
    }
}

void SymbolTreeVisitor::handleStatement(const SyntaxNode& node,
                                        const slang::parsing::Token& keyword) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Object};
    bool ok = extractRange(keyword, symbol);
    if (ok) {
        handleRecursive(node, symbol);
    }
}

void SymbolTreeVisitor::handle(const ConditionalStatementSyntax& node) {
    if (node.parent && node.parent->kind == SyntaxKind::ElseClause) {
        visitDefault(node);
        return;
    }
    handleStatement(node, node.ifKeyword);
}

void SymbolTreeVisitor::handle(const ElseClauseSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Object};
    bool ok = extractRange(node.elseKeyword, symbol);
    if (ok) {
        if (const auto* conditional = node.clause->as_if<ConditionalStatementSyntax>()) {
            symbol.name = "else if";
            symbol.selectionRange =
                toRange(slang::SourceRange(node.elseKeyword.range().start(),
                                           conditional->ifKeyword.range().end()),
                        m_sourceManager);
        }
        handleRecursive(node, symbol);
    }
}

void SymbolTreeVisitor::handle(const ForLoopStatementSyntax& node) {
    handleStatement(node, node.forKeyword);
}

void SymbolTreeVisitor::handle(const CaseStatementSyntax& node) {
    handleStatement(node, node.caseKeyword);
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

            handleRecursive(node, symbol);
        }
    }
}

void SymbolTreeVisitor::handle(const NetDeclarationSyntax& node) {
    handleDeclList(node, lsp::SymbolKind::Variable);
}

void SymbolTreeVisitor::handle(const LocalVariableDeclarationSyntax& node) {
    handleDeclList(node, lsp::SymbolKind::Variable);
}

void SymbolTreeVisitor::handle(const DataDeclarationSyntax& node) {
    handleDeclList(node, lsp::SymbolKind::Variable);
}

void SymbolTreeVisitor::handle(const StructUnionMemberSyntax& node) {
    handleDeclList(node, lsp::SymbolKind::Field);
}

void SymbolTreeVisitor::handle(const EnumTypeSyntax& node) {
    for (const DeclaratorSyntax* decl : node.members) {
        if (decl) {
            lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::EnumMember};
            bool ok = extractRange(decl->name, symbol);
            if (ok) {
                handleRecursive(*decl, symbol);
            }
        }
    }
}

void SymbolTreeVisitor::handle(const PortDeclarationSyntax& node) {
    for (const DeclaratorSyntax* decl : node.declarators) {
        if (decl) {
            lsp::DocumentSymbol symbol{.detail = nodeStr(*node.header),
                                       .kind = lsp::SymbolKind::Interface};
            bool ok = extractRange(decl->name, symbol);
            if (ok) {
                handleRecursive(*decl, symbol);
            }
        }
    }
}

void SymbolTreeVisitor::handle(const ImplicitAnsiPortSyntax& node) {
    lsp::DocumentSymbol symbol{.detail = nodeStr(*node.header), .kind = lsp::SymbolKind::Interface};
    bool ok = extractRange(node.declarator->name, symbol);
    if (ok && !m_sourceManager.isMacroLoc(node.sourceRange().start())) {
        symbol.range = toRange(node.sourceRange(), m_sourceManager);
        m_currentSymbols->push_back(symbol);
    }
}

void SymbolTreeVisitor::handle(const ParameterDeclarationStatementSyntax& node) {
    visitDefault(node);
}

void SymbolTreeVisitor::handle(const ParameterDeclarationSyntax& node) {
    handleDeclList(node, lsp::SymbolKind::TypeParameter);
}

void SymbolTreeVisitor::handle(const MemberSyntax& node) {
    lsp::DocumentSymbol symbol{.kind = lsp::SymbolKind::Object};
    bool ok = extractRange(node.getFirstToken(), symbol);
    if (ok) {
        handleRecursive(node, symbol);
    }
}

} // namespace server
