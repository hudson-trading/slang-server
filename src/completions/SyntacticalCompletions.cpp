#include "Snippets.hpp"
#include "completions/Completions.h"
#include "lsp/LspTypes.h"

namespace server::completions {

void addModuleMemberKwCompletions(std::vector<lsp::CompletionItem>& results) {
    for (const auto& snippet : SV_MODULE_MEMBER_SNIPPETS) {
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

} // namespace server::completions