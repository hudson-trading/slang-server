//------------------------------------------------------------------------------
// MemberCompletions.h
// Lexical, member-access, and scoped-access completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "completions/CompletionContext.h"
#include "lsp/LspTypes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "slang/ast/Symbol.h"

namespace slang::parsing {
class Token;
}

namespace server::completions {

/// Base for queries that produce AST member candidates and resolve their deferred data.
class MemberCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> createLexical(lsp::Range replacementRange,
                                                          bool followedByCall,
                                                          bool followedByInstantiation);

    static std::unique_ptr<CompletionQuery> createMemberAccess(
        lsp::Range replacementRange, const slang::parsing::Token* receiverToken,
        bool followedByCall);

    static std::unique_ptr<CompletionQuery> createScopedAccess(
        lsp::Range replacementRange, const slang::parsing::Token* receiverToken,
        bool followedByCall);

    /// Resolve a member completion received through completionItem/resolve.
    static void resolve(CompletionDispatch& dispatch, lsp::CompletionItem& item);

    /// Populate deferred member properties while the symbol is already available.
    static void resolve(const slang::ast::Symbol& symbol, lsp::CompletionItem& item,
                        bool resolveCallableEdit = false);

protected:
    using CompletionQuery::CompletionQuery;

    static void addCompletions(std::vector<lsp::CompletionItem>& results,
                               const slang::ast::Scope* scope, CompletionContextKind contextKind,
                               const slang::ast::Scope* originalScope, std::string_view documentUri,
                               bool labelOnly = false, bool deferCallableEdit = false,
                               bool isOriginalCall = true);

    static lsp::CompletionItem getHierarchicalCompletion(const slang::ast::Symbol& parentSymbol,
                                                         const slang::ast::Symbol& symbol,
                                                         std::string_view documentUri,
                                                         bool labelOnly = false,
                                                         bool deferCallableEdit = false,
                                                         std::string_view completionLabel = {});

private:
    struct CompletionData {
        std::string documentUri;
        std::string symbolPath;
        std::string symbolName;
        uint32_t bufferId = 0;
        uint64_t offset = 0;
        slang::ast::SymbolKind symbolKind = slang::ast::SymbolKind::Unknown;
        bool labelOnly = false;
    };

    static lsp::CompletionItemKind getCompletionKind(const slang::ast::Symbol& symbol);
    static lsp::CompletionItem getCompletion(const slang::ast::Symbol& symbol,
                                             const slang::ast::Scope* currentScope,
                                             std::string_view documentUri, bool labelOnly,
                                             bool deferCallableEdit);
};

} // namespace server::completions
