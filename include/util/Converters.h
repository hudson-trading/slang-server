//------------------------------------------------------------------------------
// Converters.h
// Type conversion utilities for LSP server, primarily between slang and LSP types
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <cstddef>
#include <cstdint>
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

std::optional<SourceLocation> toSourceLocation(BufferID buffer, const lsp::Position& position,
                                               const SourceManager& sourceManager);

lsp::Range toRange(const SourceRange& range, const SourceManager& sourceManager);

// Byte offset into `line` of the given UTF-16 code unit column, clamped to the
// first newline or end of string. `line` starts at the column-0 character.
size_t utf16ColumnToByte(std::string_view line, uint32_t character);

lsp::Location toOriginalLocation(const SourceRange& range, const SourceManager& sourceManager);

lsp::Range toRange(const SourceLocation& loc, const SourceManager& sourceManager,
                   const size_t length);

lsp::Location toLocation(const SourceRange& range, const SourceManager& sourceManager);

lsp::Location toLocation(const SourceLocation& loc, const SourceManager& sourceManager);

lsp::SymbolKind toSymbolKind(const slang::syntax::SyntaxKind& kind);

lsp::MarkupContent markdown(std::string& md);

std::string portString(ast::ArgumentDirection dir);

std::string subroutineString(ast::SubroutineKind kind);

} // namespace server
