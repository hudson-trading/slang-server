#include "util/Formatting.h"
#include <catch2/catch_test_macros.hpp>

#include "slang/numeric/ConstantValue.h"

using namespace server;

TEST_CASE("ToCamelCase") {
    CHECK(toCamelCase("THEUPPERCASEMODULE") == "theuppercasemodule");
    CHECK(toCamelCase("UpperThenMoreUpper") == "upperThenMoreUpper");
    CHECK(toCamelCase("SOMEUpperCase") == "someUpperCase");
}

TEST_CASE("FormatScalarBitValues") {
    CHECK(formatConstantValue(slang::ConstantValue(slang::SVInt(1, 0, false))) == "0");
    CHECK(formatConstantValue(slang::ConstantValue(slang::SVInt(1, 1, false))) == "1");
    CHECK(formatConstantValue(slang::ConstantValue(slang::SVInt(slang::logic_t::x))) == "1'bx");
    CHECK(formatConstantValue(slang::ConstantValue(slang::SVInt(slang::logic_t::z))) == "1'bz");
    CHECK(formatConstantValue(slang::ConstantValue(slang::SVInt(2, 1, false))) == "2'b1");
}
