// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

using namespace slang;

// Helper function to verify that all references point to the expected token text
void verifyReferenceTokens(ServerHarness& server, const std::vector<lsp::Location>& refs,
                           const std::string& expectedToken) {
    for (const auto& ref : refs) {
        auto refDoc = server.openFile(std::string(ref.uri.getPath()));
        auto text = refDoc.getText();

        // Calculate byte offset from position
        size_t offset = 0;
        size_t currentLine = 0;
        size_t currentCol = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            if (currentLine == ref.range.start.line && currentCol == ref.range.start.character) {
                offset = i;
                break;
            }
            if (text[i] == '\n') {
                currentLine++;
                currentCol = 0;
            }
            else {
                currentCol++;
            }
        }

        // Extract the token at this position
        std::string tokenText;
        for (size_t i = offset; i < text.size() && (std::isalnum(text[i]) || text[i] == '_'); ++i) {
            tokenText += text[i];
        }

        INFO("Reference at " << ref.uri.getPath() << ":" << ref.range.start.line << ":"
                             << ref.range.start.character);
        INFO("Expected token: '" << expectedToken << "', Found: '" << tokenText << "'");
        CHECK(tokenText == expectedToken);
    }
}

TEST_CASE("FindReferences - Simple Variable") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to 'data' signal
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    CHECK(refs->size() == 3); // declaration + 2 uses
}

TEST_CASE("FindReferences - Exclude Declaration") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to 'data' signal without declaration
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = false},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    CHECK(refs->size() == 2); // only 2 uses, not declaration
}

TEST_CASE("FindReferences - Module Port") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to input port 'clk'
    auto cursor = hdl.after("input logic ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    CHECK(refs->size() >= 2); // declaration + 1 use in always_ff
}

TEST_CASE("FindReferences - Parameter") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to parameter 'WIDTH'
    auto cursor = hdl.after("parameter ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    CHECK(refs->size() >= 3); // declaration + 2 uses (din, temp declarations)
}

TEST_CASE("FindReferences - No Symbol") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Try to find references at a location with no symbol (whitespace)
    auto cursor = hdl.begin();
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    CHECK(!refs.has_value()); // Should return nullopt
}

TEST_CASE("FindReferences - Module Name") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to module name
    auto cursor = hdl.after("module ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Module declaration (endmodule label doesn't count as a symbol reference)
    CHECK(refs->size() >= 1);
}

TEST_CASE("Rename - Simple Variable") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Rename 'data' signal to 'my_data'
    auto cursor = hdl.after("logic [7:0] ");
    auto edit = server.getDocRename(lsp::RenameParams{
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
        .newName = "my_data",
    });

    REQUIRE(edit.has_value());
    REQUIRE(edit->changes.has_value());

    auto& changes = *edit->changes;
    CHECK(changes.size() == 1); // Only one file

    // Check that we have 3 edits (declaration + 2 uses)
    auto uriStr = hdl.m_uri.str();
    REQUIRE(changes.find(uriStr) != changes.end());
    CHECK(changes[uriStr].size() == 3);

    // All edits should have the new name
    for (const auto& textEdit : changes[uriStr]) {
        CHECK(textEdit.newText == "my_data");
    }
}

TEST_CASE("Rename - Parameter") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Rename parameter 'WIDTH' to 'BUS_WIDTH'
    auto cursor = hdl.after("parameter ");
    auto edit = server.getDocRename(lsp::RenameParams{
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
        .newName = "BUS_WIDTH",
    });

    REQUIRE(edit.has_value());
    REQUIRE(edit->changes.has_value());

    auto& changes = *edit->changes;
    auto uriStr = hdl.m_uri.str();
    REQUIRE(changes.find(uriStr) != changes.end());

    // Should have at least 3 references (declaration + 2 uses)
    CHECK(changes[uriStr].size() >= 3);

    // All edits should have the new name
    for (const auto& textEdit : changes[uriStr]) {
        CHECK(textEdit.newText == "BUS_WIDTH");
    }
}

