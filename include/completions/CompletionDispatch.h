//------------------------------------------------------------------------------
// CompletionDispatch.h
// Dispatch controller for LSP completion requests and responses
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "Indexer.h"
#include "completions/CompletionContext.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "lsp/RequestContext.h"
#include <memory>
#include <string>
#include <vector>

#include "slang/text/SourceLocation.h"
#include "slang/util/Bag.h"

namespace server {

class ServerDriver;

class CompletionDispatch {
private:
    friend class CompletionQuery;

    // May need to retrieve additional documents
    ServerDriver& m_driver;
    const Indexer& m_indexer;
    SourceManager& m_sourceManager;
    slang::Bag& m_options;

public:
    bool resolveEdits = false;

    CompletionDispatch(ServerDriver& driver, const Indexer& indexer, SourceManager& sourceManager,
                       slang::Bag& options);

    /// Top-level completion entry point. The semantic target is derived from source around the
    /// cursor; trigger characters only control when clients invoke this method.
    void getCompletions(std::vector<lsp::CompletionItem>& results, std::shared_ptr<SlangDoc> doc,
                        const CompletionContext& ctx);

    void getCompletionItemResolve(lsp::CompletionItem& item, const lsp::RequestContext& ctx);
};

namespace completions {

/// Characters that clients should use to trigger completion requests.
const std::vector<std::string>& completionTriggerCharacters();

} // namespace completions

} // namespace server
