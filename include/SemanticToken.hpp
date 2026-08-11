#pragma once

#include "lsp/LspTypes.h"

/// @brief Configuration for semantic tokens, defining token types and modifiers
/// Currently just defines a single `comment` token type for inactive regions
const lsp::SemanticTokensLegend SemanticTokensLegendConfig = {.tokenTypes = {"comment"}};
