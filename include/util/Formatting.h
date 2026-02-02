// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "lsp/LspTypes.h"
#include <string>
#include <string_view>

#include "slang/syntax/SyntaxNode.h"

namespace slang {
class ConstantValue;
} // namespace slang

namespace slang::ast {
class Type;
class ValueSymbol;
} // namespace slang::ast

namespace server {
/**
 * @brief Utility functions for formatting SystemVerilog code snippets.
 * These utility functions help with formatting code snippets for LSP responses, as well as
 * formatting the SystemVeriog itself. Eventually the SV Formatting will be superseded by a full SV
 * formatter, but for now these provide decent formatting utility.
 */

using namespace slang;

static const size_t FORMATTING_INDENT = 4;

/// @brief Strip whitespace leading up to the first line with a non-whitespace character
void stripBlankLines(std::string& s);

/// Left align the block of code
void shiftIndent(std::string& s);

/// @brief For each line, squash multiple spaces into a single other than the leading indent
void squashSpaces(std::string& s);

bool isSingleLine(const std::string& s);

std::string detailFormat(const syntax::SyntaxNode& node);

/// Format a syntax node as plain text (no markdown wrapper)
std::string formatSyntaxNode(const syntax::SyntaxNode& node);

std::string svCodeBlockString(std::string_view code);
std::string svCodeBlockString(const syntax::SyntaxNode& node);

lsp::MarkupContent svCodeBlock(std::string_view code);
lsp::MarkupContent svCodeBlock(const syntax::SyntaxNode& node);

void ltrim(std::string& s);

std::string toCamelCase(std::string_view str);

/// @brief Format a ConstantValue for display in hovers
/// For string values, shows escaped invalid UTF-8 characters
std::string formatConstantValue(const slang::ConstantValue& value);

// Print the canonical type nicely, if it's a type alias
template<bool isMarkdown>
std::string getTypeStringImpl(const ast::Type& type);

// Print a type of a value symbol nicely, including the canonical type and port direction if
// applicable
template<bool isMarkdown>
std::string getTypeStringImpl(const ast::ValueSymbol& value);

// Plain text versions
inline std::string getTypeString(const ast::Type& type) {
    return getTypeStringImpl<false>(type);
}
inline std::string getTypeString(const ast::ValueSymbol& value) {
    return getTypeStringImpl<false>(value);
}

// Hover/Markdown versions
inline std::string getHoverTypeString(const ast::Type& type) {
    return getTypeStringImpl<true>(type);
}
inline std::string getHoverTypeString(const ast::ValueSymbol& value) {
    return getTypeStringImpl<true>(value);
}

} // namespace server
