//------------------------------------------------------------------------------
// CompletionDispatch.h
// Dispatch controller for LSP completion requests and responses
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "Indexer.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include <filesystem>
#include <memory>
#include <optional>

#include "slang/text/SourceLocation.h"
#include "slang/util/Bag.h"

namespace server {

class CompletionDispatch {
private:
    const Indexer& m_indexer;
    SourceManager& m_sourceManager;
    slang::Bag& m_options;

    /// Last open document, used to store context for completion resolution
    std::shared_ptr<SlangDoc> m_lastDoc;

    // name of last scope
    std::string m_lastScope;

public:
    CompletionDispatch(const Indexer& indexer, SourceManager& sourceManager, slang::Bag& options);

    void getInvokedCompletions(std::vector<lsp::CompletionItem>& results,
                               std::shared_ptr<SlangDoc> doc, bool isLhs,
                               slang::SourceLocation loc);

    void getTriggerCompletions(char triggerChar, char prevChar, std::shared_ptr<SlangDoc> doc,
                               slang::SourceLocation loc,
                               std::vector<lsp::CompletionItem>& results);

    void resolveModuleCompletion(lsp::CompletionItem& item,
                                 std::optional<std::filesystem::path> modulePath = std::nullopt,
                                 bool excludeName = false);

    void resolveMacroCompletion(lsp::CompletionItem& item);

    void getCompletionItemResolve(lsp::CompletionItem& item);
};

} // namespace server