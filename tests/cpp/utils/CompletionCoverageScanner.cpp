// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "CompletionCoverageScanner.h"

#include "GoldenTest.h"
#include "ServerHarness.h"
#include "completions/CompletionContext.h"
#include "completions/CompletionDispatch.h"
#include "completions/MemberCompletions.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <ranges>
#include <unordered_set>

#include "slang/parsing/Token.h"

namespace {

std::optional<std::string> triggerForTokenKind(slang::parsing::TokenKind kind) {
    switch (kind) {
        case slang::parsing::TokenKind::Hash:
            return "#";
        case slang::parsing::TokenKind::Dot:
            return ".";
        case slang::parsing::TokenKind::OpenParenthesis:
            return "(";
        case slang::parsing::TokenKind::Colon:
        case slang::parsing::TokenKind::DoubleColon:
            return ":";
        case slang::parsing::TokenKind::OpenBracket:
            return "[";
        case slang::parsing::TokenKind::Dollar:
            return "$";
        default:
            return std::nullopt;
    }
}

} // namespace

std::optional<std::string> CompletionCoverageScanner::triggerForToken(
    const DocumentHandle& hdl, const slang::parsing::Token& token) {
    if (token.location().offset() == 0) {
        return std::nullopt;
    }

    auto prevToken = hdl.doc->getTokenAt(token.location() - 1);
    if (!prevToken) {
        return std::nullopt;
    }

    auto trigger = triggerForTokenKind(prevToken->kind);
    if (!trigger) {
        return std::nullopt;
    }

    const auto& triggerCharacters = server::completions::completionTriggerCharacters();
    if (std::ranges::find(triggerCharacters, *trigger) == triggerCharacters.end()) {
        return std::nullopt;
    }

    return trigger;
}

std::optional<CompletionCoverageScanner::CompletionIssue> CompletionCoverageScanner::checkToken(
    DocumentHandle& hdl, const slang::parsing::Token& token) {
    if (token.kind != slang::parsing::TokenKind::Identifier) {
        return std::nullopt;
    }
    if (token.location().buffer() != hdl.doc->getBuffer()) {
        return std::nullopt;
    }

    auto definition = hdl.getDefinitionInfoAt(static_cast<lsp::uint>(token.location().offset()));
    if (!definition || !definition->symbol()) {
        return std::nullopt;
    }
    if (definition->nameToken().location() == token.location()) {
        return std::nullopt;
    }

    auto label = std::string(token.valueText());
    Cursor cursor(hdl, static_cast<lsp::uint>(token.location().offset()));
    auto trigger = triggerForToken(hdl, token);
    lsp::CompletionContext lspContext{
        .triggerKind = trigger ? lsp::CompletionTriggerKind::TriggerCharacter
                               : lsp::CompletionTriggerKind::Invoked,
        .triggerCharacter = trigger,
    };
    auto ctx = server::CompletionContext::fromLocation(*hdl.doc, token.location(), lspContext);
    auto items = cursor.getCompletions(trigger);
    auto completion = std::ranges::find_if(items, [&](const CompletionHandle& completion) {
        return completion.m_item.label == label;
    });
    if (completion != items.end()) {
        if (trigger != "." || !completion->m_item.data)
            return std::nullopt;

        auto expected = completion->m_item;
        server::completions::MemberCompletionQuery::resolve(*definition->symbol(), expected, true);
        completion->resolve();

        auto documentationText = [](const lsp::CompletionItem& item) -> std::optional<std::string> {
            if (!item.documentation)
                return std::nullopt;
            return rfl::visit(
                [](const auto& documentation) {
                    using T = std::decay_t<decltype(documentation)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        return documentation;
                    else
                        return documentation.value;
                },
                *item.documentation);
        };

        std::string mismatches;
        auto addMismatch = [&](std::string_view field) {
            if (!mismatches.empty())
                mismatches += ",";
            mismatches += field;
        };
        if (documentationText(completion->m_item) != documentationText(expected))
            addMismatch("documentation");
        if (completion->m_item.insertText != expected.insertText)
            addMismatch("insertText");
        if (completion->m_item.insertTextFormat != expected.insertTextFormat)
            addMismatch("insertTextFormat");

        if (mismatches.empty())
            return std::nullopt;

        auto line = hdl.m_server.sourceManager().getLineNumber(token.location());
        auto column = hdl.m_server.sourceManager().getColumnNumber(token.location()) - 1;
        return CompletionIssue{
            .line = static_cast<lsp::uint>(line),
            .column = static_cast<lsp::uint>(column),
            .length = static_cast<lsp::uint>(std::max<size_t>(label.size(), 1)),
            .label = std::move(label),
            .contextKind = std::string(toString(ctx.kind)),
            .triggerKind = *trigger,
            .completionCount = items.size(),
            .resolutionMismatch = std::move(mismatches),
        };
    }

    auto line = hdl.m_server.sourceManager().getLineNumber(token.location());
    auto column = hdl.m_server.sourceManager().getColumnNumber(token.location()) - 1;
    return CompletionIssue{.line = static_cast<lsp::uint>(line),
                           .column = static_cast<lsp::uint>(column),
                           .length = static_cast<lsp::uint>(std::max<size_t>(label.size(), 1)),
                           .label = std::move(label),
                           .contextKind = std::string(toString(ctx.kind)),
                           .triggerKind = trigger.value_or("Invoked"),
                           .completionCount = items.size(),
                           .resolutionMismatch = std::nullopt};
}

