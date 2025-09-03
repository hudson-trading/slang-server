//------------------------------------------------------------------------------
// Converters.h
// Type conversion utilities for LSP server, primarily between slang and LSP types
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <optional>
#include <string_view>

#include "slang/ast/SemanticFacts.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {

using namespace slang;

std::optional<const parsing::Token> findNameToken(const syntax::SyntaxNode* node,
                                                  std::string_view name);

lsp::Position toPosition(const SourceLocation& loc, const SourceManager& sourceManager);

lsp::Range toRange(const SourceRange& range, const SourceManager& sourceManager);

lsp::Location toLocation(const SourceRange& range, const SourceManager& sourceManager);

lsp::Location toLocation(const SourceLocation& loc, const SourceManager& sourceManager);

lsp::MarkupContent markdown(std::string& md);

std::string portString(ast::ArgumentDirection dir);

std::string subroutineString(ast::SubroutineKind kind);

} // namespace server