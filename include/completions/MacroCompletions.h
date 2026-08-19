//------------------------------------------------------------------------------
// MacroCompletions.h
// SystemVerilog macro completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "completions/CompletionContext.h"
#include <memory>
#include <string>

namespace server::completions {

/// Query and resolver for macro completions.
class MacroCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   std::string typedPrefix, bool followedByCall);

    static void resolve(CompletionDispatch& dispatch, lsp::CompletionItem& item);

protected:
    using CompletionQuery::CompletionQuery;
};

} // namespace server::completions
