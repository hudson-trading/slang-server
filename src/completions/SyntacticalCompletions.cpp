#include "Snippets.hpp"
#include "completions/Completions.h"
#include <string_view>

namespace server::completions {

void addModuleMemberKwCompletions(std::vector<lsp::CompletionItem>& results) {
    for (const auto& snippet : SV_MODULE_MEMBER_SNIPPETS) {
        results.emplace_back(lsp::CompletionItem{
            .label = std::string(snippet.label),
            .kind = lsp::CompletionItemKind::Snippet,
            .documentation = snippet.documentation.empty()
                                 ? std::nullopt
                                 : std::make_optional(std::string(snippet.documentation)),
            .filterText = std::string(snippet.filterText),
            .insertText = std::string(snippet.insertText),
            .insertTextFormat = lsp::InsertTextFormat::Snippet,
        });
    }
}

} // namespace server::completions