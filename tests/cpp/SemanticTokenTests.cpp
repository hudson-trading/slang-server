// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/SemanticTokens.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang;

TEST_CASE("SemanticTokenMapping") {
    using TK = parsing::TokenKind;
    using ST = server::SemanticTokenType;

    CHECK(server::mapTokenKind(TK::ModuleKeyword) == ST::Keyword);
    CHECK(server::mapTokenKind(TK::Identifier) == ST::Variable);
    CHECK(server::mapTokenKind(TK::SystemIdentifier) == ST::Function);
    CHECK(server::mapTokenKind(TK::Directive) == ST::Macro);
    CHECK(server::mapTokenKind(TK::StringLiteral) == ST::String);
    CHECK(server::mapTokenKind(TK::IntegerLiteral) == ST::Number);
    CHECK(server::mapTokenKind(TK::RealLiteral) == ST::Number);
    CHECK(server::mapTokenKind(TK::TimeLiteral) == ST::Number);
    CHECK(server::mapTokenKind(TK::Semicolon) == ST::Operator);
    CHECK(!server::mapTokenKind(TK::EndOfFile).has_value());
}
