// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Formatting.h"

#include "Config.h"
#include "lsp/LspTypes.h"
#include "util/Markdown.h"
#include "util/SlangExtensions.h"
#include <cctype>
#include <fmt/format.h>
#include <sstream>
#include <string>

#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/DeclaredType.h"
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

namespace {
std::string escapeInvalidUtf8(std::string_view s);
}

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

std::optional<std::string> getDeclaredTypeString(const ast::ValueSymbol& value) {
    auto declaredType = value.getDeclaredType();
    auto* syntax = declaredType->getResolvedTypeSyntax();
    if (!syntax)
        return std::nullopt;

    auto result = detailFormat(*syntax);
    if (auto* dimensions = declaredType->getDimensionSyntax(); dimensions && !dimensions->empty()) {
        result += " ";
        for (auto* dimension : *dimensions)
            result += detailFormat(*dimension);
    }
    if (result.empty())
        return std::nullopt;
    return result;
}

/// Copied from `Slang::SyntaxPrinter::printLeadingComments` with minor adjustments
/// Licensed under MIT; see external/slang/LICENSE
std::optional<size_t> findLeadingDocCommentStart(const syntax::SyntaxNode& node) {
    auto triviaSpan = node.getFirstToken().trivia();
    std::optional<size_t> lastComment;
    std::optional<size_t> leadingCommentStart;

    // Walk backwards through trivia until
    // - block comment
    // - double new line after seeing a comment
    // This misses leading trivia at first line, although that's typically for license/file
    auto findDocBoundary = [&]() {
        bool lastIsNewline = false;
        for (size_t i = triviaSpan.size(); i > 0; --i) {
            const size_t triviaIndex = i - 1;
            const auto& trivia = triviaSpan[triviaIndex];
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
                    leadingCommentStart = triviaIndex;
                    return;
                case parsing::TriviaKind::LineComment:
                    lastComment = triviaIndex;
                    [[fallthrough]];
                default:
                    lastIsNewline = false;
            }
        }
    };
    findDocBoundary();

    return leadingCommentStart;
}

inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            s.end());
}

std::string getDocCommentForHover(const syntax::SyntaxNode& node,
                                  const Config::HoverConfig::DocCommentFormat format) {
    SLANG_ASSERT(format != Config::HoverConfig::DocCommentFormat::raw);

    auto triviaSpan = node.getFirstToken().trivia();
    auto start = findLeadingDocCommentStart(node);
    if (!start)
        return {};

    fmt::memory_buffer out;

    auto appendLine = [&](std::string_view line, parsing::TriviaKind kind) {
#ifdef _WIN32
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
#endif

        // Trim leading whitespace and newlines
        ltrim(line);

        if (kind == parsing::TriviaKind::LineComment) {
            // Single-line doc comment
            if (line.starts_with("///"))
                line.remove_prefix(3);
            else if (line.starts_with("//"))
                line.remove_prefix(2);
        }

        else { // if (kind == parsing::TriviaKind::BlockComment)
            // Check for leading '*'; ie:
            /*
             * <- Leading star
             */
            if (!line.empty() && line.front() == '*')
                line.remove_prefix(1);

            // Everything else if ignored, including single line comments. Single
            // line comments are displayed as is in the doc comment.
        }

        ltrim(line);

        const bool hasText = !line.empty();

        if (format == Config::HoverConfig::DocCommentFormat::plaintext) {
            const std::string escaped = markup::escapeMarkdownLine(line);
            fmt::format_to(fmt::appender(out), "{}", escaped);
        }
        else {
            fmt::format_to(fmt::appender(out), "{}", line);
        }

        // Force markdown to respect newlines by replacing `\n` with `  \n`
        if (hasText) {
            fmt::format_to(fmt::appender(out), "  \n");
        }
        else {
            fmt::format_to(fmt::appender(out), "\n");
        }
    };

    for (auto it = triviaSpan.begin() + static_cast<std::ptrdiff_t>(*start); it != triviaSpan.end();
         ++it) {
        const auto& t = *it;

        if (t.kind == parsing::TriviaKind::LineComment) {
            std::string_view line = t.getRawText();
            appendLine(line, t.kind);
        }

        else if (t.kind == parsing::TriviaKind::BlockComment) {

            std::string_view text = t.getRawText();

            if (text.starts_with("/*")) {
                text.remove_prefix(2);

                if (text.starts_with("*")) {
                    // Handle /** doc comments
                    text.remove_prefix(1);
                }
            }

            if (text.ends_with("*/")) {
                text.remove_suffix(2);

                if (text.ends_with("*")) {
                    // Handle **/ doc comments
                    text.remove_suffix(1);
                }
            }

            std::size_t pos = 0;
            while (pos <= text.size()) {
                std::size_t end = text.find('\n', pos);
                if (end == std::string_view::npos)
                    end = text.size();

                std::string_view line = text.substr(pos, end - pos);
                appendLine(line, t.kind);

                pos = end + 1;
            }
        }
    }

    return fmt::to_string(out);
}

