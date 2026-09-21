//------------------------------------------------------------------------------
// AddInclude.h
// Include quick fixes for unresolved macros and header declarations
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "codeactions/CodeActionDispatch.h"

namespace server::codeactions {

/// Offer source edits that include an indexed header providing the unresolved name.
void addIncludeActions(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                       const CodeActionContext& ctx);

} // namespace server::codeactions
