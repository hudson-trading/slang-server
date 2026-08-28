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
#include <span>

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"

namespace server {

class SymbolTreeVisitor : public slang::syntax::SyntaxVisitor<SymbolTreeVisitor> {
    const slang::SourceManager& m_sourceManager;
    std::span<const slang::syntax::DefineDirectiveSyntax* const> m_macros;
    std::vector<lsp::DocumentSymbol> m_symbols;
    std::vector<lsp::DocumentSymbol>* m_symbols_ptr;

private:
    [[nodiscard]] bool extract_range(const slang::parsing::Token& token, lsp::DocumentSymbol&,
                                     std::optional<std::string> overrideName = std::nullopt);
    void handle_module(const auto&);
    void handle_decl_list(const auto&, lsp::SymbolKind);
    void handle_recursive(const slang::syntax::SyntaxNode&, lsp::DocumentSymbol&);

public:
    SymbolTreeVisitor(const slang::SourceManager&);

    std::vector<lsp::DocumentSymbol> get_symbols(std::shared_ptr<slang::syntax::SyntaxTree> tree,
                                                 bool);
    void invalidate() { m_symbols.clear(); }

    void handle(const slang::syntax::GenerateBlockSyntax&);

    // ModuleDeclarationSyntax captures:
    //   - module
    //   - interface
    //   - package
    //   - program
    void handle(const slang::syntax::ModuleDeclarationSyntax&);
    void handle(const slang::syntax::ExternModuleDeclSyntax&);
    void handle(const slang::syntax::ClassDeclarationSyntax&);

    // Instance declaration
    void handle(const slang::syntax::HierarchyInstantiationSyntax&);

    // FunctionDeclarationSyntax captures:
    //   - function
    //   - task
    void handle(const slang::syntax::FunctionDeclarationSyntax&);

    void handle(const slang::syntax::NetDeclarationSyntax&);
    void handle(const slang::syntax::LocalVariableDeclarationSyntax&);
    void handle(const slang::syntax::DataDeclarationSyntax&);
    void handle(const slang::syntax::PortDeclarationSyntax&);
    void handle(const slang::syntax::ImplicitAnsiPortSyntax&);
    // ParameterDeclarationSyntax captures:
    //   - parameter
    //   - localparam
    void handle(const slang::syntax::ParameterDeclarationSyntax&);
};
} // namespace server
