//------------------------------------------------------------------------------
// ExpandMacro.h
// Code action to expand a macro usage inline
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "codeactions/CodeActionDispatch.h"

namespace server::codeactions {

/// Add an "Expand macro" code action if the context is a macro usage
void addExpandMacroAction(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                          const CodeActionContext& ctx);

} // namespace server::codeactions
