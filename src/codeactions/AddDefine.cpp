//------------------------------------------------------------------------------
// AddDefine.cpp
// Code action to add a -D define for an undefined macro in ifdef
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "codeactions/AddDefine.h"

#include <rfl/Generic.hpp>

namespace server::codeactions {
using namespace slang;

void addAddDefineAction(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                        const CodeActionContext& ctx) {
    if (!ctx.token)
        return;

    auto macroName = std::string(ctx.token->valueText());
    if (macroName.empty())
        return;

    // Only show when the macro is not defined in the current file
    if (ctx.analysis.macros.find(macroName) != ctx.analysis.macros.end())
        return;

    results.push_back(lsp::CodeAction{
        .title = fmt::format("Add define '{}' to local flags", macroName),
        .kind = lsp::CodeActionKind::from_name<"quickfix">(),
        .command =
            lsp::Command{
                .title = "Add define",
                .command = "slang.addDefine",
                .arguments = {std::vector<lsp::LSPAny>{rfl::Generic(macroName)}},
            },
    });
}

} // namespace server::codeactions