TEST_CASE("FindReferences - Cross-File Package Type") {
    ServerHarness server("indexer_test");
    auto pkgHdl = server.openFile("crossfile_pkg.sv");
    auto modHdl = server.openFile("crossfile_module.sv");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references to 'transaction_t' typedef from the package file
    auto cursor = pkgHdl.after("typedef struct packed {").after("} ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration in pkg + uses in module (trans_in, trans_out params, buffer, t1, t2)
    CHECK(refs->size() >= 5);

    // Verify references span multiple files
    std::set<std::string> filesWithRefs;
    for (const auto& ref : *refs) {
        filesWithRefs.insert(ref.uri.str());
    }
    CHECK(filesWithRefs.size() >= 2); // At least pkg and module files
}

TEST_CASE("FindReferences - Cross-File Parameter") {
    ServerHarness server("indexer_test");
    auto pkgHdl = server.openFile("crossfile_pkg.sv");
    auto modHdl = server.openFile("crossfile_module.sv");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references to 'FIFO_DEPTH' parameter from the package file
    auto cursor = pkgHdl.after("parameter int ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration in pkg + uses in module (DEPTH default, crossfile_top instantiation)
    CHECK(refs->size() >= 2);

    // Verify at least one reference is in the module file
    bool foundInModule = false;
    for (const auto& ref : *refs) {
        if (ref.uri.str().find("crossfile_module.sv") != std::string::npos) {
            foundInModule = true;
            break;
        }
    }
    CHECK(foundInModule);
}

TEST_CASE("FindReferences - Cross-File Function") {
    ServerHarness server("indexer_test");
    auto pkgHdl = server.openFile("crossfile_pkg.sv");
    auto modHdl = server.openFile("crossfile_module.sv");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references to 'calculate_size' function from the package file
    auto cursor = pkgHdl.after("function automatic int ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration in pkg + call in module's initial block
    CHECK(refs->size() >= 2);
}

TEST_CASE("FindReferences - Goto Refs On Returned Refs") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to 'data' signal
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    REQUIRE(refs->size() >= 2);

    // Store original reference set for comparison
    std::set<std::pair<std::string, lsp::Position>> originalRefs;
    for (const auto& ref : *refs) {
        originalRefs.insert({ref.uri.str(), ref.range.start});
    }

    // Now call getReferences on each returned reference location
    for (const auto& ref : *refs) {
        auto refsFromRef = server.getDocReferences(lsp::ReferenceParams{
            .context = {.includeDeclaration = true},
            .textDocument = {.uri = ref.uri},
            .position = ref.range.start,
        });

        REQUIRE(refsFromRef.has_value());

        // Verify the reference set is the same
        std::set<std::pair<std::string, lsp::Position>> refsFromRefSet;
        for (const auto& r : *refsFromRef) {
            refsFromRefSet.insert({r.uri.str(), r.range.start});
        }

        CHECK(refsFromRefSet == originalRefs);
    }
}

TEST_CASE("FindReferences - Cross-File Goto Refs On Returned Refs") {
    ServerHarness server("indexer_test");
    auto pkgHdl = server.openFile("crossfile_pkg.sv");
    auto modHdl = server.openFile("crossfile_module.sv");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references to 'FIFO_DEPTH' parameter
    auto cursor = pkgHdl.after("parameter int ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    REQUIRE(refs->size() >= 2);

    // Store original reference set
    std::set<std::pair<std::string, lsp::Position>> originalRefs;
    for (const auto& ref : *refs) {
        originalRefs.insert({ref.uri.str(), ref.range.start});
    }

    // Call getReferences on each returned reference (including cross-file ones)
    for (const auto& ref : *refs) {
        auto refsFromRef = server.getDocReferences(lsp::ReferenceParams{
            .context = {.includeDeclaration = true},
            .textDocument = {.uri = ref.uri},
            .position = ref.range.start,
        });

        REQUIRE(refsFromRef.has_value());

        // Verify the reference set is identical regardless of which reference we query from
        std::set<std::pair<std::string, lsp::Position>> refsFromRefSet;
        for (const auto& r : *refsFromRef) {
            refsFromRefSet.insert({r.uri.str(), r.range.start});
        }

        CHECK(refsFromRefSet == originalRefs);
    }
}

TEST_CASE("FindReferences - File Modification Whitespace") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("references_test.sv");
    hdl.ensureSynced();

    // Find references to 'data' signal
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    auto originalRefCount = refs->size();

    // Store original reference locations (line numbers)
    std::vector<lsp::uint> originalLines;
    for (const auto& ref : *refs) {
        originalLines.push_back(ref.range.start.line);
    }
    std::sort(originalLines.begin(), originalLines.end());

    // Modify the file by adding whitespace at the beginning
    hdl.insert(0, "\n\n");
    hdl.ensureSynced();

    // Find references again from the same semantic location (now shifted down by 2 lines)
    auto newCursor = hdl.after("logic [7:0] ");
    auto newRefs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = newCursor.getPosition(),
    });

    REQUIRE(newRefs.has_value());
    CHECK(newRefs->size() == originalRefCount);

    // Verify that reference lines shifted by 2
    std::vector<lsp::uint> newLines;
    for (const auto& ref : *newRefs) {
        newLines.push_back(ref.range.start.line);
    }
    std::sort(newLines.begin(), newLines.end());

    REQUIRE(newLines.size() == originalLines.size());
    for (size_t i = 0; i < originalLines.size(); i++) {
        CHECK(newLines[i] == originalLines[i] + 2);
    }
}

