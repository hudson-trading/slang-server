//------------------------------------------------------------------------------
// Converters.h
// Type conversion utilities for LSP server.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <fmt/format.h>
#include <optional>
#include <string_view>

#include "slang/ast/SemanticFacts.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {

using namespace slang;

// Runs dfs on the syntax node to find the name token, which will point to the same memory
inline std::optional<const parsing::Token> findNameToken(const syntax::SyntaxNode* node,
                                                         std::string_view name) {
    // The name and token will occupy the same memory, so match on that
    for (size_t i = 0; i < node->getChildCount(); i++) {
        // check if the child is a token
        auto token = node->childToken(i);
        if (token) {
            if (token.valueText().data() == name.data()) {
                return token;
            }
            continue;
        }
        // check if the child is a node
        auto child = node->childNode(i);
        if (child) {
            auto token = findNameToken(child, name);
            if (token)
                return token;
        }
    }
    return std::nullopt;
}

static lsp::Position toPosition(const SourceLocation& loc, const SourceManager& sourceManager) {
    auto character = sourceManager.getColumnNumber(loc);
    return lsp::Position{.line = static_cast<uint>(sourceManager.getLineNumber(loc) - 1),
                         .character = static_cast<uint>(character > 0 ? character - 1 : 0)};
}

static lsp::Range toRange(const SourceRange& range, const SourceManager& sourceManager) {
    return lsp::Range{.start = toPosition(range.start(), sourceManager),
                      .end = toPosition(range.end(), sourceManager)};
}

static lsp::Range toRange(const SourceLocation& loc, const SourceManager& sourceManager,
                          const size_t length) {

    auto character = sourceManager.getColumnNumber(loc);
    lsp::Position start{.line = static_cast<uint>(sourceManager.getLineNumber(loc) - 1),
                        .character = static_cast<uint>(character > 0 ? character - 1 : 0)};
    lsp::Position end{start};
    end.character += length;
    return lsp::Range{.start = start, .end = end};
}

static lsp::Location toLocation(const SourceRange& range, const SourceManager& sourceManager) {
    return lsp::Location{.uri = URI::fromFile(sourceManager.getFullPath(range.start().buffer())),
                         .range = toRange(range, sourceManager)};
}

static lsp::Location toLocation(const SourceLocation& loc, const SourceManager& sourceManager) {
    return lsp::Location{.uri = URI::fromFile(sourceManager.getFullPath(loc.buffer())),
                         .range = lsp::Range{.start = toPosition(loc, sourceManager),
                                             .end = toPosition(loc + 1, sourceManager)}};
}

static bool isWithin(const lsp::Position& position, const lsp::Range& range) {
    return position.line >= range.start.line && position.line <= range.end.line &&
           position.character >= range.start.character && position.character <= range.end.character;
}

static lsp::MarkupContent markdown(std::string& md) {
    // We use quad backticks since in sv it's used for macro concatenations
    return lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(), .value = md};
}

static void ltrim(std::string& s) {
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

static std::string toCamelCase(std::string_view str) {
    if (str.empty()) {
        return "";
    }
    std::string result;
    result.reserve(str.size());
    result.push_back(static_cast<char>(std::tolower(str[0])));
    result.append(str.substr(1));
    return result;
}

static std::string portString(ast::ArgumentDirection dir) {
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

static std::string subroutineString(ast::SubroutineKind kind) {
    switch (kind) {
        case ast::SubroutineKind::Function:
            return "function";
        case ast::SubroutineKind::Task:
            return "task";
        default:
            SLANG_UNREACHABLE;
    }
}

} // namespace server
template<>
struct fmt::formatter<slang::SourceLocation> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    constexpr auto format(const slang::SourceLocation& loc, FormatContext& ctx) const {
        if (loc == slang::SourceLocation::NoLocation)
            return fmt::format_to(ctx.out(), "NoLocation");
        else {
            return fmt::format_to(ctx.out(), "{}", loc.offset());
        }
    }
};

template<>
struct fmt::formatter<slang::SourceRange> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    constexpr auto format(const slang::SourceRange& range, FormatContext& ctx) const {
        if (range == slang::SourceRange::NoLocation)
            return fmt::format_to(ctx.out(), "NoRange");
        else {
#ifdef SLANG_DEBUG
            return fmt::format_to(ctx.out(), "{}: {} - {}", range.start().bufferName, range.start(),
                                  range.end());
#else
            return fmt::format_to(ctx.out(), "{} - {}", range.start(), range.end());
#endif
        }
    }
};
