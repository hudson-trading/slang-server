// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Formatting.h"

#include "lsp/LspTypes.h"
#include <fmt/format.h>
#include <sstream>
#include <string>

#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/types/TypePrinter.h"
#include "slang/numeric/ConstantValue.h"
#include "slang/parsing/Token.h"
#include "slang/parsing/TokenKind.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/text/CharInfo.h"

namespace server {
using namespace slang;

void stripBlankLines(std::string& s) {
    auto firstTok = std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !isWhitespace(static_cast<char>(ch));
    });
    // now get newline before that, if any
    auto lineStart = std::find_if(std::reverse_iterator(firstTok), s.rend(),
                                  [](unsigned char ch) { return ch == '\n'; });

    if (lineStart != s.rend()) {
        s.erase(s.begin(), lineStart.base());
    }
}

void shiftIndent(std::string& s) {
    if (s.empty()) {
        return;
    }

    // First pass: determine the minimum indentation
    size_t minIndent = SIZE_MAX;
    std::istringstream stream(s);
    std::string line;
    bool usingTabs = s.find('\t') != std::string::npos;
    char iChar = usingTabs ? '\t' : ' ';

    // if it's a single line, just lstrip
    if (isSingleLine(s)) {
        ltrim(s);
        return;
    }

    // Skip the first line, since it's whitespace isn't included
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        size_t indent = 0;
        for (char c : line) {
            if (c == iChar) {
                indent++;
            }
            else {
                break; // Found non-whitespace character
            }
        }

        // Only consider lines with content
        if (indent < line.length()) {
            minIndent = std::min(minIndent, indent);
        }
    }

    // If no content found or no indentation to remove
    if (minIndent == SIZE_MAX || minIndent == 0) {
        return;
    }

    // Second pass: remove that indentation from each line
    std::ostringstream result;
    stream.clear();
    stream.str(s);
    while (std::getline(stream, line)) {

        if (line.empty()) {
            // Keep empty lines as-is
            continue;
        }

        // Remove the minimum indentation
        size_t removed = 0;
        size_t pos = 0;
        while (pos < line.length() && removed < minIndent) {
            if (line[pos] == iChar) {
                removed++;
                pos++;
            }
            else {
                break;
            }
        }

        result << line.substr(pos) << "\n";
    }

    s = result.str();
    s.resize(s.size() - 1); // remove last added newline
}

void squashSpaces(std::string& s) {
    // be efficient here- minimize copying
    if (s.empty()) {
        return;
    }

    std::ostringstream result;
    std::istringstream stream(s);
    std::string line;

    while (std::getline(stream, line)) {

        if (line.empty()) {
            continue;
        }

        // Find the end of leading whitespace
        size_t contentStart = 0;
        while (contentStart < line.length() &&
               (line[contentStart] == ' ' || line[contentStart] == '\t')) {
            contentStart++;
        }

        // Copy leading whitespace as-is
        result << line.substr(0, contentStart);

        // Process the content part, squashing multiple spaces
        bool inSpaceSequence = false;
        for (size_t i = contentStart; i < line.length(); ++i) {
            char c = line[i];

            if (c == ' ') {
                if (!inSpaceSequence) {
                    result << c;
                    inSpaceSequence = true;
                }
                // Skip additional spaces in the sequence
            }
            else {
                result << c;
                inSpaceSequence = false;
            }
        }
        result << '\n';
    }

    s = result.str();
    s.resize(s.size() - 1); // remove last added newline
}

bool isSingleLine(const std::string& s) {
    return s.find('\n') == std::string::npos;
}

// Print compactly in a single line
std::string detailFormat(const syntax::SyntaxNode& node) {
    auto res = syntax::SyntaxPrinter().setIncludeComments(false).print(node).str();
    stripBlankLines(res);
    squashSpaces(res);
    ltrim(res);
    return res;
}

/// Copied from `Slang::SyntaxPrinter::printLeadingComments` with minor adjustments
/// Licensed under MIT; see external/slang/LICENSE
std::optional<std::span<const parsing::Trivia>::iterator> findLeadingDocCommentStart(
    const syntax::SyntaxNode& node) {
    auto triviaSpan = node.getFirstToken().trivia();
    using Iterator = std::span<const parsing::Trivia>::iterator;
    std::optional<Iterator> lastComment;
    std::optional<Iterator> leadingCommentStart;

    // Walk backwards through trivia until
    // - block comment
    // - double new line after seeing a comment
    // This misses leading trivia at first line, although that's typically for license/file
    auto findDocBoundary = [&]() {
        bool lastIsNewline = false;
        for (auto it = triviaSpan.rbegin(); it != triviaSpan.rend(); it++) {
            const auto& trivia = *it;
            switch (trivia.kind) {
                case parsing::TriviaKind::EndOfLine:
                    if (lastIsNewline && lastComment) {
                        // found a double newline after a comment, stop here
                        return;
                    }
                    leadingCommentStart = lastComment;
                    lastIsNewline = true;
                    break;
                case parsing::TriviaKind::BlockComment:
                    // the first block comment is the start
                    leadingCommentStart = it.base() - 1;
                    return;
                case parsing::TriviaKind::LineComment:
                    lastComment = it.base() - 1;
                    [[fallthrough]];
                default:
                    lastIsNewline = false;
            }
        }
    };
    findDocBoundary();

    return leadingCommentStart;
}