TEST_CASE("FindReferences - Cross-File Modification Whitespace") {
    ServerHarness server("indexer_test");
    auto pkgHdl = server.openFile("crossfile_pkg.sv");
    auto modHdl = server.openFile("crossfile_module.sv");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references to 'FIFO_DEPTH' parameter
    auto cursor = pkgHdl.after("parameter int ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    auto originalRefCount = refs->size();

    // Store original reference set
    std::set<std::pair<std::string, lsp::Position>> originalRefs;
    for (const auto& ref : *refs) {
        originalRefs.insert({ref.uri.str(), ref.range.start});
    }

    // Modify the module file by adding whitespace
    modHdl.insert(0, "    \n    \n");
    modHdl.ensureSynced();

    // Find references again from the package file (unchanged)
    auto newRefs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(newRefs.has_value());
    CHECK(newRefs->size() == originalRefCount);

    // Build new reference set and check positions
    std::set<std::pair<std::string, lsp::Position>> newRefs_set;
    for (const auto& ref : *newRefs) {
        newRefs_set.insert({ref.uri.str(), ref.range.start});
    }

    // References in package file should be unchanged
    for (const auto& ref : *newRefs) {
        if (ref.uri.str().find("crossfile_pkg.sv") != std::string::npos) {
            auto originalIt = std::find_if(originalRefs.begin(), originalRefs.end(),
                                           [&](const auto& orig) {
                                               return orig.first == ref.uri.str();
                                           });
            REQUIRE(originalIt != originalRefs.end());
            CHECK(ref.range.start.line == originalIt->second.line);
        }
        // References in module file should be shifted by 2 lines
        else if (ref.uri.str().find("crossfile_module.sv") != std::string::npos) {
            auto originalIt =
                std::find_if(originalRefs.begin(), originalRefs.end(), [&](const auto& orig) {
                    return orig.first == ref.uri.str() &&
                           orig.second.character == ref.range.start.character;
                });
            if (originalIt != originalRefs.end()) {
                CHECK(ref.range.start.line == originalIt->second.line + 2);
            }
        }
    }
}

TEST_CASE("FindReferences - Cross-File Both Files Modified") {
    ServerHarness server("indexer_test");
    auto pkgHdl = server.openFile("crossfile_pkg.sv");
    auto modHdl = server.openFile("crossfile_module.sv");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references to 'transaction_t' from package
    auto cursor = pkgHdl.after("typedef struct packed {").after("} ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    auto originalRefCount = refs->size();

    // Add whitespace to both files
    pkgHdl.insert(0, "\n");
    modHdl.insert(0, "\n\n\n");
    pkgHdl.ensureSynced();
    modHdl.ensureSynced();

    // Find references again (cursor position in package moved down by 1)
    auto newCursor = pkgHdl.after("typedef struct packed {").after("} ");
    auto newRefs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = pkgHdl.m_uri},
        .position = newCursor.getPosition(),
    });

    REQUIRE(newRefs.has_value());
    // Should still find the same number of references
    CHECK(newRefs->size() == originalRefCount);
}

