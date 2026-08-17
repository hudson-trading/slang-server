//------------------------------------------------------------------------------
// SystemTaskCompletions.h
// Completions for SystemVerilog built-in system tasks and functions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once
#include "completions/CompletionContext.h"
#include "lsp/LspTypes.h"
#include <memory>
#include <string_view>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/SystemSubroutine.h"
#include "slang/parsing/KnownSystemName.h"

namespace server::completions {

class SystemSubroutineCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   bool followedByCall);

protected:
    using CompletionQuery::CompletionQuery;
};

lsp::CompletionItem getSystemSubroutineCompletion(slang::parsing::KnownSystemName name,
                                                  const slang::ast::SystemSubroutine& subroutine);

void addSystemSubroutineCompletions(std::vector<lsp::CompletionItem>& results,
                                    const slang::ast::Compilation& compilation);

} // namespace server::completions
