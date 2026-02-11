//------------------------------------------------------------------------------
// Completions.h
// Completions for the LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once
#include "completions/CompletionContext.h"
#include "lsp/LspTypes.h"
#include <string>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/Symbol.h"
#include "slang/syntax/AllSyntax.h"

struct Indexer;

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

lsp::CompletionItem getInstanceCompletion(std::string name, const syntax::SyntaxKind& kind);

void resolveModuleInstance(const slang::syntax::ModuleHeaderSyntax& header,
                           lsp::CompletionItem& ret, bool excludeName = false);

void resolveModule(const slang::syntax::SyntaxTree& tree, std::string_view moduleName,
                   lsp::CompletionItem& ret, bool excludeName = false);

/// Index based completions
void addIndexedCompletions(std::vector<lsp::CompletionItem>& results, const Indexer& indexer,
                           const CompletionContext& ctx);

//------------------------------------------------------------------------------
// Members
//------------------------------------------------------------------------------

lsp::CompletionItemKind getCompletionKind(const slang::ast::Symbol& symbol);

/// Get single completion for a symbol
lsp::CompletionItem getMemberCompletion(const slang::ast::Symbol& symbol,
                                        const slang::ast::Scope* currentScope = nullptr);

/// Add completions for members in a scope to results
void addMemberCompletions(std::vector<lsp::CompletionItem>& results, const slang::ast::Scope* scope,
                          bool isLhs, const slang::ast::Scope* originalScope,
                          bool isOriginalCall = true);

/// Resolve additional information for a member completion
void resolveMemberCompletion(const slang::ast::Scope& scope, lsp::CompletionItem& item);

/// Hierarchical completion (struct member, instance member, etc)
lsp::CompletionItem getHierarchicalCompletion(const slang::ast::Symbol& parentSymbol,
                                              const slang::ast::Symbol& symbol);

/// Add completions for keywords, like 'logic' and 'always_ff'
void addModuleMemberKwCompletions(std::vector<lsp::CompletionItem>& results);

} // namespace server::completions
