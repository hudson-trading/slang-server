//------------------------------------------------------------------------------
// gen_config_main.cpp
// Standalone tool to generate JSON schema from Config.h
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "Config.h"
#include <iostream>
#include <rfl/DefaultIfMissing.hpp>
#include <rfl/json.hpp>

int main() {
    try {
        const std::string schema = rfl::json::to_schema<Config, rfl::DefaultIfMissing>(
            rfl::json::pretty | YYJSON_WRITE_PRETTY_TWO_SPACES);
        std::cout << schema << '\n';
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error generating config schema: " << e.what() << '\n';
        return 1;
    }
}
