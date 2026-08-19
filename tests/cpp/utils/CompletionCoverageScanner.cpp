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

std::optional<CompletionCoverageScanner::MissingCompletion> CompletionCoverageScanner::checkToken(
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
    auto completions = cursor.getCompletions(trigger);
    auto hasCompletion = std::ranges::any_of(completions, [&](const CompletionHandle& completion) {
        return completion.m_item.label == label;
    });
    if (hasCompletion) {
        return std::nullopt;
    }

    auto line = hdl.m_server.sourceManager().getLineNumber(token.location());
    auto column = hdl.m_server.sourceManager().getColumnNumber(token.location()) - 1;
    return MissingCompletion{.line = static_cast<lsp::uint>(line),
                             .column = static_cast<lsp::uint>(column),
                             .length = static_cast<lsp::uint>(std::max<size_t>(label.size(), 1)),
                             .label = std::move(label),
                             .contextKind = std::string(toString(ctx.kind)),
                             .triggerKind = trigger.value_or("Invoked"),
                             .completionCount = completions.size()};
}

void CompletionCoverageScanner::scanDocument(DocumentHandle hdl,
                                             std::filesystem::path relativePath) {
    auto goldenPath = relativePath;
    goldenPath.replace_extension(".out.sv");
    GoldenTest test(findSlangRoot() / "tests" / "cpp" / "golden" /
                    Catch::getCurrentContext().getResultCapture()->getCurrentTestName() /
                    goldenPath);

    std::vector<MissingCompletion> misses;
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

        if (auto missing = checkToken(hdl, *token)) {
            misses.push_back(std::move(*missing));
        }
    }

    auto missIt = misses.begin();
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

        while (missIt != misses.end() && missIt->line == lineNumber) {
            test.record("//");
            if (missIt->column > 2) {
                test.record(std::string(missIt->column - 2, ' '));
            }
            test.record(std::string(missIt->length, '^'));
            test.record(fmt::format(" MissingCompletion[{}] Context[{}] Trigger[{}] Items[{}]\n",
                                    missIt->label, missIt->contextKind, missIt->triggerKind,
                                    missIt->completionCount));
            ++missIt;
        }

        lineStart = nextLineStart;
        lineNumber++;
    }
}
