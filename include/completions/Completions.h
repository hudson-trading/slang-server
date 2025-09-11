//------------------------------------------------------------------------------
// Completions.h
// Completions for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once
#include "lsp/LspTypes.h"
#include <string>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/syntax/AllSyntax.h"

namespace server::completions {

using namespace slang;

//------------------------------------------------------------------------------
// Macros
//------------------------------------------------------------------------------
lsp::CompletionItem getMacroCompletion(const slang::syntax::DefineDirectiveSyntax& macro);

lsp::CompletionItem getMacroCompletion(std::string name);

void resolveMacro(const syntax::DefineDirectiveSyntax& macro, lsp::CompletionItem& ret);

//------------------------------------------------------------------------------
// Modules, Interfaces, Packages
//------------------------------------------------------------------------------
lsp::CompletionItem getModuleCompletion(std::string name, lsp::SymbolKind kind);

void resolveModule(const slang::syntax::ModuleHeaderSyntax& header, lsp::CompletionItem& ret,
                   bool excludeName = false);

void resolveModule(const slang::syntax::SyntaxTree& tree, std::string_view moduleName,
                   lsp::CompletionItem& ret, bool excludeName = false);

//------------------------------------------------------------------------------
// Members
//------------------------------------------------------------------------------

/// Get single completion for a symbol
lsp::CompletionItem getMemberCompletion(const slang::ast::Symbol& symbol,
                                        const slang::ast::Scope* currentScope);

/// Add completions for members in a scope to results
void getMemberCompletions(std::vector<lsp::CompletionItem>& results, const slang::ast::Scope* scope,
                          bool isLhs, const slang::ast::Scope* originalScope);

/// Resolve additional information for a member completion
void resolveMemberCompletion(const slang::ast::Scope& scope, lsp::CompletionItem& item);

} // namespace server::completions