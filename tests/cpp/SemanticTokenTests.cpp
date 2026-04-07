// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/SemanticTokens.h"
#include "document/SyntaxIndexer.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "slang/parsing/Token.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

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
    CHECK(!server::mapTokenKind(TK::Semicolon).has_value());
    CHECK(!server::mapTokenKind(TK::EndOfFile).has_value());
}

namespace {

using TK = parsing::TokenKind;

void appendIdentifierToken(std::vector<parsing::Token>& storage, server::SyntaxIndexer& indexer,
                           BumpAllocator& alloc, BufferID bufId, size_t charOffset,
                           std::string_view rawText) {
    storage.emplace_back(alloc, TK::Identifier, std::span<const parsing::Trivia>{}, rawText,
                         SourceLocation(bufId, static_cast<uint64_t>(charOffset)));
    indexer.collected.push_back(&storage.back());
}

void appendSkippedKindToken(std::vector<parsing::Token>& storage, server::SyntaxIndexer& indexer,
                            BumpAllocator& alloc, BufferID bufId, size_t charOffset,
                            std::string_view rawText, TK kind) {
    storage.emplace_back(alloc, kind, std::span<const parsing::Trivia>{}, rawText,
                         SourceLocation(bufId, static_cast<uint64_t>(charOffset)));
    indexer.collected.push_back(&storage.back());
}

} // namespace

TEST_CASE("SemanticTokenDeltaSameLine") {
    SourceManager sm;
    auto buffer = sm.assignText("delta_same.sv", "aa bbb c");
    auto tree = syntax::SyntaxTree::fromBuffer(buffer, sm);
    std::vector<parsing::Token> storage;
    storage.reserve(3);
    auto& alloc = tree->allocator();
    server::SyntaxIndexer indexer(*tree);
    indexer.collected.clear();

    appendIdentifierToken(storage, indexer, alloc, buffer.id, 0, "aa");
    appendIdentifierToken(storage, indexer, alloc, buffer.id, 3, "bbb");
    appendIdentifierToken(storage, indexer, alloc, buffer.id, 7, "c");

    auto data = server::computeSemanticTokenData(indexer, sm);
    REQUIRE(data.size() == 15);
    CHECK(data[0] == 0u);
    CHECK(data[1] == 0u);
    CHECK(data[5] == 0u);
    CHECK(data[6] == 3u);
    CHECK(data[10] == 0u);
    CHECK(data[11] == 4u);
}

TEST_CASE("SemanticTokenDeltaMultiLine") {
    SourceManager sm;
    auto buffer = sm.assignText("delta_lines.sv", "aa\nbbb\nc");
    auto tree = syntax::SyntaxTree::fromBuffer(buffer, sm);
    std::vector<parsing::Token> storage;
    storage.reserve(3);
    auto& alloc = tree->allocator();
    server::SyntaxIndexer indexer(*tree);
    indexer.collected.clear();

    appendIdentifierToken(storage, indexer, alloc, buffer.id, 0, "aa");
    appendIdentifierToken(storage, indexer, alloc, buffer.id, 3, "bbb");
    appendIdentifierToken(storage, indexer, alloc, buffer.id, 7, "c");

    auto data = server::computeSemanticTokenData(indexer, sm);
    REQUIRE(data.size() == 15);
    CHECK(data[0] == 0u);
    CHECK(data[1] == 0u);
    CHECK(data[5] == 1u);
    CHECK(data[6] == 0u);
    CHECK(data[10] == 1u);
    CHECK(data[11] == 0u);
}

TEST_CASE("SemanticTokenDeltaSkipsUnmappedMiddle") {
    SourceManager sm;
    auto buffer = sm.assignText("delta_skip.sv", "aa bb");
    auto tree = syntax::SyntaxTree::fromBuffer(buffer, sm);
    std::vector<parsing::Token> storage;
    storage.reserve(3);
    auto& alloc = tree->allocator();
    server::SyntaxIndexer indexer(*tree);
    indexer.collected.clear();

    appendIdentifierToken(storage, indexer, alloc, buffer.id, 0, "aa");
    appendSkippedKindToken(storage, indexer, alloc, buffer.id, 2, "", TK::EndOfFile);
    appendIdentifierToken(storage, indexer, alloc, buffer.id, 3, "bb");

    auto data = server::computeSemanticTokenData(indexer, sm);
    REQUIRE(data.size() == 10);
    CHECK(data[0] == 0u);
    CHECK(data[1] == 0u);
    CHECK(data[5] == 0u);
    CHECK(data[6] == 3u);
}

TEST_CASE("SemanticTokenDeltaBlankLineGap") {
    SourceManager sm;
    auto buffer = sm.assignText("delta_gap.sv", "aa\n\nbb");
    auto tree = syntax::SyntaxTree::fromBuffer(buffer, sm);
    std::vector<parsing::Token> storage;
    storage.reserve(2);
    auto& alloc = tree->allocator();
    server::SyntaxIndexer indexer(*tree);
    indexer.collected.clear();

    appendIdentifierToken(storage, indexer, alloc, buffer.id, 0, "aa");
    appendIdentifierToken(storage, indexer, alloc, buffer.id, 4, "bb");

    auto data = server::computeSemanticTokenData(indexer, sm);
    REQUIRE(data.size() == 10);
    CHECK(data[0] == 0u);
    CHECK(data[1] == 0u);
    CHECK(data[5] == 2u);
    CHECK(data[6] == 0u);
}
