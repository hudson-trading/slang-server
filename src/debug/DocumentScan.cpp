// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "SlangServer.h"
#include "completions/CompletionContext.h"
#include "util/Converters.h"
#include <algorithm>
#include <fmt/ranges.h>
#include <unordered_set>

#include "slang/syntax/AllSyntax.h"
#include "slang/text/CharInfo.h"

namespace server {

DebugScanResult SlangServer::debugScanDocument(const lsp::TextDocumentIdentifier& document,
                                               const lsp::RequestContext& ctx) {
    ctx.throwIfCancelled("before document scan");
    if (!m_driver->isDocumentOpen(document.uri))
        throw std::runtime_error("slang.debug.scanDocument requires an open document URI");

    auto doc = m_driver->getDocument(document.uri);
    auto analysis = doc->getAnalysis(ctx);
    DebugScanResult result{.uri = document.uri,
                           .positionEncoding = m_client.capabilities.utf8Positions ? "utf-8"
                                                                                   : "utf-16"};
    auto disabled = analysis->syntaxes.disabledRegions;
    std::ranges::sort(disabled, {}, &SourceRange::start);
    size_t disabledIndex = 0;
    std::unordered_set<size_t> seen;
    for (const auto* token : analysis->syntaxes.collected) {
        ctx.throwIfCancelled("during document scan");
        const auto* syntax = analysis->syntaxes.getTokenParent(token);
        if (!syntax || token->isMissing() || token->location().buffer() != doc->getBuffer())
            continue;
        while (disabledIndex < disabled.size() &&
               disabled[disabledIndex].end() <= token->location())
            ++disabledIndex;
        if (disabledIndex < disabled.size() && disabled[disabledIndex].contains(token->location()))
            continue;

        const bool system = token->kind == parsing::TokenKind::SystemIdentifier;
        const bool macro = token->kind == parsing::TokenKind::Directive &&
                           syntax->kind == syntax::SyntaxKind::MacroUsage;
        if (token->kind != parsing::TokenKind::Identifier && !system && !macro)
            continue;
        if (!seen.insert(token->location().offset()).second)
            continue;
        ++result.identifiers;

        // Syntax supplies an expectation even when semantic lookup itself is broken.
        bool reference = system || macro;
        switch (syntax->kind) {
            case syntax::SyntaxKind::IdentifierName:
            case syntax::SyntaxKind::IdentifierSelectName:
            case syntax::SyntaxKind::ClassName:
            case syntax::SyntaxKind::NamedPortConnection:
            case syntax::SyntaxKind::NamedParamAssignment:
            case syntax::SyntaxKind::HierarchyInstantiation:
            case syntax::SyntaxKind::PackageImportItem:
            case syntax::SyntaxKind::MemberAccessExpression:
                reference = true;
                break;
            default:
                break;
        }

        auto position = toPosition(token->location(), sourceManager());
        auto definition = m_driver->getDefinitionInfoAt(document.uri, position);
        if (!reference && !definition)
            continue;

        // System subroutines and built-in methods have no source declaration to navigate to.
        const bool builtin = system || (definition && definition->systemSubroutine()) ||
                             (definition && definition->symbol() &&
                              definition->symbol()->location == SourceLocation::NoLocation);
        const bool declaration = definition && !builtin && !macro &&
                                 definition->nameToken().location() == token->location();
        if (declaration) {
            reference = false;
            ++result.declarations;
        }
        else
            ++result.references;

        auto label = std::string(token->valueText());
        std::vector<std::string> missingFeatures;
        std::vector<std::string> targetNames;
        auto report = [&](std::string code, std::string message, std::string completionKind = "",
                          std::string trigger = "", size_t completionCount = 0) {
            struct Context {
                std::string token;
                std::string syntaxKind;
                std::string role;
                bool resolved;
                std::string completionKind;
                std::string trigger;
                size_t completionCount;
                std::vector<std::string> missingFeatures;
                std::vector<std::string> targetNames;
            };
            auto range = toRange(token->range(), sourceManager());
            if (!m_client.capabilities.utf8Positions) {
                const auto lineStart = token->location().offset() - range.start.character;
                for (auto* endpoint : {&range.start, &range.end}) {
                    auto prefix = doc->getText().substr(lineStart, endpoint->character);
                    endpoint->character = 0;
                    while (!prefix.empty()) {
                        const auto length = std::max(size_t(1), validUtf8SequenceLength(prefix));
                        endpoint->character += length == 4 ? 2 : 1;
                        prefix.remove_prefix(length);
                    }
                }
            }
            result.diagnostics.push_back(lsp::Diagnostic{
                .range = range,
                .severity = lsp::DiagnosticSeverity::Warning,
                .code = std::move(code),
                .source = "slang-server-debug",
                .message = std::move(message),
                .data = rfl::to_generic(Context{label, std::string(toString(syntax->kind)),
                                                declaration ? "declaration" : "reference",
                                                definition.has_value(), completionKind, trigger,
                                                completionCount, missingFeatures, targetNames}),
            });
        };

        ++result.hoverChecks;
        auto hover = m_driver->getDocHover(document.uri, position, false);
        const bool hasHover =
            hover && rfl::visit(
                         [](const auto& contents) {
                             using T = std::decay_t<decltype(contents)>;
                             if constexpr (std::is_same_v<T, lsp::MarkupContent>)
                                 return !contents.value.empty();
                             else if constexpr (std::is_same_v<T, std::vector<lsp::MarkedString>>)
                                 return !contents.empty();
                             else
                                 return true;
                         },
                         hover->contents);
        if (!hasHover)
            missingFeatures.push_back("hover");

        const bool sourcelessMacro =
            definition && definition->macro() && definition->macro()->commandLineDefine() &&
            definition->macro()->commandLineDefine()->defineSourceFile.empty();
        if (!builtin && !sourcelessMacro) {
            ++result.definitionChecks;
            // Navigation also activates design instances; query its target backend directly
            // so a scan preserves the user's selected instance and include context.
            const bool hasDefinition = definition && !definition->getDefinitionLspLinks().empty();
            if (!hasDefinition)
                missingFeatures.push_back("go-to-definition");
        }
        if (!missingFeatures.empty())
            report("missing-hover-goto",
                   fmt::format("Missing {} for '{}'", fmt::join(missingFeatures, " and "), label));

        if (definition) {
            auto checkName = [&](std::string_view name, bool enumRange = false) {
                std::string_view expected = label;
                if (macro && expected.starts_with('`'))
                    expected.remove_prefix(1);
                if (name == expected)
                    return;
                if (enumRange && !name.empty() && expected.starts_with(name)) {
                    auto suffix = expected.substr(name.size());
                    if (!suffix.empty() &&
                        std::ranges::all_of(suffix, [](char c) { return c >= '0' && c <= '9'; }))
                        return;
                }
                if (std::ranges::find(targetNames, name) == targetNames.end())
                    targetNames.emplace_back(name);
            };
            auto checkSymbol = [&](const DefinitionInfo::SymbolTarget& target) {
                checkName(target.symbol->name);
                for (const auto& declaration : target.syntaxes) {
                    const auto* declarator =
                        declaration.node ? declaration.node->as_if<syntax::DeclaratorSyntax>()
                                         : nullptr;
                    checkName(declaration.nameToken.valueText(),
                              target.symbol->kind == ast::SymbolKind::EnumValue && declarator &&
                                  !declarator->dimensions.empty());
                }
            };
            for (const auto& target : definition->targets) {
                std::visit(
                    [&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, DefinitionInfo::SymbolTarget>)
                            checkSymbol(value);
                        else if constexpr (std::is_same_v<T,
                                                          DefinitionInfo::PortConnectionTarget>) {
                            checkSymbol(value.outer);
                            checkSymbol(value.inner);
                        }
                        else
                            checkName(value.nameToken().valueText());
                    },
                    target);
            }
            if (!targetNames.empty())
                report("definition-name-mismatch",
                       fmt::format("Go-to-definition for '{}' targets a different name: {}", label,
                                   fmt::join(targetNames, ", ")));
        }

        if (!reference)
            continue;

        std::optional<std::string> trigger;
        if (macro)
            trigger = "`";
        else if (system)
            trigger = "$";
        else if (const auto* previous = analysis->syntaxes.getTokenBefore(token->location())) {
            switch (previous->kind) {
                case parsing::TokenKind::Dot:
                    trigger = ".";
                    break;
                case parsing::TokenKind::DoubleColon:
                    trigger = ":";
                    break;
                default:
                    break;
            }
        }
        // Macro lists are bounded and filtered by prefix; use the full spelling to avoid
        // diagnosing a valid macro just because it falls beyond the initial result limit.
        auto completionPosition =
            toPosition(macro || system ? token->range().end() : token->location(), sourceManager());
        lsp::CompletionContext lspContext{
            .triggerKind = trigger ? lsp::CompletionTriggerKind::TriggerCharacter
                                   : lsp::CompletionTriggerKind::Invoked,
            .triggerCharacter = trigger,
        };
        auto completionContext = CompletionContext::fromLocation(
            *doc, *doc->getLocation(completionPosition), lspContext);
        ++result.completionChecks;
        auto completions = getDocCompletion(
            {.context = lspContext, .textDocument = document, .position = completionPosition});
        size_t count = 0;
        const bool found = rfl::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return false;
                }
                else {
                    const auto& items = [&]() -> const std::vector<lsp::CompletionItem>& {
                        if constexpr (std::is_same_v<T, lsp::CompletionList>)
                            return value.items;
                        else
                            return value;
                    }();
                    count = items.size();
                    return std::ranges::any_of(items, [&](const auto& item) {
                        return item.label == label;
                    });
                }
            },
            completions);
        if (!found)
            report("missing-completion", fmt::format("Missing completion for '{}'", label),
                   std::string(toString(completionContext.query->kind())),
                   trigger.value_or("Invoked"), count);
    }
    ctx.throwIfCancelled("before publishing document scan");
    m_driver->diagClient->setDebugDiagnostics(document.uri, result.diagnostics);
    return result;
}

} // namespace server
