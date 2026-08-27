//------------------------------------------------------------------------------
// IncludeForMacro.h
// Code action to add a `include for a macro that is used but not defined
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "codeactions/CodeActionDispatch.h"

#include "slang/text/SourceLocation.h"

namespace server::codeactions {

/// Add an "Add `include" code action when the cursor is on a macro usage whose
/// definition isn't visible here but the workspace index knows which file(s)
/// define it. One action per candidate file.
void addIncludeForMacroAction(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                              const CodeActionContext& ctx, slang::SourceLocation loc);

} // namespace server::codeactions
