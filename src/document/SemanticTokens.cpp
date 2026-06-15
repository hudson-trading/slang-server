//------------------------------------------------------------------------------
// SemanticTokens.cpp
// Implementation of semantic token mapping for LSP
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/SemanticTokens.h"

#include "slang/parsing/LexerFacts.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxFacts.h"
#include "slang/syntax/SyntaxKind.h"

namespace server {

using namespace slang;
using TK = parsing::TokenKind;

std::optional<SemanticTokenType> mapTokenKind(parsing::TokenKind kind) {
    if (parsing::LexerFacts::isKeyword(kind))
        return SemanticTokenType::Keyword;

    switch (kind) {
        case TK::Identifier:
            return SemanticTokenType::Variable;

        case TK::SystemIdentifier:
        case TK::UnitSystemName:
        case TK::RootSystemName:
            return SemanticTokenType::Function;

        case TK::StringLiteral:
        case TK::IncludeFileName:
            return SemanticTokenType::String;

        case TK::IntegerLiteral:
        case TK::IntegerBase:
        case TK::UnbasedUnsizedLiteral:
        case TK::RealLiteral:
        case TK::TimeLiteral:
            return SemanticTokenType::Number;

        case TK::Directive:
        case TK::MacroUsage:
        case TK::MacroQuote:
        case TK::MacroTripleQuote:
        case TK::MacroEscapedQuote:
        case TK::MacroPaste:
            return SemanticTokenType::Macro;

        default: {
            if (syntax::SyntaxFacts::getUnaryPrefixExpression(kind) !=
                    syntax::SyntaxKind::Unknown ||
                syntax::SyntaxFacts::getBinaryExpression(kind) != syntax::SyntaxKind::Unknown) {
                return SemanticTokenType::Operator;
            }
            return std::nullopt;
        }
    }
}

std::vector<uint32_t> computeSemanticTokenData(const SyntaxIndexer& indexer,
                                               const SourceManager& sm) {
    std::vector<uint32_t> data;
    data.reserve(indexer.collected.size() * 5);
    uint32_t prevLine = 0;
    uint32_t prevCol = 0;

    for (const auto* tok : indexer.collected) {
        auto type = mapTokenKind(tok->kind);
        if (!type)
            continue;

        auto loc = tok->location();
        uint32_t line = static_cast<uint32_t>(sm.getLineNumber(loc)) - 1;
        uint32_t col = static_cast<uint32_t>(sm.getColumnNumber(loc)) - 1;
        auto length = static_cast<uint32_t>(tok->rawText().length());

        if (length == 0)
            continue;

        uint32_t deltaLine = line - prevLine;
        uint32_t deltaStart = (deltaLine == 0) ? (col - prevCol) : col;

        data.push_back(deltaLine);
        data.push_back(deltaStart);
        data.push_back(length);
        data.push_back(static_cast<uint32_t>(*type));
        data.push_back(0);

        prevLine = line;
        prevCol = col;
    }

    return data;
}

} // namespace server
