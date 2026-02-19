// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <cstdlib>

TEST_CASE("getAnalysis returns same object on repeated calls") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] data;
    logic clk;
endmodule
)");

    auto a1 = hdl.doc->getAnalysis();
    auto a2 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a2.get());

    // Third call should still return the same object
    auto a3 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a3.get());
}

TEST_CASE("getAnalysis returns new object after onChange") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] data;
endmodule
)");

    auto before = hdl.doc->getAnalysis();

    // Modify the document
    hdl.after("data;").write("\n    logic clk;");
    hdl.publishChanges();

    auto after = hdl.doc->getAnalysis();
    CHECK(before.get() != after.get());
}

TEST_CASE("getAnalysis with cross-file dependencies is stable") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("crossfile_module.sv");
    hdl.ensureSynced();

    auto a1 = hdl.doc->getAnalysis();
    auto a2 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a2.get());
}

TEST_CASE("LoadConfig") {
    ServerHarness server("basic_config");
    auto flags = server.getConfig().flags.value();
    std::cerr << "Config flags: " << flags << '\n';

    CHECK(flags.size() > 0);

#if _WIN32
    server.expectError(
        "include directory 'some/include/path': The system cannot find the path specified.");
#else
    server.expectError("include directory 'some/include/path': No such file or directory");
#endif
}

TEST_CASE("CapturedDriverErrors") {
    ServerHarness server;
    server.loadConfig(Config{.flags = {"--std=invalid_standard"}});
    server.expectError("invalid value for --std option");
    server.expectError("Failed to parse config flags: --std=invalid_standard");
}
