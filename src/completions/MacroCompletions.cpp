//------------------------------------------------------------------------------
// MacroCompletions.cpp
// SystemVerilog macro completions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/MacroCompletions.h"

#include "Indexer.h"
#include "document/SlangDoc.h"
#include "lsp/SnippetString.h"
#include "util/Formatting.h"
#include "util/Logging.h"

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"

namespace server::completions {
using namespace slang;

namespace {

lsp::CompletionItem getCompletion(std::string name) {
    auto spelling = "`" + name;
    return lsp::CompletionItem{
        .label = spelling,
        .labelDetails =
            lsp::CompletionItemLabelDetails{
                .detail = " Macro",
            },
        .kind = lsp::CompletionItemKind::Constant,
        .filterText = spelling,
    };
}

std::string getCompletionText(const syntax::DefineDirectiveSyntax& macro) {
    SnippetString output;
    output.appendText("`");
    output.appendText(macro.name.valueText());
    if (macro.formalArguments) {
        output.appendText("(");
        auto& args = macro.formalArguments->args;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0)
                output.appendText(", ");
            output.appendPlaceholder(args[i]->name.valueText());
        }
        output.appendText(")");
    }
    return std::string(output.getValue());
}

void resolve(const syntax::DefineDirectiveSyntax& macro, lsp::CompletionItem& item) {
    item.documentation = svCodeBlock(macro);
    if (!item.insertText) {
        item.insertText = getCompletionText(macro);
        item.insertTextFormat = lsp::InsertTextFormat::Snippet;
    }
}

lsp::CompletionItem getCompletion(const syntax::DefineDirectiveSyntax& macro) {
    auto item = getCompletion(std::string(macro.name.valueText()));
    resolve(macro, item);
    return item;
}

class MacroCompletionQueryImpl final : public MacroCompletionQuery {
public:
    MacroCompletionQueryImpl(lsp::Range replacementRange, std::string typedPrefix,
                             bool followedByCall) :
        MacroCompletionQuery(std::move(replacementRange), followedByCall),
        typedPrefix(std::move(typedPrefix)) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::Macro; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch& dispatch,
                        const std::shared_ptr<SlangDoc>& doc,
                        const CompletionContext&) const final {
        for (auto& macro : doc->getSyntaxTree()->getDefinedMacros()) {
            if (macro->name.location() == SourceLocation::NoLocation)
                continue;
            results.push_back(getCompletion(*macro));
        }

        for (const auto& name : getIndexer(dispatch).getAllMacroNames())
            results.push_back(getCompletion(name));
    }

private:
    std::string typedPrefix;
};

} // namespace

std::unique_ptr<CompletionQuery> MacroCompletionQuery::create(lsp::Range replacementRange,
                                                              std::string typedPrefix,
                                                              bool followedByCall) {
    return std::make_unique<MacroCompletionQueryImpl>(std::move(replacementRange),
                                                      std::move(typedPrefix), followedByCall);
}

void MacroCompletionQuery::resolve(CompletionDispatch& dispatch, lsp::CompletionItem& item) {
    auto paths = getIndexer(dispatch).getFilesForMacro(item.label.substr(1));
    if (paths.empty()) {
        WARN("No macro files found for {}", item.label);
        return;
    }

    auto maybeTree = SyntaxTree::fromFile(paths[0].string(), getSourceManager(dispatch),
                                          getOptions(dispatch));
    if (!maybeTree)
        return;

    for (auto macro : maybeTree.value()->getDefinedMacros()) {
        if (macro->name.valueText() == item.label.substr(1)) {
            completions::resolve(*macro, item);
            updateCompletionEditText(item);
            return;
        }
    }
    WARN("Didn't find macro for {} in {}", item.label, paths[0].string());
}

} // namespace server::completions
