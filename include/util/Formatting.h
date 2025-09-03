// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "lsp/LspTypes.h"
#include <string>
#include <string_view>

#include "slang/syntax/SyntaxNode.h"

namespace server {
using namespace slang;

/// @brief Strip whitespace leading up to the first line with a non-whitespace character
void stripBlankLines(std::string& s);

/// Left align the block of code
void shiftIndent(std::string& s);

/// @brief For each line, squash multiple spaces into a single other than the leading indent
void squashSpaces(std::string& s);

bool isSingleLine(const std::string& s);

std::string detailFormat(const syntax::SyntaxNode& node);

std::string svCodeBlockString(std::string_view code);

std::string svCodeBlockString(const syntax::SyntaxNode& node);

lsp::MarkupContent svCodeBlock(const syntax::SyntaxNode& node);

void ltrim(std::string& s);

std::string toCamelCase(std::string_view str);

} // namespace server
