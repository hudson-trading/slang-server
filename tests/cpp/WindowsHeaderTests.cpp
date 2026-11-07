// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

// Regression test for Windows header hygiene in WcpClient.h (issue #608).
//
// Other server headers put 'using namespace slang' at global scope, so
// slang::byte is visible wherever WcpClient.h is included. If WcpClient.h
// pulls in rpcndr.h (through a full windows.h), MinGW's global 'byte' typedef
// makes every unqualified 'byte' ambiguous and this file fails to compile.

// WcpClient.h expects rfl::json to be declared by its includer.
#include "rfl/json.hpp"

#include "slang/util/Util.h"
using namespace slang;

#include "ast/WcpClient.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <type_traits>

#if defined(min) || defined(max)
#    error "WcpClient.h must not leak the Windows min/max macros"
#endif

TEST_CASE("WcpClient header coexists with using namespace slang") {
    // Must resolve to slang::byte and not be ambiguous with a global typedef.
    byte b{0x2a};
    STATIC_REQUIRE(std::is_same_v<decltype(b), std::byte>);
    CHECK(std::to_integer<int>(b) == 0x2a);

    // std::min/std::max must stay usable after the Windows socket headers.
    CHECK(std::min(1, 2) == 1);
    CHECK(std::max(1, 2) == 2);
}
