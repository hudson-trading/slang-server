//------------------------------------------------------------------------------
// Converters.cpp
// Type conversion utilities for LSP server, primarily between slang and LSP types
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "util/Converters.h"

#include <fmt/format.h>

namespace server {

using namespace slang;

// Runs dfs on the syntax node to find the name token, which will point to the same memory
std::optional<const parsing::Token> findNameToken(const syntax::SyntaxNode* node,
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

lsp::Position toPosition(const SourceLocation& loc, const SourceManager& sourceManager) {
    auto character = sourceManager.getColumnNumber(loc);
    return lsp::Position{.line = static_cast<lsp::uint>(sourceManager.getLineNumber(loc) - 1),
                         .character = static_cast<lsp::uint>(character > 0 ? character - 1 : 0)};
}

lsp::Range toRange(const SourceRange& range, const SourceManager& sourceManager) {
    return lsp::Range{.start = toPosition(range.start(), sourceManager),
                      .end = toPosition(range.end(), sourceManager)};
}

lsp::Range toRange(const SourceLocation& loc, const SourceManager& sourceManager,
                   const size_t length) {

    auto character = sourceManager.getColumnNumber(loc);
    lsp::Position start{.line = static_cast<lsp::uint>(sourceManager.getLineNumber(loc) - 1),
                        .character = static_cast<lsp::uint>(character > 0 ? character - 1 : 0)};
    lsp::Position end{start};
    end.character += length;
    return lsp::Range{.start = start, .end = end};
}

lsp::Location toLocation(const SourceRange& range, const SourceManager& sourceManager) {
    // TODO: make this logic just one function in source manager
    auto declRange = range;

    // Get location of `MACRO_USAGE if from a macro expansion
    auto locs = sourceManager.getMacroExpansions(range.start());
    if (locs.size()) {
        auto macroInfo = sourceManager.getMacroInfo(locs.back());
        declRange = macroInfo ? macroInfo->expansionRange
                              : sourceManager.getFullyOriginalRange(range);
    }

    return lsp::Location{.uri = URI::fromFile(
                             sourceManager.getFullPath(declRange.start().buffer())),
                         .range = toRange(declRange, sourceManager)};
}

lsp::Location toLocation(const SourceLocation& loc, const SourceManager& sourceManager) {
    return lsp::Location{.uri = URI::fromFile(sourceManager.getFullPath(loc.buffer())),
                         .range = lsp::Range{.start = toPosition(loc, sourceManager),
                                             .end = toPosition(loc + 1, sourceManager)}};
}

lsp::MarkupContent markdown(std::string& md) {
    return lsp::MarkupContent{.kind = lsp::MarkupKind::make<"markdown">(), .value = md};
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

std::string subroutineString(ast::SubroutineKind kind) {
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