std::string stripDocComment(const syntax::SyntaxNode& node) {
    auto triviaSpan = node.getFirstToken().trivia();
    auto start = findLeadingDocCommentStart(node);
    if (!start)
        return {};

    fmt::memory_buffer out;
    bool inBlock = false;
    bool lastLineHadText = false;

    auto appendLine = [&](std::string_view line) {
        // Trim leading whitespace
        ltrim(line);

        if (!inBlock) {
            // Single-line doc comment
            if (line.starts_with("///"))
                line.remove_prefix(3);
            else if (line.starts_with("//"))
                line.remove_prefix(2);

            // To be a valid doc comment, block comments must start
            // at beginning of line
            else if (line.starts_with("/*")) {
                inBlock = true;
                line.remove_prefix(2);
            }
        }

        if (inBlock) {
            // End of block comment
            if (auto p = line.find("*/"); p != std::string_view::npos) {
                line = line.substr(0, p);
                inBlock = false;
            }

            // Check for leading '*'; ie:
            /*
             * <- Leading star
             */
            if (!line.empty() && line.front() == '*')
                line.remove_prefix(1);

            // Everything else if ignored, including single line comments. Single
            // line comments are displayed as is in the doc comment.
        }

        const bool hasText = !line.empty();

        // Force markdown to respect newlines by replacing `\n` with `  \n`
        if (lastLineHadText) {
            fmt::format_to(fmt::appender(out), "  \n");
        }
        else if (!lastLineHadText && hasText) {
            fmt::format_to(fmt::appender(out), "\n");
        }

        fmt::format_to(fmt::appender(out), "{}", line);
        lastLineHadText = hasText;
    };

    for (auto it = *start; it != triviaSpan.end(); ++it) {
        const auto& t = *it;

        if (t.kind == parsing::TriviaKind::LineComment ||
            t.kind == parsing::TriviaKind::BlockComment) {

            std::string_view text = t.getRawText();

            std::size_t pos = 0;
            while (pos <= text.size()) {
                std::size_t end = text.find('\n', pos);
                if (end == std::string_view::npos)
                    end = text.size();

                std::string_view line = text.substr(pos, end - pos);
#ifdef _WIN32
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
#endif
                appendLine(line);

                pos = end + 1;
            }
        }
        // else if (t.kind == parsing::TriviaKind::EndOfLine) {
        // }
    }

    return fmt::to_string(out);
}

std::string svCodeBlockString(std::string_view code) {
    auto res = std::string{code};
    stripBlankLines(res);
    shiftIndent(res);
    // We use quad backticks since in sv triple can be used for macro concatenations
    return fmt::format("````systemverilog\n{}\n````", res);
}

lsp::MarkupContent svCodeBlock(const std::string_view code) {
    return lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(),
                              .value = svCodeBlockString(code)};
}

const syntax::SyntaxNode& selectDisplayNode(const syntax::SyntaxNode& node) {
    const syntax::SyntaxNode* fmtNode = &node;
    switch (node.kind) {
        // Adjust these to just be the header
        case syntax::SyntaxKind::ModuleDeclaration:
        case syntax::SyntaxKind::ProgramDeclaration:
        case syntax::SyntaxKind::PackageDeclaration:
        case syntax::SyntaxKind::InterfaceDeclaration:
            fmtNode = node.as<syntax::ModuleDeclarationSyntax>().header;
            break;
        // Adjust to include the type in the declaration
        case syntax::SyntaxKind::Declarator:
        case syntax::SyntaxKind::HierarchicalInstance:
        case syntax::SyntaxKind::EnumType:
        case syntax::SyntaxKind::TypeAssignment:
            fmtNode = node.parent;
            break;
        default:
            break;
    }

    if (fmtNode->parent && fmtNode->parent->kind == syntax::SyntaxKind::TypedefDeclaration) {
        fmtNode = fmtNode->parent;
    }

    return *fmtNode;
}

inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            s.end());
}

std::string formatDocComment(const syntax::SyntaxNode& node) {
    auto res = slang::syntax::SyntaxPrinter().printLeadingComments(node).str();

    if (res.empty()) {
        return "";
    }

    // Apply formatting for clean display
    stripBlankLines(res);
    shiftIndent(res);
    rtrim(res);

    return res + "\n";
}

std::string formatCode(const syntax::SyntaxNode& node) {
    auto res = slang::syntax::SyntaxPrinter().printExcludingLeadingComments(node).str();

    if (isSingleLine(res)) {
        squashSpaces(res);
    }

    // Apply formatting for clean display
    stripBlankLines(res);
    shiftIndent(res);

    return res;
}