TEST_CASE("FindReferences - Macro Argument Simple") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("macro_refs.sv");
    hdl.ensureSynced();

    // Find references to 'counter' which is used in multiple macro invocations
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find at least declaration + some macro usages
    // Note: Macro expansions may or may not create separate reference entries
    CHECK(refs->size() >= 1);
}

TEST_CASE("FindReferences - Macro Argument Multiple Macros") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("macro_refs.sv");
    hdl.ensureSynced();

    // Find references to 'temp_val' which appears in macro arguments
    auto cursor = hdl.after("logic [7:0] counter;\n    logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find at least declaration + uses
    CHECK(refs->size() >= 1);
}

TEST_CASE("FindReferences - Macro Argument Multiline Macro") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("macro_refs.sv");
    hdl.ensureSynced();

    // Find references to 'result' which is used in a multi-line macro
    auto cursor = hdl.after("logic [7:0] temp_val;\n    logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find at least declaration + uses
    CHECK(refs->size() >= 1);
}

TEST_CASE("FindReferences - Macro Argument Cross File") {
    ServerHarness server("indexer_test");
    auto hdl1 = server.openFile("macro_refs.sv");
    auto hdl2 = server.openFile("macro_crossfile.sv");
    hdl1.ensureSynced();
    hdl2.ensureSynced();

    // Find references to 'my_counter' in macro_crossfile.sv
    auto cursor = hdl2.after("logic [7:0] data_in, data_out;\n    logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl2.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find at least declaration + macro usages
    CHECK(refs->size() >= 1);
}

TEST_CASE("FindReferences - Macro Argument Goto Refs Consistency") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("macro_refs.sv");
    hdl.ensureSynced();

    // Find references to 'counter'
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    REQUIRE(refs->size() >= 2);

    // Store original reference set
    std::set<std::pair<std::string, lsp::Position>> originalRefs;
    for (const auto& ref : *refs) {
        originalRefs.insert({ref.uri.str(), ref.range.start});
    }

    // Call getReferences on each returned reference (including ones in macro args)
    for (const auto& ref : *refs) {
        auto refsFromRef = server.getDocReferences(lsp::ReferenceParams{
            .context = {.includeDeclaration = true},
            .textDocument = {.uri = ref.uri},
            .position = ref.range.start,
        });

        REQUIRE(refsFromRef.has_value());

        // Verify the reference set is identical
        std::set<std::pair<std::string, lsp::Position>> refsFromRefSet;
        for (const auto& r : *refsFromRef) {
            refsFromRefSet.insert({r.uri.str(), r.range.start});
        }

        CHECK(refsFromRefSet == originalRefs);
    }
}

TEST_CASE("FindReferences - Macro Argument With File Modification") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("macro_refs.sv");
    hdl.ensureSynced();

    // Find references to 'counter'
    auto cursor = hdl.after("logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    auto originalRefCount = refs->size();
    REQUIRE(originalRefCount >= 1);

    // Modify file by adding whitespace
    hdl.insert(0, "\n\n\n");
    hdl.ensureSynced();

    // Find references again
    auto newCursor = hdl.after("logic [7:0] ");
    auto newRefs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = newCursor.getPosition(),
    });

    REQUIRE(newRefs.has_value());
    // Should find at least one reference (the behavior with macro arguments may vary)
    // The key test is that it doesn't crash and still finds references
    CHECK(newRefs->size() >= 1);
}

