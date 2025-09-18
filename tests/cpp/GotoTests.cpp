// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

using namespace slang;

TEST_CASE("FindSyntax") {
    /// Find the syntax at each location in the file
    ServerHarness server("");
    auto hdl = server.openFile("all.sv");

    SyntaxScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindSymbolRef") {
    /// Find the referenced symbol at each location in the file, if any.
    ServerHarness server("");
    auto hdl = server.openFile("all.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindSymbolRefHdl") {
    /// Find the referenced symbol at each location in the comms test file.
    ServerHarness server("");
    auto hdl = server.openFile("hdl_test.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("FindSymbolRefMacro") {
    /// Find the referenced symbol at each location in the macro test file.
    ServerHarness server("");
    auto hdl = server.openFile("macro_test.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}

TEST_CASE("LoadTransitivePackages") {
    /// Find the referenced symbol at each location in files with circular package dependencies.
    /// Tests the queue-based cycle detection in getDependentDocs.
    ServerHarness server("repo1"); // Use repo1 as workspace folder
    auto hdl = server.openFile("cycle_test.sv");

    SymbolRefScanner scanner;
    scanner.scanDocument(hdl);
}
