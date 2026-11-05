//------------------------------------------------------------------------------
// CodeActionDispatch.h
// Dispatches code action requests to individual action providers
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "document/ShallowAnalysis.h"
#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include <rfl/Variant.hpp>
#include <vector>

namespace slang {
class SourceManager;
}

struct Indexer;

namespace server {

class ServerDriver;

/// Context passed to individual code action providers, built by the dispatcher.
struct CodeActionContext {
    const lsp::CodeActionParams& params;
    SlangDoc& doc;
    ShallowAnalysis& analysis;
    const slang::SourceManager& sourceManager;
    /// Workspace declarations and include candidates.
    const Indexer& indexer;

    /// Configured preprocessing options used to choose resolvable include spellings.
    const slang::Bag& configuredOptions;

    /// Token at cursor (may be null)
    const slang::parsing::Token* token;
    /// Parent syntax of the token (may be null)
    const slang::syntax::SyntaxNode* syntax;
    /// Diagnostics at the cursor range (from the request)
    const std::vector<lsp::Diagnostic>& diagnostics;
};

class CodeActionDispatch {
    ServerDriver& m_driver;
    slang::SourceManager& m_sourceManager;
    /// Workspace index used to find headers for unresolved names.
    const Indexer& m_indexer;

public:
    CodeActionDispatch(ServerDriver& driver, slang::SourceManager& sm, const Indexer& indexer);

    std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>> getCodeActions(
        const lsp::CodeActionParams& params);
};

} // namespace server
