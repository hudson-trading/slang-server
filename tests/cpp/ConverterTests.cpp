// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Converters.h"
#include <catch2/catch_test_macros.hpp>

#include "slang/text/SourceManager.h"

TEST_CASE("LSP positions use raw source lines") {
    slang::SourceManager sourceManager;
    auto buffer = sourceManager.assignText("source.sv", "first\n`line\nthird\n");
    REQUIRE(buffer);

    auto directive = sourceManager.getSourceLocation(buffer.id, 2, 1);
    auto location = sourceManager.getSourceLocation(buffer.id, 3, 3);
    REQUIRE(directive);
    REQUIRE(location);
    sourceManager.addLineDirective(*directive, 100, "mapped.sv", 0);

    CHECK(sourceManager.getLineNumber(*location) == 100);
    auto position = server::toPosition(*location, sourceManager);
    CHECK(position.line == 2);
    CHECK(position.character == 2);

    auto range = server::toRange(*location, sourceManager, 2);
    CHECK(range.start.line == 2);
    CHECK(range.start.character == 2);
    CHECK(range.end.line == 2);
    CHECK(range.end.character == 4);
}

TEST_CASE("LSP positions resolve macro locations") {
    slang::SourceManager sourceManager;
    auto buffer = sourceManager.assignText("source.sv", "first\nmacro(D)\n");
    REQUIRE(buffer);

    auto location = sourceManager.getSourceLocation(buffer.id, 2, 7);
    REQUIRE(location);
    slang::SourceRange expansionRange(*location, *location + 1);
    auto macroLocation = sourceManager.createExpansionLoc(*location, expansionRange, "M");

    CHECK(sourceManager.getRawLineNumber(macroLocation) == 0);
    auto position = server::toPosition(macroLocation, sourceManager);
    CHECK(position.line == 1);
    CHECK(position.character == 6);

    auto range = server::toRange(slang::SourceRange(macroLocation, macroLocation + 1),
                                 sourceManager);
    CHECK(range.start.line == 1);
    CHECK(range.start.character == 6);
    CHECK(range.end.line == 1);
    CHECK(range.end.character == 7);

    auto lengthRange = server::toRange(macroLocation, sourceManager, 1);
    CHECK(lengthRange.start.line == 1);
    CHECK(lengthRange.start.character == 6);
    CHECK(lengthRange.end.line == 1);
    CHECK(lengthRange.end.character == 7);
}
