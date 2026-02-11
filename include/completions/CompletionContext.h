//------------------------------------------------------------------------------
// CompletionContext.h
// Syntax-aware completion context detection
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "slang/ast/Lookup.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/text/SourceLocation.h"

namespace slang::ast {
class Scope;
}

namespace slang::syntax {
class SyntaxNode;
}

namespace server {

class SlangDoc;

#define CCK(x) x(PortList) x(Expression) x(ModuleMember) x(Procedural) x(Unknown)
SLANG_ENUM(CompletionContextKind, CCK)
#undef CCK

/// Represents the completion context at a specific location in the source
struct CompletionContext {
    /// The kind of context (type vs value vs module item)
    CompletionContextKind kind = CompletionContextKind::Unknown;

    /// The scope at the completion location
    const slang::ast::Scope* scope = nullptr;

    /// The common ancestor of tokens before/after the location
    const slang::syntax::SyntaxNode* syntax = nullptr;

    /// Determine completion context from a document location
    /// @param doc The document containing the location
    /// @param loc The source location to analyze
    /// @returns The completion context at that location
    static CompletionContext fromLocation(SlangDoc& doc, slang::SourceLocation loc);
};

} // namespace server
