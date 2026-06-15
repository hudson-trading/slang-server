//------------------------------------------------------------------------------
// SystemTaskCompletions.h
// Completions for SystemVerilog built-in system tasks and functions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once
#include "lsp/LspTypes.h"
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/SystemSubroutine.h"
#include "slang/parsing/KnownSystemName.h"

namespace server::completions {

lsp::CompletionItem getSystemSubroutineCompletion(slang::parsing::KnownSystemName name,
                                                  const slang::ast::SystemSubroutine& subroutine);

void addSystemSubroutineCompletions(std::vector<lsp::CompletionItem>& results,
                                    const slang::ast::Compilation& compilation);

} // namespace server::completions
