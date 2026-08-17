//------------------------------------------------------------------------------
// KeywordCompletions.h
// SystemVerilog keyword completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <vector>

namespace server::completions {

void addKeywordCompletions(std::vector<lsp::CompletionItem>& results);

} // namespace server::completions
