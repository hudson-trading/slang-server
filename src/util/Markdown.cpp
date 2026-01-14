// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Markdown.h"

#include <fmt/format.h>
#include <sstream>

namespace server::markup {

// Paragraph implementation

Paragraph& Paragraph::appendText(std::string_view text) {
    buffer += text;
    return *this;
}

Paragraph& Paragraph::appendCode(std::string_view code) {
    fmt::format_to(std::back_inserter(buffer), "`{}`", code);
    return *this;
}

Paragraph& Paragraph::appendBold(std::string_view text) {
    if (!buffer.empty() && buffer.back() != ' ') {
        buffer += " ";
    }
    fmt::format_to(std::back_inserter(buffer), "**{}** ", text);
    return *this;
}

Paragraph& Paragraph::appendCodeBlock(std::string_view code) {
    // Use quad backticks for SystemVerilog since triple can be used in macro concatenations
    fmt::format_to(std::back_inserter(buffer), "````systemverilog\n{}\n````", code);
    return *this;
}

Paragraph& Paragraph::newLine() {
    buffer += "  \n"; // Markdown line break (two spaces + newline)
    return *this;
}

// Document implementation

Paragraph& Document::addParagraph() {
    paragraphs.emplace_back();
    return paragraphs.back();
}

void Document::addParagraph(Paragraph para) {
    paragraphs.push_back(std::move(para));
}

lsp::MarkupContent Document::build() const {
    std::ostringstream result;
    bool first = true;

    for (const auto& para : paragraphs) {
        if (para.isEmpty())
            continue;

        if (!first)
            result << "\n\n---\n\n";

        result << para.asMarkdown();
        first = false;
    }

    return lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(), .value = result.str()};
}

} // namespace server::markup
