//------------------------------------------------------------------------------
// AssignmentPatternCompletions.cpp
// Assignment pattern completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/AssignmentPatternCompletions.h"

#include "completions/MemberCompletions.h"
#include "document/ShallowAnalysis.h"
#include "document/SlangDoc.h"
#include "lsp/SnippetString.h"
#include "util/Converters.h"
#include "util/SlangExtensions.h"
#include <unordered_set>

#include "slang/ast/types/AllTypes.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxFacts.h"

namespace server::completions {
using namespace slang;

namespace {

const syntax::AssignmentPatternExpressionSyntax* findPatternExpression(
    const CompletionContext& context, SourceLocation cursor) {
    auto* syntax = context.analysis->syntaxes.getSyntaxAt(cursor);
    if (!syntax) {
        auto* previous = context.analysis->syntaxes.getTokenBefore(cursor);
        syntax = context.analysis->syntaxes.getTokenParent(previous);
    }
    for (; syntax; syntax = syntax->parent) {
        if (auto* expression = syntax->as_if<syntax::AssignmentPatternExpressionSyntax>())
            return expression;
    }
    return nullptr;
}

class StructAssignCompletionQueryImpl final : public StructAssignCompletionQuery {
public:
    StructAssignCompletionQueryImpl(lsp::Range replacementRange, SourceLocation cursor) :
        StructAssignCompletionQuery(replacementRange), cursor(cursor) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::StructAssign; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch&,
                        const std::shared_ptr<SlangDoc>&,
                        const CompletionContext& context) const final {
        auto* targetScope = context.analysis->getAssignmentPatternCompletionScope(cursor);
        if (!targetScope)
            return;

        auto* patternExpression = findPatternExpression(context, cursor);
        if (!patternExpression)
            return;
        auto* simplePattern =
            patternExpression->pattern->as_if<syntax::SimpleAssignmentPatternSyntax>();
        if (!simplePattern || !std::ranges::all_of(simplePattern->items, [](const auto* item) {
                return item->getFirstToken().isMissing();
            })) {
            return;
        }

        auto* targetType = targetScope->asSymbol().as_if<ast::Type>();
        if (!targetType || !unwrapErrorType(*targetType).isStruct())
            return;

        SnippetString snippet;
        auto& sourceManager = context.analysis->getSourceManager();
        auto position = toPosition(cursor, sourceManager);
        snippet.appendText("\n\t");

        // Preserve delimiters absorbed by the parser while the pattern is incomplete.
        auto needsComma = [&]() {
            auto* item =
                patternExpression->parent
                    ? patternExpression->parent->as_if<syntax::AssignmentPatternItemSyntax>()
                    : nullptr;
            auto* outerPattern =
                item && item->parent
                    ? item->parent->as_if<syntax::StructuredAssignmentPatternSyntax>()
                    : nullptr;
            return item && item->expr.get() == patternExpression && outerPattern &&
                   outerPattern->closeBrace.isMissing();
        }();
        auto needsSemicolon = [&]() {
            auto* parent = patternExpression->parent.get();
            if (!parent)
                return false;
            if (auto* assignment = parent->as_if<syntax::BinaryExpressionSyntax>();
                assignment && assignment->right.get() == patternExpression &&
                syntax::SyntaxFacts::isAssignmentOperator(assignment->kind)) {
                parent = assignment->parent.get();
                if (!parent)
                    return false;
                if (auto* continuous = parent->as_if<syntax::ContinuousAssignSyntax>())
                    return continuous->semi.isMissing();
                if (auto* statement = parent->as_if<syntax::ExpressionStatementSyntax>())
                    return statement->semi.isMissing();
                return false;
            }

            auto* value = parent->as_if<syntax::EqualsValueClauseSyntax>();
            if (!value || value->expr.get() != patternExpression || !value->parent)
                return false;
            auto* declarator = value->parent->as_if<syntax::DeclaratorSyntax>();
            if (!declarator || !declarator->parent)
                return false;
            parent = declarator->parent.get();
            if (auto* declaration = parent->as_if<syntax::DataDeclarationSyntax>())
                return declaration->semi.isMissing();
            if (auto* declaration = parent->as_if<syntax::NetDeclarationSyntax>())
                return declaration->semi.isMissing();
            if (auto* declaration = parent->as_if<syntax::LocalVariableDeclarationSyntax>())
                return declaration->semi.isMissing();
            return false;
        }();
        std::string_view trailingDelimiter;
        if (needsComma)
            trailingDelimiter = ",";
        else if (needsSemicolon)
            trailingDelimiter = ";";

        bool first = true;
        std::string label = "'{";
        for (auto& member : targetScope->members()) {
            if (!ast::FieldSymbol::isKind(member.kind) || member.name.empty())
                continue;
            if (!first) {
                snippet.appendText(",\n\t");
                label += ", ";
            }
            first = false;
            snippet.appendText(member.name).appendText(": ").appendTabstop();
            label += member.name;
        }
        if (first)
            return;
        label += "}";

        auto closeBraceMissing = simplePattern->closeBrace.isMissing();
        if (closeBraceMissing ||
            toPosition(simplePattern->closeBrace.location(), sourceManager).line == position.line) {
            snippet.appendText("\n");
            if (closeBraceMissing) {
                snippet.appendText("}");
                snippet.appendText(trailingDelimiter);
            }
        }
        lsp::CompletionItem item{
            .label = label,
            .kind = lsp::CompletionItemKind::Snippet,
            .sortText = "0",
            .filterText = label,
            .insertText = std::string(snippet.getValue()),
            .insertTextFormat = lsp::InsertTextFormat::Snippet,
            .insertTextMode = lsp::InsertTextMode::adjustIndentation,
        };
        auto* tokenAfterClose = closeBraceMissing ? nullptr
                                                  : context.analysis->syntaxes.getTokenAfter(
                                                        simplePattern->closeBrace.range().end());
        auto hasTrailingDelimiter =
            tokenAfterClose && !tokenAfterClose->isMissing() &&
            ((trailingDelimiter == "," && tokenAfterClose->kind == parsing::TokenKind::Comma) ||
             (trailingDelimiter == ";" && tokenAfterClose->kind == parsing::TokenKind::Semicolon));
        if (!trailingDelimiter.empty() && !closeBraceMissing && !hasTrailingDelimiter) {
            auto insertPosition = toPosition(simplePattern->closeBrace.range().end(),
                                             sourceManager);
            item.additionalTextEdits = std::vector<lsp::TextEdit>{lsp::TextEdit{
                .range = lsp::Range{.start = insertPosition, .end = insertPosition},
                .newText = std::string(trailingDelimiter),
            }};
        }
        results.push_back(std::move(item));
    }

private:
    SourceLocation cursor;
};

class StructMemberCompletionQueryImpl final : public StructMemberCompletionQuery {
public:
    StructMemberCompletionQueryImpl(lsp::Range replacementRange, SourceLocation cursor,
                                    bool followedByColon) :
        StructMemberCompletionQuery(replacementRange), cursor(cursor),
        followedByColon(followedByColon) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::StructMember; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch& dispatch,
                        const std::shared_ptr<SlangDoc>& doc,
                        const CompletionContext& context) const final {
        auto* targetScope = context.analysis->getAssignmentPatternCompletionScope(cursor);
        if (!targetScope)
            return;

        auto* patternExpression = findPatternExpression(context, cursor);

        // Avoid suggesting keys already present elsewhere in a structured pattern.
        std::unordered_set<std::string_view> existingFields;
        bool hasDefault = false;
        if (patternExpression) {
            if (auto* pattern = patternExpression->pattern
                                    ->as_if<syntax::StructuredAssignmentPatternSyntax>()) {
                for (auto* item : pattern->items) {
                    auto range = item->key->sourceRange();
                    if (range.start() <= cursor && cursor <= range.end())
                        continue;

                    if (item->key->kind == syntax::SyntaxKind::DefaultPatternKeyExpression) {
                        hasDefault = true;
                        continue;
                    }

                    auto* key = item->key->as_if<syntax::IdentifierNameSyntax>();
                    if (!key || key->identifier.isMissing())
                        continue;
                    existingFields.emplace(key->identifier.valueText());
                }
            }
        }

        for (auto& member : targetScope->members()) {
            if (!ast::FieldSymbol::isKind(member.kind) || member.name.empty() ||
                existingFields.contains(member.name)) {
                continue;
            }

            auto item = getHierarchicalCompletion(targetScope->asSymbol(), member,
                                                  doc->getURI().str(), false,
                                                  resolvesCompletionEdits(dispatch));
            item.kind = lsp::CompletionItemKind::Field;
            if (!followedByColon) {
                SnippetString snippet;
                snippet.appendText(member.name).appendText(": ").appendTabstop();
                item.insertText = snippet.getValue();
                item.insertTextFormat = lsp::InsertTextFormat::Snippet;
            }
            results.push_back(std::move(item));
        }

        if (!hasDefault) {
            lsp::CompletionItem item{
                .label = "default",
                .kind = lsp::CompletionItemKind::Keyword,
            };
            if (!followedByColon) {
                SnippetString snippet;
                snippet.appendText("default: ").appendTabstop();
                item.insertText = snippet.getValue();
                item.insertTextFormat = lsp::InsertTextFormat::Snippet;
            }
            results.push_back(std::move(item));
        }
    }

private:
    SourceLocation cursor;
    bool followedByColon;
};

} // namespace

std::unique_ptr<CompletionQuery> StructAssignCompletionQuery::create(lsp::Range replacementRange,
                                                                     SourceLocation cursor) {
    return std::make_unique<StructAssignCompletionQueryImpl>(std::move(replacementRange), cursor);
}

std::unique_ptr<CompletionQuery> StructMemberCompletionQuery::create(lsp::Range replacementRange,
                                                                     SourceLocation cursor,
                                                                     bool followedByColon) {
    return std::make_unique<StructMemberCompletionQueryImpl>(std::move(replacementRange), cursor,
                                                             followedByColon);
}

} // namespace server::completions
