//------------------------------------------------------------------------------
// KeywordCompletions.cpp
// SystemVerilog keyword completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/KeywordCompletions.h"

#include <array>
#include <string_view>

namespace server::completions {

void addKeywordCompletions(std::vector<lsp::CompletionItem>& results) {
    static constexpr std::array moduleMemberKeywords{"logic", "assign", "wire", "reg"};
    for (std::string_view keyword : moduleMemberKeywords) {
        results.push_back(lsp::CompletionItem{
            .label = std::string(keyword),
            .kind = lsp::CompletionItemKind::Keyword,
            .documentation = std::nullopt,
            .filterText = std::string(keyword),
        });
    }

    results.push_back(lsp::CompletionItem{
        .label = "always_ff",
        .kind = lsp::CompletionItemKind::Snippet,
        .documentation = std::nullopt,
        .filterText = "always_ff",
        .insertText = "always_ff @($0) begin\n\t\nend",
        .insertTextFormat = lsp::InsertTextFormat::Snippet,
    });
    results.push_back(lsp::CompletionItem{
        .label = "always_comb",
        .kind = lsp::CompletionItemKind::Snippet,
        .documentation = std::nullopt,
        .filterText = "always_comb",
        .insertText = "always_comb begin\n\t$0\nend",
        .insertTextFormat = lsp::InsertTextFormat::Snippet,
    });
    results.push_back(lsp::CompletionItem{
        .label = "always_latch",
        .kind = lsp::CompletionItemKind::Snippet,
        .documentation = std::nullopt,
        .filterText = "always_latch",
        .insertText = "always_latch begin\n\t$0\nend",
        .insertTextFormat = lsp::InsertTextFormat::Snippet,
    });
}

} // namespace server::completions
