// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Markdown.h"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <sstream>
#include <string>
#include <string_view>

namespace server::markup {

namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isAlpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isAlnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool isDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isXDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

std::string_view rtrim(std::string_view s) {
    while (!s.empty() && isSpace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

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

Paragraph& Paragraph::appendHeader(std::string_view text, int level) {
    if (!buffer.empty() && buffer.back() != ' ') {
        buffer += " ";
    }
    buffer += std::string(level, '#') + " " + std::string(text);
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

namespace {

// Function adapted from the LLVM project
// https://github.com/llvm/llvm-project/blob/11515959b571739ab046b368c351b75e46ef7c3b/clang-tools-extra/clangd/support/Markup.cpp#L27-L152
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Is <contents a plausible start to an HTML tag?
// Contents may not be the rest of the line, but it's the rest of the plain
// text, so we expect to see at least the tag name.
bool looksLikeTag(std::string_view contents) {
    if (contents.empty())
        return false;
    if (contents.front() == '!' || contents.front() == '?' || contents.front() == '/')
        return true;
    // Check the start of the tag name.
    if (!isAlpha(contents.front()))
        return false;
    // Drop rest of the tag name, and following whitespace.
    size_t i = 0;
    while (i < contents.size() &&
           (isAlnum(contents[i]) || contents[i] == '-' || contents[i] == '_' || contents[i] == ':'))
        ++i;
    while (i < contents.size() && isSpace(contents[i]))
        ++i;
    // The rest of the tag consists of attributes, which have restrictive names.
    // If we hit '=', all bets are off (attribute values can contain anything).
    for (; i < contents.size(); ++i) {
        auto suffix = contents.substr(i);
        if (isAlnum(contents[i]) || isSpace(contents[i]))
            continue;
        if (contents[i] == '>' || suffix.starts_with("/>"))
            return true; // May close the tag.
        if (contents[i] == '=')
            return true; // Don't try to parse attribute values.
        return false;    // Random punctuation means this isn't a tag.
    }
    return true; // Potentially incomplete tag.
}

// Function adapted from the LLVM project
// https://github.com/llvm/llvm-project/blob/11515959b571739ab046b368c351b75e46ef7c3b/clang-tools-extra/clangd/support/Markup.cpp#L27-L152
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Tests whether C should be backslash-escaped in markdown.
// The string being escaped is Before + C + After. This is part of a paragraph.
// StartsLine indicates whether `Before` is the start of the line.
// After may not be everything until the end of the line.
//
// It's always safe to escape punctuation, but want minimal escaping.
// The strategy is to escape the first character of anything that might start
// a markdown grammar construct.
bool needsLeadingEscapePlaintext(char c, std::string_view before, std::string_view after,
                                 bool startsLine) {

    auto rulerLength = [&]() -> unsigned {
        if (!startsLine || !before.empty())
            return 0;
        auto trimmedAfter = rtrim(after);
        if (!std::all_of(trimmedAfter.begin(), trimmedAfter.end(), [&](char d) { return d == c; }))
            return 0;
        return 1 + trimmedAfter.size();
    };
    auto isBullet = [&]() {
        return startsLine && before.empty() && (after.empty() || after.starts_with(" "));
    };
    auto spaceSurrounds = [&]() {
        return (after.empty() || isSpace(after.front())) &&
               (before.empty() || isSpace(before.back()));
    };
    auto wordSurrounds = [&]() {
        return (!after.empty() && isAlnum(after.front())) &&
               (!before.empty() && isAlnum(before.back()));
    };

    switch (c) {
        case '\\': // Escaped character.
            return true;
        case '`': // Code block or inline code
            // Any number of backticks can delimit an inline code block that can end
            // anywhere (including on another line). We must escape them all.
            return true;
        case '~': // Code block
            return startsLine && before.empty() && after.starts_with("~~");
        case '#': { // ATX heading.
            if (!startsLine || !before.empty())
                return false;
            std::string_view rest = after;
            while (!rest.empty() && rest.front() == '#')
                rest.remove_prefix(1);
            return rest.empty() || rest.starts_with(" ");
        }
        case ']': // Link or link reference.
            // We escape ] rather than [ here, because it's more constrained:
            //   ](...) is an in-line link
            //   ]: is a link reference
            // The following are only links if the link reference exists:
            //   ] by itself is a shortcut link
            //   ][...] is an out-of-line link
            // Because we never emit link references, we don't need to handle these.
            return after.starts_with(":") || after.starts_with("(");
        case '=': // Setex heading.
            return rulerLength() > 0;
        case '_': // Horizontal ruler or matched delimiter.
            if (rulerLength() >= 3)
                return true;
            // Not a delimiter if surrounded by space, or inside a word.
            // (The rules at word boundaries are subtle).
            return !(spaceSurrounds() || wordSurrounds());
        case '-': // Setex heading, horizontal ruler, or bullet.
            if (rulerLength() > 0)
                return true;
            return isBullet();
        case '+': // Bullet list.
            return isBullet();
        case '*': // Bullet list, horizontal ruler, or delimiter.
            return isBullet() || rulerLength() >= 3 || !spaceSurrounds();
        case '<': // HTML tag (or autolink, which we choose not to escape)
            return looksLikeTag(after);
        case '>': // Quote marker. Needs escaping at start of line.
            return startsLine && before.empty();
        case '&': { // HTML entity reference
            auto end = after.find(';');
            if (end == std::string_view::npos)
                return false;
            auto content = after.substr(0, end);
            if (!content.empty() && content.front() == '#') {
                content.remove_prefix(1);
                if (!content.empty() && (content.front() == 'x' || content.front() == 'X')) {
                    content.remove_prefix(1);
                    return std::all_of(content.begin(), content.end(),
                                       [](char c) { return isXDigit(c); });
                }
                return std::all_of(content.begin(), content.end(),
                                   [](char c) { return isDigit(c); });
            }
            return std::all_of(content.begin(), content.end(), [](char c) { return isAlpha(c); });
        }
        case '.': // Numbered list indicator. Escape 12. -> 12\. at start of line.
        case ')':
            return startsLine && !before.empty() &&
                   std::all_of(before.begin(), before.end(), [](char c) { return isDigit(c); }) &&
                   after.starts_with(" ");
        default:
            return false;
    }
}

} // namespace

std::string escapeMarkdownLine(std::string_view line) {
    std::string out;
    out.reserve(line.size());

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        std::string_view before = line.substr(0, i);
        std::string_view after = line.substr(i + 1);

        if (needsLeadingEscapePlaintext(c, before, after, true))
            out.push_back('\\');

        out.push_back(c);
    }

    return out;
}

} // namespace server::markup