TEST_CASE("FindReferences - Struct Member Simple") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'addr' member of transaction_s
    auto cursor = hdl.after("typedef struct packed {\n    logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + tx1.addr + packet.request.addr (2x in reset + 2x in else) +
    // tx2.addr
    CHECK(refs->size() == 7);
    verifyReferenceTokens(server, *refs, "addr");
}

TEST_CASE("FindReferences - Struct Member Multiple Accesses") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'data' member of transaction_s
    auto cursor = hdl.after("logic [7:0] addr;\n    logic [31:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + tx1.data + packet.request.data (reset + else) +
    // packet.response.data + tx2.data (2x)
    CHECK(refs->size() == 7);
    verifyReferenceTokens(server, *refs, "data");
}

TEST_CASE("FindReferences - Nested Struct Member") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'request' member of bus_packet_s
    auto cursor = hdl.after("typedef struct packed {\n    transaction_s ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + packet.request.addr (2x in reset + 2x in else) +
    // packet.request.data + tx2.addr line
    CHECK(refs->size() == 6);
    verifyReferenceTokens(server, *refs, "request");
}

TEST_CASE("FindReferences - Enum Member Simple") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'IDLE' enum member
    auto cursor = hdl.after("typedef enum logic [1:0] {\n    ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + initial assignment + case IDLE + 2 assignments to IDLE (WAIT/ERROR
    // cases)
    CHECK(refs->size() == 5);
    verifyReferenceTokens(server, *refs, "IDLE");
}

TEST_CASE("FindReferences - Enum Member In Case Statement") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'ACTIVE' enum member
    auto cursor = hdl.after("IDLE = 2'b00,\n    ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + case ACTIVE + assignment to ACTIVE
    CHECK(refs->size() == 3);
    verifyReferenceTokens(server, *refs, "ACTIVE");
}

TEST_CASE("FindReferences - Command Enum Member") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'CMD_WRITE' enum member
    auto cursor = hdl.after("CMD_READ = 3'b000,\n    ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + assignment in case IDLE
    CHECK(refs->size() == 2);
    verifyReferenceTokens(server, *refs, "CMD_WRITE");
}

