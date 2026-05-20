#include "Snippets.hpp"
#include "completions/CompletionContext.h"
#include "completions/Completions.h"
#include "lsp/LspTypes.h"

namespace server::completions {

void addModuleMemberKwCompletions(std::vector<lsp::CompletionItem>& results,
                                  const CompletionContext& ctx) {
    for (const auto& snippet : SV_MODULE_MEMBER_SNIPPETS) {
        if (toMask(ctx.kind) & snippet.context) {
            results.emplace_back(lsp::CompletionItem{
                .label = std::string(snippet.label),
                .labelDetails = lsp::CompletionItemLabelDetails{"", std::string(snippet.label)},
                .kind = lsp::CompletionItemKind::Snippet,
                .documentation = lsp::MarkupContent{lsp::MarkupKind{"markdown"},
                                                    std::string(snippet.documentation)},
                .filterText = std::string(snippet.filterText),
                .insertText = std::string(snippet.insertText),
                .insertTextFormat = lsp::InsertTextFormat::Snippet,
            });
        }
    }
}

} // namespace server::completions