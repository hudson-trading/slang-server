//------------------------------------------------------------------------------
// AssignmentPatternCompletions.h
// Assignment pattern completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "completions/MemberCompletions.h"
#include <memory>

namespace server::completions {

/// Query for the whole-struct snippet offered when an assignment pattern is opened.
class StructAssignCompletionQuery : public CompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   slang::SourceLocation cursor);

protected:
    using CompletionQuery::CompletionQuery;
};

/// Query for individual assignment pattern key completions.
class StructMemberCompletionQuery : public MemberCompletionQuery {
public:
    static std::unique_ptr<CompletionQuery> create(lsp::Range replacementRange,
                                                   slang::SourceLocation cursor,
                                                   bool followedByColon);

protected:
    using MemberCompletionQuery::MemberCompletionQuery;
};

} // namespace server::completions
