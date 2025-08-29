// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <cstdlib>

TEST_CASE("LoadConfig") {
    ServerHarness server("basic_config");
    auto flags = server.getConfig().flags.value();
    std::cerr << "Config flags: " << flags << '\n';

    CHECK(flags.size() > 0);

    server.expectError("include directory 'some/include/path': No such file or directory");
}

TEST_CASE("CapturedDriverErrors") {
    ServerHarness server;
    server.loadConfig(Config{.flags = {"--std=invalid_standard"}});
    server.expectError("invalid value for --std option");
    server.expectError("Failed to parse config flags: --std=invalid_standard");
}
