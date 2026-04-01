//------------------------------------------------------------------------------
// SemanticTokens.h
// Semantic token mapping for the LSP semantic tokens request
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "document/SyntaxIndexer.h"
#include "lsp/LspTypes.h"
#include <optional>
#include <vector>

#include "slang/parsing/TokenKind.h"
#include "slang/text/SourceManager.h"

namespace server {

/// LSP semantic token type indices, matching the legend order.
enum class SemanticTokenType : uint32_t {
    Keyword = 0,
    Variable,
    Function,
    Macro,
    String,
    Number,
    Operator,
};

/// Returns the semantic token legend broadcast during initialization.
lsp::SemanticTokensLegend getSemanticTokensLegend();

/// Maps a slang TokenKind to an LSP semantic token type.
std::optional<SemanticTokenType> mapTokenKind(slang::parsing::TokenKind kind);

/// Computes the LSP delta-encoded semantic token data for a document.
std::vector<uint32_t> computeSemanticTokenData(const SyntaxIndexer& indexer,
                                               const slang::SourceManager& sm);

} // namespace server
