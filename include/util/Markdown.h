// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "lsp/LspTypes.h"
#include <string>
#include <string_view>
#include <vector>

namespace server::markup {

/// Represents a paragraph with inline text and code
class Paragraph {
public:
    /// Append plain text to the end of the paragraph
    Paragraph& appendText(std::string_view text);

    /// Append bold text
    Paragraph& appendBold(std::string_view text);

    /// Append a header
    Paragraph& appendHeader(std::string_view text, int level);

    /// Append inline code, this translates to the ` block in markdown
    Paragraph& appendCode(std::string_view code);

    /// Append a SystemVerilog code block
    Paragraph& appendCodeBlock(std::string_view code);

    /// Add a line break within the paragraph
    Paragraph& newLine();

    /// Check if the paragraph has any content
    bool isEmpty() const { return buffer.empty(); }

    /// Get the markdown content
    std::string_view asMarkdown() const { return buffer; }

private:
    std::string buffer;
};

/// A document is a sequence of paragraphs
class Document {
public:
    /// Add a new paragraph and return it for chaining
    Paragraph& addParagraph();

    /// Add an existing paragraph to the document
    void addParagraph(Paragraph para);

    /// Build and return LSP MarkupContent
    lsp::MarkupContent build() const;

private:
    std::vector<Paragraph> paragraphs;
};

} // namespace server::markup
