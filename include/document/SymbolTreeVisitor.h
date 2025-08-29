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

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"
namespace server {
using namespace slang::syntax;
using namespace slang;

class SymbolTreeVisitor : public SyntaxVisitor<SymbolTreeVisitor> {
    const SourceManager& m_sourceManager;
    std::span<const DefineDirectiveSyntax* const> m_macros;
    std::vector<lsp::DocumentSymbol> m_symbols;
    std::vector<lsp::DocumentSymbol>* m_symbols_ptr;

private:
    [[nodiscard]] bool extract_range(const slang::parsing::Token& token, lsp::DocumentSymbol&,
                                     std::optional<std::string> overrideName = std::nullopt);
    void handle_module(const auto&);
    void handle_decl_list(const auto&, lsp::SymbolKind);
    void handle_recursive(const SyntaxNode&, lsp::DocumentSymbol&);

public:
    SymbolTreeVisitor(const SourceManager&);

    std::vector<lsp::DocumentSymbol> get_symbols(std::shared_ptr<SyntaxTree> tree, bool);
    void invalidate() { m_symbols.clear(); }

    void handle(const GenerateBlockSyntax&);

    // ModuleDeclarationSyntax captures:
    //   - module
    //   - interface
    //   - package
    //   - program
    void handle(const ModuleDeclarationSyntax&);
    void handle(const ExternModuleDeclSyntax&);
    void handle(const ClassDeclarationSyntax&);

    // Instance declaration
    void handle(const HierarchyInstantiationSyntax&);

    // FunctionDeclarationSyntax captures:
    //   - function
    //   - task
    void handle(const FunctionDeclarationSyntax&);

    void handle(const NetDeclarationSyntax&);
    void handle(const LocalVariableDeclarationSyntax&);
    void handle(const DataDeclarationSyntax&);
    void handle(const PortDeclarationSyntax&);
    void handle(const ImplicitAnsiPortSyntax&);
    // ParameterDeclarationSyntax captures:
    //   - parameter
    //   - localparam
    void handle(const ParameterDeclarationSyntax&);
};
} // namespace server