std::string svCodeBlockString(std::string_view code) {
    auto res = escapeInvalidUtf8(code);
    stripBlankLines(res);
    shiftIndent(res);
    // We use quad backticks since in sv triple can be used for macro concatenations
    return fmt::format("````systemverilog\n{}\n````", res);
}

lsp::MarkupContent svCodeBlock(const std::string_view code) {
    return lsp::MarkupContent{.kind = lsp::MarkupKindOptions::from_name<"markdown">().str(),
                              .value = svCodeBlockString(code)};
}

const syntax::SyntaxNode& selectDisplayNode(const syntax::SyntaxNode& node) {
    const syntax::SyntaxNode* fmtNode = &node;
    switch (node.kind) {
        // Directives live as trivia on the following token, so their syntax parent is the
        // next declaration in the file — not a semantic parent. Skip typedef promotion.
        case syntax::SyntaxKind::DefineDirective:
            return node;
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

std::string formatCodeWithLeadingComments(const syntax::SyntaxNode& node) {
    auto res = slang::syntax::SyntaxPrinter().printWithLeadingComments(node).str();
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
    return lsp::MarkupContent{.kind = lsp::MarkupKindOptions::from_name<"markdown">().str(),
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
    const std::size_t n = str.size();
    if (n == 0)
        return "";

    std::string result(str);

    // 1. Always lower the first character
    result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
    if (n == 1)
        return result;

    // 2. The Sliding Window: Pre-calculate the first "current"
    bool currentIsUpper = std::isupper(static_cast<unsigned char>(result[1]));

    // Loop from the second char up to the second-to-last char
    for (std::size_t i = 1; i < n - 1; ++i) {
        // Since we stop at n-1, result[i+1] is always safe
        bool nextIsUpper = std::isupper(static_cast<unsigned char>(result[i + 1]));

        if (currentIsUpper) {
            if (nextIsUpper) {
                result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
            }
            else {
                // At the transition letter, like 'P' in 'JSONParser')
                return result;
            }
        }
        else {
            return result; // Hit a lowercase letter, we're done
        }

        currentIsUpper = nextIsUpper;
    }

    // If we reached here, it means the whole string (up to n-1) was uppercase
    if (currentIsUpper) {
        result[n - 1] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[n - 1])));
    }

    return result;
}

std::string getTypeString(const ast::Type& declType, TypeStringMode mode) {
    auto& type = unwrapErrorType(declType);
    if (type.isError()) {
        return "Incomplete type";
    }

    slang::ast::TypePrinter printer;
    if (mode != TypeStringMode::Canonical) {
        if (mode == TypeStringMode::FriendlyMarkdownQuoted)
            printer.options.quoteChar = '`';
        printer.options.anonymousTypeStyle = ast::TypePrintingOptions::FriendlyName;
    }
    printer.options.elideScopeNames = true;
    printer.options.skipTypeDefs = true;
    printer.options.printAKA = true;
    printer.options.printIntegralRange = true;
    printer.append(declType.kind == ast::SymbolKind::ErrorType ? type : declType);

    if (type.isStruct() || type.isUnion() || type.isEnum()) {
        auto kindStr = toString(type.kind);
        // Trim off "Type" from kind string
        return fmt::format("{} {}", kindStr.substr(0, kindStr.size() - 4), printer.toString());
    }
    else {
        return printer.toString();
    }
}

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

std::string getTypeString(const ast::ValueSymbol& value, TypeStringMode mode) {
    const slang::ast::Type& decl = value.getType();
    auto port = value.getFirstPortBackref();
    if (port) {
        return fmt::format("{} {}", portString(port->port->direction), getTypeString(decl, mode));
    }
    return getTypeString(decl, mode);
}

namespace {

size_t validUtf8SequenceLength(std::string_view s) {
    if (s.empty())
        return 0;

    auto sequenceLength = utf8Len(static_cast<unsigned char>(s.front()));
    if (sequenceLength == 0 || s.size() < static_cast<size_t>(sequenceLength))
        return 0;

    char bytes[4]{};
    for (size_t i = 0; i < std::min(s.size(), sizeof(bytes)); i++)
        bytes[i] = s[i];

    uint32_t codePoint;
    int error;
    int decodedLength;
    utf8Decode(bytes, &codePoint, &error, decodedLength);
    return error ? 0 : static_cast<size_t>(decodedLength);
}

// Escape invalid UTF-8 bytes as \xNN, preserving valid ASCII/UTF-8
std::string escapeInvalidUtf8(std::string_view s) {
    std::string result;
    result.reserve(s.size());

    size_t i = 0;
    while (i < s.size()) {
        auto sequenceLength = validUtf8SequenceLength(s.substr(i));
        if (sequenceLength) {
            result.append(s.substr(i, sequenceLength));
            i += sequenceLength;
        }
        else {
            result += fmt::format("\\x{:02x}", static_cast<unsigned char>(s[i]));
            i++;
        }
    }

    return result;
}

} // namespace

bool isValidUtf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        auto sequenceLength = validUtf8SequenceLength(s.substr(i));
        if (!sequenceLength)
            return false;
        i += sequenceLength;
    }
    return true;
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
