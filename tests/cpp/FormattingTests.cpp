#include "util/Formatting.h"
#include <catch2/catch_test_macros.hpp>

using namespace server;

TEST_CASE("ToCamelCase") {
    CHECK(toCamelCase("THEUPPERCASEMODULE") == "theuppercasemodule");
    CHECK(toCamelCase("UpperThenMoreUpper") == "upperThenMoreUpper");
    CHECK(toCamelCase("SOMEUpperCase") == "someUpperCase");
}
