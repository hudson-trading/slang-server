// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <cstdlib>
#include <functional>

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

TEST_CASE("onChange handles WholeDocument content change") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] foo;
endmodule
)");

    auto before = hdl.doc->getAnalysis();

    // Replace the entire document with a renamed symbol (the wire form Claude's
    // LSP client uses: { text } only, no range).
    std::string newText = R"(module test;
    logic [7:0] bar;
endmodule
)";
    hdl.replaceAll(newText);
    hdl.publishChanges();

    // Buffer was actually swapped (analysis invalidated)
    auto after = hdl.doc->getAnalysis();
    CHECK(before.get() != after.get());

    // Buffer text reflects the new contents (getText returns view including
    // the trailing null terminator, so size is +1).
    auto text = hdl.doc->getText();
    REQUIRE(text.size() == newText.size() + 1);
    CHECK(std::string_view{text.data(), newText.size()} == newText);

    // Follow-up symbol query reflects the renamed identifier
    auto syms = hdl.getSymbolTree();
    REQUIRE(!syms.empty());
    bool foundBar = false;
    bool foundFoo = false;
    std::function<void(const std::vector<lsp::DocumentSymbol>&)> walk =
        [&](const std::vector<lsp::DocumentSymbol>& nodes) {
            for (const auto& node : nodes) {
                if (node.name == "bar")
                    foundBar = true;
                if (node.name == "foo")
                    foundFoo = true;
                if (node.children.has_value())
                    walk(*node.children);
            }
        };
    walk(syms);
    CHECK(foundBar);
    CHECK_FALSE(foundFoo);
}

TEST_CASE("onChange handles mixed WholeDocument and Partial changes") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic [7:0] foo;
endmodule
)");

    // First queue a WholeDocument change that resets the buffer
    std::string replacement = R"(module test;
    logic [7:0] bar;
endmodule
)";
    hdl.replaceAll(replacement);

    // Then queue a Partial change that depends on the post-replacement buffer:
    // append a new line at the end of the file (offset 0,0 -> append empty
    // wouldn't exercise it; instead insert at a position that requires the new
    // buffer's line offsets).
    auto insertOffset = replacement.size();
    auto insertPos = hdl.getPosition(insertOffset);
    std::string appended = "// trailing\n";
    hdl.pending_changes.push_back({lsp::TextDocumentContentChangePartial{
        .range = lsp::Range{insertPos, insertPos}, .text = appended}});

    // Apply both in a single didChange request
    server.onDocDidChange(lsp::DidChangeTextDocumentParams{
        .textDocument = lsp::VersionedTextDocumentIdentifier{.uri = hdl.doc->getURI()},
        .contentChanges = hdl.pending_changes});
    hdl.pending_changes.clear();

    std::string expected = replacement + appended;
    auto text = hdl.doc->getText();
    REQUIRE(text.size() == expected.size() + 1);
    CHECK(std::string_view{text.data(), expected.size()} == expected);
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
    Config cfg;
    cfg.flagsByFile.value().push_back({"test", "--std=invalid_standard"});
    server.loadConfig(cfg);
    server.expectError("invalid value for --std option");
    server.expectError("Failed to parse config flags");
}
