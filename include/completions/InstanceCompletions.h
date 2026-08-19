//------------------------------------------------------------------------------
// InstanceCompletions.h
// Module, interface, package, and class completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "completions/CompletionContext.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "slang/syntax/SyntaxTree.h"

namespace slang::parsing {
class Token;
}

namespace server::completions {

/// Query, candidate generation, and resolver for indexed design-unit completions.
class InstanceCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   const slang::parsing::Token* moduleToken);

    static void addCompletions(std::vector<lsp::CompletionItem>& results, const Indexer& indexer,
                               const CompletionContext& context);

    static lsp::CompletionItem getCompletion(std::string name, slang::syntax::SyntaxKind kind);

    static void resolve(const slang::syntax::SyntaxTree& tree, std::string_view moduleName,
                        lsp::CompletionItem& item, bool excludeName = false);

    static void resolve(CompletionDispatch& dispatch, lsp::CompletionItem& item,
                        std::optional<std::filesystem::path> modulePath = std::nullopt,
                        bool excludeName = false);

protected:
    using CompletionQuery::CompletionQuery;

private:
    static void resolveModuleInstance(const slang::syntax::ModuleHeaderSyntax& header,
                                      lsp::CompletionItem& item, bool excludeName);
};

} // namespace server::completions
