//------------------------------------------------------------------------------
// CompletionDispatch.cpp
// Completion site classification and shared query dispatch.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/CompletionDispatch.h"

#include "completions/InstanceCompletions.h"
#include "completions/MacroCompletions.h"
#include "completions/MemberCompletions.h"
#include "completions/SystemTaskCompletions.h"
#include "document/ShallowAnalysis.h"
#include "lsp/SnippetString.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include <algorithm>
#include <string>

#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"

namespace server {

namespace completions {

const std::vector<std::string>& completionTriggerCharacters() {
    static const std::vector<std::string> triggerCharacters{
        "`", // macros
        "#", // hierarchical instantiation: modules and interfaces
        ".", // hierarchical references
        "(", // function calls
        ":", // package scope (::), wire width
        "[", // wire width, array indexing
        "$", // system tasks and functions
    };
    return triggerCharacters;
}

} // namespace completions

namespace {

struct CompletionSite {
    lsp::Range replacementRange;
    const slang::parsing::Token* targetToken = nullptr;
    const slang::parsing::Token* tokenBefore = nullptr;
    const slang::parsing::Token* tokenAfter = nullptr;
    std::string typedPrefix;
};

CompletionSite getCompletionSite(const SlangDoc& doc, const ShallowAnalysis& analysis,
                                 slang::SourceLocation cursor) {
    using slang::parsing::TokenKind;

    // Probe the previous byte because a cursor at a token's end can fall outside token lookup.
    auto* targetToken = analysis.getWordTokenAt(cursor);
    if (!targetToken && cursor.offset() > 0)
        targetToken = analysis.getWordTokenAt(cursor - 1);

    // The fallback probe is valid only within the token or exactly at its end.
    if (targetToken &&
        (cursor < targetToken->range().start() || targetToken->range().end() < cursor)) {
        targetToken = nullptr;
    }

    // Replace the whole token so completion in its middle also removes the existing suffix.
    auto start = cursor;
    auto end = cursor;
    if (targetToken) {
        start = targetToken->range().start();
        end = targetToken->range().end();
    }

    auto* tokenBefore = analysis.syntaxes.getTokenBefore(start);
    if (!targetToken) {
        // Standalone `$` and backtick markers are not words but belong to the replacement range.
        auto* marker = analysis.syntaxes.getTokenBefore(cursor);
        if (marker && marker->range().end() == cursor &&
            (marker->kind == TokenKind::Dollar || marker->rawText() == "`")) {
            targetToken = marker;
            start = marker->range().start();
            end = marker->range().end();
            tokenBefore = analysis.syntaxes.getTokenBefore(start);
        }
    }

    // Filtering uses only text before the cursor even though replacement spans the whole token.
    std::string typedPrefix;
    if (targetToken) {
        auto length = std::min<size_t>(cursor.offset() - start.offset(),
                                       targetToken->rawText().size());
        typedPrefix = targetToken->rawText().substr(0, length);
    }

    // Neighboring tokens are measured outside the replacement range for query classification.
    return CompletionSite{
        .replacementRange = lsp::Range{.start = toPosition(start, doc.getSourceManager()),
                                       .end = toPosition(end, doc.getSourceManager())},
        .targetToken = targetToken,
        .tokenBefore = tokenBefore,
        .tokenAfter = analysis.syntaxes.getTokenAfter(end),
        .typedPrefix = std::move(typedPrefix),
    };
}

bool isSeparatedOnlyByWhitespace(const slang::parsing::Token& token) {
    return std::ranges::all_of(token.trivia(), [](const slang::parsing::Trivia& trivia) {
        return trivia.kind == slang::parsing::TriviaKind::Whitespace ||
               trivia.kind == slang::parsing::TriviaKind::EndOfLine;
    });
}

void setCompletionEdit(lsp::CompletionItem& item, const lsp::Range& replacementRange,
                       bool followedByCall, bool followedByInstantiation) {
    // Existing calls and instances still resolve documentation but retain their source shape.
    if ((followedByCall && item.kind == lsp::CompletionItemKind::Constant) ||
        (followedByInstantiation && item.kind == lsp::CompletionItemKind::Module)) {
        item.insertText = item.label;
        item.insertTextFormat = lsp::InsertTextFormat::PlainText;
    }

    auto newText = item.insertText.value_or(item.label);
    auto useLabelOnly = (followedByCall &&
                         item.insertTextFormat == lsp::InsertTextFormat::Snippet) ||
                        (followedByInstantiation && item.kind == lsp::CompletionItemKind::Module);
    if (useLabelOnly && item.insertTextFormat == lsp::InsertTextFormat::Snippet) {
        SnippetString escapedLabel;
        escapedLabel.appendText(item.label);
        newText = escapedLabel.getValue();
    }
    else if (useLabelOnly) {
        newText = item.label;
    }
    item.textEdit = lsp::TextEdit{.range = replacementRange, .newText = std::move(newText)};
}

} // namespace

std::unique_ptr<CompletionQuery> CompletionQuery::fromLocation(
    const SlangDoc& doc, const std::shared_ptr<ShallowAnalysis>& analysis,
    slang::SourceLocation cursor) {
    using slang::parsing::TokenKind;

    auto site = getCompletionSite(doc, *analysis, cursor);
    auto followedByCall = site.tokenAfter && site.tokenAfter->kind == TokenKind::OpenParenthesis &&
                          isSeparatedOnlyByWhitespace(*site.tokenAfter);
    auto followedByInstantiation = site.tokenAfter &&
                                   isSeparatedOnlyByWhitespace(*site.tokenAfter) &&
                                   (site.tokenAfter->kind == TokenKind::Identifier ||
                                    site.tokenAfter->kind == TokenKind::Hash);

    if (site.targetToken && site.targetToken->rawText().starts_with('$')) {
        return completions::SystemSubroutineCompletionQuery::create(
            std::move(site.replacementRange), followedByCall);
    }
    if (site.targetToken && site.targetToken->rawText().starts_with('`')) {
        return completions::MacroCompletionQuery::create(std::move(site.replacementRange),
                                                         std::move(site.typedPrefix),
                                                         followedByCall);
    }
    if (site.tokenBefore && site.tokenBefore->kind == TokenKind::DoubleColon) {
        return completions::MemberCompletionQuery::createScopedAccess(
            std::move(site.replacementRange),
            analysis->syntaxes.getTokenBefore(site.tokenBefore->location()), followedByCall);
    }
    if (site.tokenBefore && site.tokenBefore->kind == TokenKind::Dot) {
        return completions::MemberCompletionQuery::createMemberAccess(
            std::move(site.replacementRange),
            analysis->syntaxes.getTokenBefore(site.tokenBefore->location()), followedByCall);
    }
    if (!site.targetToken && site.tokenBefore && site.tokenBefore->kind == TokenKind::Hash) {
        return completions::InstanceCompletionQuery::create(std::move(site.replacementRange),
                                                            analysis->syntaxes.getTokenBefore(
                                                                site.tokenBefore->location()));
    }

    return completions::MemberCompletionQuery::createLexical(std::move(site.replacementRange),
                                                             followedByCall,
                                                             followedByInstantiation);
}

void CompletionQuery::setCompletionEdit(lsp::CompletionItem& item) const {
    server::setCompletionEdit(item, replacementRange, followedByCall, followedByInstantiation);
}

ServerDriver& CompletionQuery::getDriver(CompletionDispatch& dispatch) {
    return dispatch.m_driver;
}

const Indexer& CompletionQuery::getIndexer(const CompletionDispatch& dispatch) {
    return dispatch.m_indexer;
}

slang::SourceManager& CompletionQuery::getSourceManager(CompletionDispatch& dispatch) {
    return dispatch.m_sourceManager;
}

slang::Bag& CompletionQuery::getOptions(CompletionDispatch& dispatch) {
    return dispatch.m_options;
}

bool CompletionQuery::resolvesCompletionEdits(const CompletionDispatch& dispatch) {
    return dispatch.resolveEdits;
}

void CompletionQuery::updateCompletionEditText(lsp::CompletionItem& item) {
    if (!item.insertText || !item.textEdit)
        return;

    if (rfl::holds_alternative<lsp::TextEdit>(*item.textEdit))
        rfl::get<lsp::TextEdit>(*item.textEdit).newText = *item.insertText;
    else
        rfl::get<lsp::InsertReplaceEdit>(*item.textEdit).newText = *item.insertText;
}

CompletionDispatch::CompletionDispatch(ServerDriver& driver, const Indexer& indexer,
                                       SourceManager& sourceManager, slang::Bag& options) :
    m_driver(driver), m_indexer(indexer), m_sourceManager(sourceManager), m_options(options) {
}

void CompletionDispatch::getCompletions(std::vector<lsp::CompletionItem>& results,
                                        std::shared_ptr<SlangDoc> doc,
                                        const CompletionContext& context) {
    SLANG_ASSERT(context.query);
    context.query->getCompletions(results, *this, doc, context);

    for (auto& item : results)
        context.query->setCompletionEdit(item);

    INFO("Returning {} completions for {} query in {} context", results.size(),
         toString(context.query->kind()), toString(context.kind));
}

void CompletionDispatch::getCompletionItemResolve(lsp::CompletionItem& item) {
    INFO("Resolving completion item: {}", item.label);
    if (!item.label.empty() && item.label[0] == '$')
        return;

    switch (*item.kind) {
        case lsp::CompletionItemKind::Constant:
            completions::MacroCompletionQuery::resolve(*this, item);
            break;
        case lsp::CompletionItemKind::Module:
            completions::InstanceCompletionQuery::resolve(*this, item);
            break;
        default:
            completions::MemberCompletionQuery::resolve(*this, item);
            break;
    }
}

} // namespace server