void CompletionCoverageScanner::scanDocument(DocumentHandle hdl,
                                             std::filesystem::path relativePath) {
    auto goldenPath = relativePath;
    goldenPath.replace_extension(".out.sv");
    GoldenTest test(findSlangRoot() / "tests" / "cpp" / "golden" /
                    Catch::getCurrentContext().getResultCapture()->getCurrentTestName() /
                    goldenPath);

    std::vector<CompletionIssue> issues;
    std::unordered_set<size_t> seenTokenOffsets;
    auto text = hdl.getText();
    for (lsp::uint offset = 0; offset < text.size(); offset++) {
        auto loc = hdl.getLocation(offset);
        if (!loc) {
            continue;
        }

        auto token = hdl.doc->getWordTokenAt(*loc);
        if (!token || token->location() == slang::SourceLocation::NoLocation ||
            !seenTokenOffsets.insert(token->location().offset()).second) {
            continue;
        }

        if (auto issue = checkToken(hdl, *token)) {
            issues.push_back(std::move(*issue));
        }
    }

    auto issueIt = issues.begin();
    lsp::uint lineNumber = 1;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        auto lineEnd = text.find('\n', lineStart);
        auto hasNewline = lineEnd != std::string::npos;
        auto nextLineStart = hasNewline ? lineEnd + 1 : text.size();
        test.record(std::string_view(text).substr(lineStart, nextLineStart - lineStart));
        if (!hasNewline) {
            test.record("\n");
        }

        while (issueIt != issues.end() && issueIt->line == lineNumber) {
            test.record("//");
            if (issueIt->column > 2) {
                test.record(std::string(issueIt->column - 2, ' '));
            }
            test.record(std::string(issueIt->length, '^'));
            if (issueIt->resolutionMismatch) {
                test.record(fmt::format(
                    " MismatchedCompletion[{}] Fields[{}] Context[{}] Trigger[{}] Items[{}]\n",
                    issueIt->label, *issueIt->resolutionMismatch, issueIt->contextKind,
                    issueIt->triggerKind, issueIt->completionCount));
            }
            else {
                test.record(fmt::format(
                    " MissingCompletion[{}] Context[{}] Trigger[{}] Items[{}]\n", issueIt->label,
                    issueIt->contextKind, issueIt->triggerKind, issueIt->completionCount));
            }
            ++issueIt;
        }

        lineStart = nextLineStart;
        lineNumber++;
    }
}
