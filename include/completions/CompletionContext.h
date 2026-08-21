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
#include <utility>
#include <vector>

#include "slang/ast/Lookup.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/text/SourceLocation.h"

namespace slang::ast {
class Scope;
}

namespace slang {
class Bag;
class SourceManager;
} // namespace slang

namespace slang::syntax {
class SyntaxNode;
}

struct Indexer;

namespace server {

class CompletionDispatch;
class ServerDriver;
struct CompletionContext;
class SlangDoc;
class ShallowAnalysis;

#define CCK(x) x(PortList) x(Expression) x(ModuleMember) x(Procedural) x(Unknown)
SLANG_ENUM(CompletionContextKind, CCK)
#undef CCK

#define CQK(x)                                                                          \
    x(Lexical) x(MemberAccess) x(ScopedAccess) x(StructAssign) x(StructMember) x(Macro) \
        x(SystemSubroutine) x(InstantiationSuffix)
SLANG_ENUM(CompletionQueryKind, CQK)
#undef CQK

/// A semantic completion operation derived from the lexer tokens around the cursor.
class CompletionQuery {
public:
    virtual ~CompletionQuery() = default;

    virtual CompletionQueryKind kind() const = 0;

    virtual void getCompletions(std::vector<lsp::CompletionItem>& results,
                                CompletionDispatch& dispatch, const std::shared_ptr<SlangDoc>& doc,
                                const CompletionContext& context) const = 0;

    virtual bool isIncomplete() const { return false; }

    /// Apply the query's replacement range and existing-source shape to a completion item.
    void setCompletionEdit(lsp::CompletionItem& item) const;

    /// Create the appropriate query from the request context and tokens surrounding the cursor.
    static std::unique_ptr<CompletionQuery> fromLocation(
        const SlangDoc& doc, const std::shared_ptr<ShallowAnalysis>& analysis,
        slang::SourceLocation cursor, const lsp::CompletionContext& lspContext);

protected:
    CompletionQuery(lsp::Range replacementRange, bool followedByCall = false,
                    bool followedByInstantiation = false) :
        replacementRange(std::move(replacementRange)), followedByCall(followedByCall),
        followedByInstantiation(followedByInstantiation) {}

    static ServerDriver& getDriver(CompletionDispatch& dispatch);
    static const Indexer& getIndexer(const CompletionDispatch& dispatch);
    static slang::SourceManager& getSourceManager(CompletionDispatch& dispatch);
    static slang::Bag& getOptions(CompletionDispatch& dispatch);
    static bool resolvesCompletionEdits(const CompletionDispatch& dispatch);
    static void updateCompletionEditText(lsp::CompletionItem& item);

    lsp::Range replacementRange;
    bool followedByCall;
    bool followedByInstantiation;
};

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

    /// The semantic operation and state specific to this completion site.
    std::unique_ptr<CompletionQuery> query;

    /// Determine completion context from a document location and LSP request.
    /// @param doc The document containing the location
    /// @param loc The source location to analyze
    /// @param lspContext The LSP completion-request context
    /// @returns The completion context at that location
    static CompletionContext fromLocation(SlangDoc& doc, slang::SourceLocation loc,
                                          lsp::CompletionContext lspContext = {});
};

} // namespace server
