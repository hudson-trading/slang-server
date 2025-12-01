// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

using namespace slang;

TEST_CASE("GotoDefinition_AfterPackageModification") {
    /// Test that goto definition still works after modifying a package file
    ServerHarness server("repo1");

    // Open the test file that uses package items
    auto cycleTest = server.openFile("cycle_test.sv");
    cycleTest.ensureSynced();

    // Find cursor on config_t (which is defined in base_pkg.sv)
    auto cursor = cycleTest.after("base_pkg::").before("config_t");

    // Get initial definitions
    auto initialDefs = cursor.getDefinitions();
    REQUIRE(!initialDefs.empty());
    REQUIRE(initialDefs.size() == 1);

    // Verify the definition points to base_pkg.sv
    auto& def = initialDefs[0];
    CHECK(def.targetUri.str().find("base_pkg.sv") != std::string::npos);

    // Store the original line number
    auto originalLine = def.targetRange.start.line;

    // Open and modify the package file by adding whitespace at the beginning
    auto basePkg = server.openFile("base_pkg.sv");
    basePkg.insert(0, "\n\n\n");
    basePkg.ensureSynced();

    // Get definitions again - should still work
    auto newDefs = cursor.getDefinitions();
    REQUIRE(!newDefs.empty());
    REQUIRE(newDefs.size() == 1);

    // Verify the definition still points to base_pkg.sv
    auto& newDef = newDefs[0];
    CHECK(newDef.targetUri.str().find("base_pkg.sv") != std::string::npos);

    // Verify the line number has shifted by 3 (the whitespace we added)
    CHECK(newDef.targetRange.start.line == originalLine + 3);
}

TEST_CASE("GotoDefinition_AfterPackageModification_Function") {
    /// Test goto definition on a function after modifying the package
    ServerHarness server("repo1");

    // Open the test file
    auto cycleTest = server.openFile("cycle_test.sv");
    cycleTest.ensureSynced();

    // Find cursor on create_config (which is defined in util_pkg.sv and exported via base_pkg)
    auto cursor = cycleTest.after("base_pkg::").before("create_config");

    // Get initial definitions
    auto initialDefs = cursor.getDefinitions();
    REQUIRE(!initialDefs.empty());

    // The definition should point to util_pkg.sv where create_config is defined
    auto& def = initialDefs[0];
    auto defPath = def.targetUri.str();
    REQUIRE(defPath.find("util_pkg.sv") != std::string::npos);

    auto originalLine = def.targetRange.start.line;

    // Modify the util_pkg file
    auto utilPkg = server.openFile("util_pkg.sv");
    utilPkg.insert(0, "\n\n");
    utilPkg.ensureSynced();

    // Get definitions again
    auto newDefs = cursor.getDefinitions();
    REQUIRE(!newDefs.empty());

    // Verify it still points to the right file and the line has shifted
    auto& newDef = newDefs[0];
    CHECK(newDef.targetUri.str().find("util_pkg.sv") != std::string::npos);
    CHECK(newDef.targetRange.start.line == originalLine + 2);
}

TEST_CASE("GotoDefinition_AfterBothFilesModified") {
    /// Test goto definition when both the source and target files are modified
    ServerHarness server("repo1");

    // Open the test file
    auto cycleTest = server.openFile("cycle_test.sv");
    cycleTest.ensureSynced();

    // Find cursor on result_t
    auto cursor = cycleTest.after("base_pkg::").before("result_t");

    // Get initial definitions
    auto initialDefs = cursor.getDefinitions();
    REQUIRE(!initialDefs.empty());

    auto& def = initialDefs[0];
    auto originalLine = def.targetRange.start.line;

    // Modify the source file (cycle_test.sv)
    cycleTest.insert(0, "\n");
    cycleTest.ensureSynced();

    // Modify the target file (util_pkg.sv where result_t is defined)
    auto utilPkg = server.openFile("util_pkg.sv");
    utilPkg.insert(0, "\n\n\n\n");
    utilPkg.ensureSynced();

    // Get the cursor again (it should have shifted in the modified file)
    auto newCursor = cycleTest.after("base_pkg::").before("result_t");

    // Get definitions from the new cursor position
    auto newDefs = newCursor.getDefinitions();
    REQUIRE(!newDefs.empty());

    // Verify the definition line has shifted correctly in the target file
    auto& newDef = newDefs[0];
    CHECK(newDef.targetRange.start.line == originalLine + 4);
}

TEST_CASE("GotoDefinition_CrossFileConsistency") {
    /// Test that goto definition is consistent when called from different files
    ServerHarness server("repo1");

    auto cycleTest = server.openFile("cycle_test.sv");
    cycleTest.ensureSynced();

    // Find config_t in cycle_test.sv
    auto cursor1 = cycleTest.after("base_pkg::").before("config_t");
    auto defs1 = cursor1.getDefinitions();
    REQUIRE(!defs1.empty());

    // Open base_pkg.sv and find config_t there
    auto basePkg = server.openFile("base_pkg.sv");
    basePkg.ensureSynced();

    auto cursor2 = basePkg.after("typedef struct packed").before("config_t");
    auto defs2 = cursor2.getDefinitions();
    REQUIRE(!defs2.empty());

    // Both should point to the same location
    CHECK(defs1[0].targetUri.str() == defs2[0].targetUri.str());
    CHECK(defs1[0].targetRange.start.line == defs2[0].targetRange.start.line);
    CHECK(defs1[0].targetRange.start.character == defs2[0].targetRange.start.character);

    // Modify base_pkg.sv
    basePkg.insert(0, "\n\n");
    basePkg.ensureSynced();

    // Get definitions again from both cursors
    auto newDefs1 = cursor1.getDefinitions();
    auto newCursor2 = basePkg.after("typedef struct packed").before("config_t");
    auto newDefs2 = newCursor2.getDefinitions();

    REQUIRE(!newDefs1.empty());
    REQUIRE(!newDefs2.empty());

    // Both should still point to the same (now shifted) location
    CHECK(newDefs1[0].targetUri.str() == newDefs2[0].targetUri.str());
    CHECK(newDefs1[0].targetRange.start.line == newDefs2[0].targetRange.start.line);
    CHECK(newDefs1[0].targetRange.start.character == newDefs2[0].targetRange.start.character);

    // Verify the shift
    CHECK(newDefs1[0].targetRange.start.line == defs1[0].targetRange.start.line + 2);
}
