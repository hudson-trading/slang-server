// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "Formatting.h"

#include "Logging.h"
#include <sstream>

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/text/CharInfo.h"

namespace server {
using namespace slang;

void stripBlankLines(std::string& s) {
    auto firstTok = std::find_if(s.begin(), s.end(),
                                 [](unsigned char ch) { return !isWhitespace(ch); });
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

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue; // Skip empty lines
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

std::string detailFormat(const syntax::SyntaxNode& node) {
    // print without comments, need one line
    auto res = syntax::SyntaxPrinter().setIncludeComments(false).print(node).str();
    stripBlankLines(res);
    squashSpaces(res);
    if (!isSingleLine(res)) {
        ERROR("Expected single line for detail string: {}", res);
    }
    return res;
}

std::string svCodeBlockString(std::string_view code) {
    auto res = std::string{code};
    stripBlankLines(res);
    shiftIndent(res);
    if (isSingleLine(res)) {
        squashSpaces(res);
    }
    return fmt::format("````systemverilog\n{}\n````", res);
}

std::string svCodeBlockString(const syntax::SyntaxNode& node) {
    // adjust certain node types to include the type
    const syntax::SyntaxNode* fmtNode = &node;
    switch (node.kind) {
        case syntax::SyntaxKind::ModuleDeclaration:
        case syntax::SyntaxKind::ProgramDeclaration:
        case syntax::SyntaxKind::PackageDeclaration:
        case syntax::SyntaxKind::InterfaceDeclaration:
            fmtNode = node.as<syntax::ModuleDeclarationSyntax>().header;
            break;
        case syntax::SyntaxKind::Declarator:
        case syntax::SyntaxKind::HierarchicalInstance:
        case syntax::SyntaxKind::EnumType:
            fmtNode = node.parent;
            break;
        default:
            break;
    }

    auto res = fmtNode->toString();
    return svCodeBlockString(res);
}

lsp::MarkupContent svCodeBlock(const syntax::SyntaxNode& node) {
    return lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(),
                              .value = svCodeBlockString(node)};
}

} // namespace server
