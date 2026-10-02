//------------------------------------------------------------------------------
// NamedPortCompletions.h
// Named port and parameter connection completions inside a module instance.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "completions/CompletionContext.h"
#include <memory>

namespace server::completions {

class NamedPortCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   slang::SourceLocation cursor, bool parameters,
                                                   bool followedByCall);

protected:
    using CompletionQuery::CompletionQuery;
};

} // namespace server::completions
