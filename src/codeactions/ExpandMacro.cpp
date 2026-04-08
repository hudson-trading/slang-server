//------------------------------------------------------------------------------
// ExpandMacro.cpp
// Code action to expand a macro usage inline
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "codeactions/ExpandMacro.h"

#include "util/Converters.h"

namespace server::codeactions {
using namespace slang;

/// Re-indent a multi-line expansion to match the usage site's column.
/// Preserves relative indentation within the block.
static std::string reindent(const std::string& text, lsp::uint column) {
    if (text.find('\n') == std::string::npos)
        return text;

    std::vector<std::string_view> lines;
    std::string_view remaining = text;
    while (!remaining.empty()) {
        auto nl = remaining.find('\n');
        lines.push_back(remaining.substr(0, nl));
        remaining = nl == std::string_view::npos ? "" : remaining.substr(nl + 1);
    }

    // Find minimum indent of non-empty lines after the first
    size_t minIndent = std::string_view::npos;
    for (size_t i = 1; i < lines.size(); i++) {
        auto pos = lines[i].find_first_not_of(" \t");
        if (pos != std::string_view::npos && pos < minIndent)
            minIndent = pos;
    }
    if (minIndent == std::string_view::npos)
        minIndent = 0;

    std::string indent(column, ' ');
    std::string result(lines[0]);
    for (size_t i = 1; i < lines.size(); i++) {
        result += '\n';
        auto pos = lines[i].find_first_not_of(" \t");
        if (pos == std::string_view::npos) {
            result += indent;
        }
        else {
            result += indent;
            if (pos > minIndent)
                result += std::string(pos - minIndent, ' ');
            result += lines[i].substr(pos);
        }
    }
    return result;
}

void addExpandMacroAction(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                          const CodeActionContext& ctx) {
    auto it = ctx.analysis.syntaxes.macroExpansions.find(ctx.syntax);
    if (it == ctx.analysis.syntaxes.macroExpansions.end())
        return;

    auto usageRange = toRange(ctx.syntax->sourceRange(), ctx.sourceManager);
    auto expandedText = reindent(it->second.getText(), usageRange.start.character);

    std::unordered_map<std::string, std::vector<lsp::TextEdit>> changes;
    changes[ctx.params.textDocument.uri.str()].push_back(
        lsp::TextEdit{.range = usageRange, .newText = std::move(expandedText)});

    results.push_back(lsp::CodeAction{
        .title = "Expand macro",
        .kind = lsp::CodeActionKind::from_name<"refactor">(),
        .edit = lsp::WorkspaceEdit{.changes = std::move(changes)},
    });
}

} // namespace server::codeactions