std::string svCodeBlockString(const syntax::SyntaxNode& node) {
    const auto& fmtNode = selectDisplayNode(node);
    const auto res = formatDocComment(fmtNode) + formatCode(fmtNode);
    return svCodeBlockString(res);
}

lsp::MarkupContent svCodeBlock(const syntax::SyntaxNode& node) {
    return lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(),
                              .value = svCodeBlockString(node)};
}

void ltrim(std::string& s) {
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

void ltrim(std::string_view& sv) {
    std::size_t i = 0;
    while (i < sv.size() && std::isspace(sv[i])) {
        ++i;
    }
    sv.remove_prefix(i);
}

std::string toCamelCase(std::string_view str) {
    if (str.empty()) {
        return "";
    }
    std::string result;
    result.reserve(str.size());
    result.push_back(static_cast<char>(std::tolower(str[0])));
    result.append(str.substr(1));
    return result;
}

template<bool ForHover>
std::string getTypeStringImpl(const ast::Type& declType) {
    if (declType.isError()) {
        return "Incomplete type";
    }

    slang::ast::TypePrinter printer;
    if constexpr (ForHover) {
        printer.options.quoteChar = '`';
        printer.options.anonymousTypeStyle = ast::TypePrintingOptions::FriendlyName;
    }
    printer.options.elideScopeNames = true;
    printer.options.skipTypeDefs = true;
    printer.options.printAKA = true;
    printer.options.printIntegralRange = true;
    printer.append(declType);

    auto& type = declType.getCanonicalType();

    if (type.isStruct() || type.isUnion() || type.isEnum()) {
        auto kindStr = toString(type.kind);
        // Trim off "Type" from kind string
        return fmt::format("{} {}", kindStr.substr(0, kindStr.size() - 4), printer.toString());
    }
    else {
        return printer.toString();
    }
}

template std::string getTypeStringImpl<true>(const ast::Type& declType);
template std::string getTypeStringImpl<false>(const ast::Type& declType);

std::string portString(ast::ArgumentDirection dir) {
    switch (dir) {
        case ast::ArgumentDirection::In:
            return "input";
        case ast::ArgumentDirection::Out:
            return "output";
        case ast::ArgumentDirection::InOut:
            return "inout";
        case ast::ArgumentDirection::Ref:
            return "ref";
        default:
            SLANG_UNREACHABLE;
    }
    return "unknown";
}

template<bool ForHover>
std::string getTypeStringImpl(const ast::ValueSymbol& value) {
    const slang::ast::Type& decl = value.getType();
    auto port = value.getFirstPortBackref();
    if (port) {
        return fmt::format("{} {}", portString(port->port->direction),
                           getTypeStringImpl<ForHover>(decl));
    }
    return getTypeStringImpl<ForHover>(decl);
}

template std::string getTypeStringImpl<true>(const ast::ValueSymbol& value);
template std::string getTypeStringImpl<false>(const ast::ValueSymbol& value);

bool isValidUtf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int seqLen = utf8Len(c);

        // utf8Len returns 0 for invalid leading bytes
        if (seqLen == 0) {
            return false;
        }

        // Check if we have enough bytes remaining
        if (i + seqLen > s.size()) {
            return false;
        }

        // Validate continuation bytes (must be 10xxxxxx)
        for (int j = 1; j < seqLen; j++) {
            unsigned char cont = static_cast<unsigned char>(s[i + j]);
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
        }

        i += seqLen;
    }

    return true;
}

// Escape invalid UTF-8 bytes as \xNN, preserving valid ASCII/UTF-8
std::string escapeInvalidUtf8(std::string_view s) {
    std::string result;
    result.reserve(s.size());

    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int seqLen = utf8Len(c);

        // utf8Len returns 0 for invalid leading bytes
        if (seqLen == 0) {
            result += fmt::format("\\x{:02x}", c);
            i++;
            continue;
        }

        // Check if we have enough bytes remaining
        if (i + static_cast<size_t>(seqLen) > s.size()) {
            result += fmt::format("\\x{:02x}", c);
            i++;
            continue;
        }

        // Validate continuation bytes (must be 10xxxxxx)
        bool valid = true;
        for (int j = 1; j < seqLen; j++) {
            unsigned char cont = static_cast<unsigned char>(s[i + j]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }

        if (valid) {
            result.append(s.substr(i, seqLen));
            i += seqLen;
        }
        else {
            result += fmt::format("\\x{:02x}", c);
            i++;
        }
    }

    return result;
}

std::string formatConstantValue(const ConstantValue& value) {
    if (value.isString()) {
        const auto& str = value.str();
        if (isValidUtf8(str)) {
            // Valid UTF-8 string, display normally
            return value.toString();
        }
        else {
            // Invalid UTF-8: show escaped string and hex value
            return escapeInvalidUtf8(str);
        }
    }
    // For non-string values, use default toString
    return value.toString();
}

} // namespace server