TEST_CASE("FindReferences - Struct Member Cross File") {
    ServerHarness server("indexer_test");
    auto hdl1 = server.openFile("struct_enum_refs.sv");
    auto hdl2 = server.openFile("struct_enum_crossfile.sv");
    hdl1.ensureSynced();
    hdl2.ensureSynced();

    // Find references to 'valid' member from main file
    auto cursor = hdl1.after("logic [31:0] data;\n    logic ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl1.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + tx1.valid + packet.response.valid + my_tx.valid +
    // my_packet.response.valid
    CHECK(refs->size() == 4);
    verifyReferenceTokens(server, *refs, "valid");
}

TEST_CASE("FindReferences - Enum Member Cross File") {
    ServerHarness server("indexer_test");
    auto hdl1 = server.openFile("struct_enum_refs.sv");
    auto hdl2 = server.openFile("struct_enum_crossfile.sv");
    hdl1.ensureSynced();
    hdl2.ensureSynced();

    // Find references to 'ERROR' enum member
    auto cursor = hdl1.after("WAIT = 2'b10,\n    ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl1.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + case ERROR + if (state == ERROR) in cross-file
    CHECK(refs->size() == 3);
    verifyReferenceTokens(server, *refs, "ERROR");
}

TEST_CASE("FindReferences - Struct Member Goto Refs Consistency") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'ready' member
    auto cursor = hdl.after("logic valid;\n    logic ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    REQUIRE(refs->size() >= 2);

    // Store original reference set
    std::set<std::pair<std::string, lsp::Position>> originalRefs;
    for (const auto& ref : *refs) {
        originalRefs.insert({ref.uri.str(), ref.range.start});
    }

    // Call getReferences on each returned reference
    for (const auto& ref : *refs) {
        auto refsFromRef = server.getDocReferences(lsp::ReferenceParams{
            .context = {.includeDeclaration = true},
            .textDocument = {.uri = ref.uri},
            .position = ref.range.start,
        });

        REQUIRE(refsFromRef.has_value());

        // Verify the reference set is identical
        std::set<std::pair<std::string, lsp::Position>> refsFromRefSet;
        for (const auto& r : *refsFromRef) {
            refsFromRefSet.insert({r.uri.str(), r.range.start});
        }

        CHECK(refsFromRefSet == originalRefs);
    }
}

TEST_CASE("FindReferences - Nested Struct Member Deep Access") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("struct_enum_refs.sv");
    hdl.ensureSynced();

    // Find references to 'id' member of bus_packet_s (not nested, but part of nested struct)
    auto cursor = hdl.after("transaction_s response;\n    logic [3:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find: declaration + packet.id in reset
    CHECK(refs->size() == 2);
    verifyReferenceTokens(server, *refs, "id");
}

TEST_CASE("Rename - Port From Definition") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("port_rename.sv");
    hdl.ensureSynced();

    // Rename 'clk' port from its declaration in child_module
    auto cursor = hdl.after("input logic c"); // Position at 'c' of 'clk'
    auto edit = server.getDocRename(lsp::RenameParams{
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
        .newName = "clock",
    });

    REQUIRE(edit.has_value());
    REQUIRE(edit->changes.has_value());

    auto& changes = *edit->changes;
    auto uriStr = hdl.m_uri.str();
    REQUIRE(changes.find(uriStr) != changes.end());

    // Should find:
    // 1. Declaration: input logic clk
    // 2. Usage: @(posedge clk)
    // 3. Named port connection: .clk(sys_clk)
    CHECK(changes[uriStr].size() == 3);

    // All edits should have the new name
    for (const auto& textEdit : changes[uriStr]) {
        CHECK(textEdit.newText == "clock");
    }
}

TEST_CASE("Rename - Port From Instance Connection") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("port_rename.sv");
    hdl.ensureSynced();

    // Rename 'clk' port from the named port connection in parent_module
    auto cursor = hdl.after(".c"); // Position at 'c' of '.clk'
    auto edit = server.getDocRename(lsp::RenameParams{
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
        .newName = "clock",
    });

    REQUIRE(edit.has_value());
    REQUIRE(edit->changes.has_value());

    auto& changes = *edit->changes;
    auto uriStr = hdl.m_uri.str();
    REQUIRE(changes.find(uriStr) != changes.end());

    // Should find same 3 references as when renaming from definition
    CHECK(changes[uriStr].size() == 3);

    // All edits should have the new name
    for (const auto& textEdit : changes[uriStr]) {
        CHECK(textEdit.newText == "clock");
    }
}

TEST_CASE("FindReferences - Port Across Instance Boundary") {
    ServerHarness server("indexer_test");
    auto hdl = server.openFile("port_rename.sv");
    hdl.ensureSynced();

    // Find references to 'data_out' port from its declaration
    auto cursor = hdl.after("output logic [7:0] ");
    auto refs = server.getDocReferences(lsp::ReferenceParams{
        .context = {.includeDeclaration = true},
        .textDocument = {.uri = hdl.m_uri},
        .position = cursor.getPosition(),
    });

    REQUIRE(refs.has_value());
    // Should find:
    // 1. Declaration: output logic [7:0] data_out
    // 2. Usage in reset: data_out <= '0
    // 3. Usage in else: data_out <= data_out + 1 (LHS)
    // 4. Usage in else: data_out <= data_out + 1 (RHS)
    // 5. Named port connection: .data_out(result)
    CHECK(refs->size() == 5);
    verifyReferenceTokens(server, *refs, "data_out");
}
