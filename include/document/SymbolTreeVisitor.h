//------------------------------------------------------------------------------
// SymbolTreeVisitor.h
// Syntax visitor for building LSP document symbol trees
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "lsp/LspTypes.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"

namespace server {

class SymbolTreeVisitor : public slang::syntax::SyntaxVisitor<SymbolTreeVisitor> {
public:
    explicit SymbolTreeVisitor(const slang::SourceManager& sourceManager);

    std::vector<lsp::DocumentSymbol> getSymbols(std::shared_ptr<slang::syntax::SyntaxTree> tree,
                                                bool macros);
    void invalidate() { m_symbols.clear(); }

    void handle(const slang::syntax::GenerateBlockSyntax& node);

    // ModuleDeclarationSyntax captures:
    //   - module
    //   - interface
    //   - package
    //   - program
    void handle(const slang::syntax::ModuleDeclarationSyntax& node);
    void handle(const slang::syntax::ExternModuleDeclSyntax& node);
    void handle(const slang::syntax::ClassDeclarationSyntax& node);
    void handle(const slang::syntax::TypedefDeclarationSyntax& node);
    void handle(const slang::syntax::ForwardTypedefDeclarationSyntax& node);

    void handle(const slang::syntax::HierarchyInstantiationSyntax& node);
    void handle(const slang::syntax::HierarchicalInstanceSyntax& node);

    void handle(const slang::syntax::ProceduralBlockSyntax& node);
    void handle(const slang::syntax::ConditionalStatementSyntax& node);
    void handle(const slang::syntax::ElseClauseSyntax& node);
    void handle(const slang::syntax::ForLoopStatementSyntax& node);
    void handle(const slang::syntax::CaseStatementSyntax& node);

    // FunctionDeclarationSyntax captures:
    //   - function
    //   - task
    void handle(const slang::syntax::FunctionDeclarationSyntax& node);

    void handle(const slang::syntax::NetDeclarationSyntax& node);
    void handle(const slang::syntax::LocalVariableDeclarationSyntax& node);
    void handle(const slang::syntax::DataDeclarationSyntax& node);
    void handle(const slang::syntax::StructUnionMemberSyntax& node);
    void handle(const slang::syntax::EnumTypeSyntax& node);
    void handle(const slang::syntax::PortDeclarationSyntax& node);
    void handle(const slang::syntax::ImplicitAnsiPortSyntax& node);
    void handle(const slang::syntax::ParameterDeclarationStatementSyntax& node);
    // ParameterDeclarationSyntax captures:
    //   - parameter
    //   - localparam
    void handle(const slang::syntax::ParameterDeclarationSyntax& node);

    void handle(const slang::syntax::MemberSyntax& node);

private:
    [[nodiscard]] bool extractRange(const slang::parsing::Token& token, lsp::DocumentSymbol& symbol,
                                    std::optional<std::string> overrideName = std::nullopt,
                                    bool allowMacroLocation = false);
    void handleModule(const auto& node);
    void handleTypedef(const auto& node, lsp::SymbolKind kind);
    void handleDeclList(const auto& node, lsp::SymbolKind kind);
    void handleStatement(const slang::syntax::SyntaxNode& node,
                         const slang::parsing::Token& keyword);
    void handleRecursive(const slang::syntax::SyntaxNode& node, lsp::DocumentSymbol& symbol);

    const slang::SourceManager& m_sourceManager;
    std::vector<lsp::DocumentSymbol> m_symbols;
    std::vector<lsp::DocumentSymbol>* m_currentSymbols = &m_symbols;
};

} // namespace server
