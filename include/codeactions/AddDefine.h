//------------------------------------------------------------------------------
// AddDefine.h
// Code action to add a -D define for an undefined macro in ifdef
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "codeactions/CodeActionDispatch.h"

namespace server::codeactions {

/// Add an "Add define" code action if the context is an undefined macro in ifdef/ifndef/elsif
void addAddDefineAction(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                        const CodeActionContext& ctx);

} // namespace server::codeactions
