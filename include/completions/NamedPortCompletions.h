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

/// Query for the port names offered after a '.' inside an instance's connection
/// list, and for the parameter names offered inside its '#(...)' list.
class NamedPortCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   slang::SourceLocation cursor, bool parameters,
                                                   bool followedByCall);

protected:
    using CompletionQuery::CompletionQuery;
};

} // namespace server::completions
