//------------------------------------------------------------------------------
// CompletionContext.h
// Syntax-aware completion context detection
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <memory>
#include <string_view>

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
class ShallowAnalysis;

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

    /// Holds the analysis alive so that scope/syntax pointers remain valid
    std::shared_ptr<ShallowAnalysis> analysis;

    /// The LSP request context (triggerKind + triggerCharacter).
    lsp::CompletionContext lspContext;

    /// Source text from the start of the document up to the cursor.
    std::string_view prevText;

    /// Char immediately before the cursor (the just-typed char), or ' ' if at start.
    char lastChar() const { return prevText.empty() ? ' ' : prevText.back(); }

    /// Char two positions before the cursor, or ' ' if not enough text. Used to detect
    /// two-character trigger sequences like `::`.
    char prev2Char() const { return prevText.size() >= 2 ? prevText[prevText.size() - 2] : ' '; }

    /// LSP trigger character, or ' ' if `triggerKind` is `Invoked`.
    char triggerChar() const {
        return lspContext.triggerCharacter ? lspContext.triggerCharacter->at(0) : ' ';
    }

    /// Determine completion context from a document location and LSP request.
    /// @param doc The document containing the location
    /// @param loc The source location to analyze
    /// @param lspContext The LSP completion-request context
    /// @param prevText Text from start-of-doc to the cursor (used for trigger-char detection)
    /// @returns The completion context at that location
    static CompletionContext fromLocation(SlangDoc& doc, slang::SourceLocation loc,
                                          lsp::CompletionContext lspContext = {},
                                          std::string_view prevText = {});
};

} // namespace server
