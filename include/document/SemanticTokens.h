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
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "slang/parsing/TokenKind.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Enum.h"

namespace server {

// clang-format off
#define SEMANTIC_TOKEN_TYPE(x) \
    x(Keyword) \
    x(Variable) \
    x(Function) \
    x(Macro) \
    x(String) \
    x(Number) \
    x(Operator)
SLANG_ENUM_SIZED(SemanticTokenType, uint32_t, SEMANTIC_TOKEN_TYPE)

/// Compile-time LSP token type names (same strings as @a toString(SemanticTokenType)).
[[nodiscard]] constexpr std::array<std::string_view, SemanticTokenType_traits::values.size()>
getSemanticTokensLegendTypes() noexcept {
    return {SEMANTIC_TOKEN_TYPE(SLANG_UTIL_ENUM_STRING)};
}
#undef SEMANTIC_TOKEN_TYPE
// clang-format on

/// Returns the semantic token legend broadcast during initialization. Token type strings are
/// lowercase for LSP (derived from @a toString(SemanticTokenType)).
[[nodiscard]] inline lsp::SemanticTokensLegend getSemanticTokensLegend() {
    lsp::SemanticTokensLegend legend{.tokenModifiers = {}};
    legend.tokenTypes.reserve(SemanticTokenType_traits::values.size());
    for (SemanticTokenType const t : SemanticTokenType_traits::values) {
        std::string name(toString(t));
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        legend.tokenTypes.emplace_back(std::move(name));
    }
    return legend;
}

/// Maps a slang TokenKind to an LSP semantic token type.
std::optional<SemanticTokenType> mapTokenKind(slang::parsing::TokenKind kind);

/// Computes the LSP delta-encoded semantic token data for a document.
std::vector<uint32_t> computeSemanticTokenData(const SyntaxIndexer& indexer,
                                               const slang::SourceManager& sm);

} // namespace server
