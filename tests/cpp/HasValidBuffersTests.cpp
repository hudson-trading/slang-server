// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/SlangExtensions.h"
#include "utils/ServerHarness.h"

TEST_CASE("hasValidBuffers: null tree returns false") {
    slang::SourceManager sm;
    std::shared_ptr<slang::syntax::SyntaxTree> tree = nullptr;
    CHECK_FALSE(server::hasValidBuffers(sm, tree));
}

TEST_CASE("hasValidBuffers: freshly parsed tree is valid") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic a;
endmodule
)");

    auto tree = hdl.doc->getSyntaxTree();
    CHECK(server::hasValidBuffers(server.sourceManager(), tree));
}

TEST_CASE("hasValidBuffers: stale after replaceBuffer") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module test;
    logic a;
endmodule
)");

    auto tree = hdl.doc->getSyntaxTree();
    CHECK(server::hasValidBuffers(server.sourceManager(), tree));

    // Modify the document - this calls replaceBuffer, marking old data stale
    hdl.after("logic a;").write("\n    logic b;");
    hdl.publishChanges();

    // The old tree should now have stale buffers
    CHECK_FALSE(server::hasValidBuffers(server.sourceManager(), tree));

    // A new tree should be valid
    auto newTree = hdl.doc->getSyntaxTree();
    CHECK(server::hasValidBuffers(server.sourceManager(), newTree));
}

TEST_CASE("hasValidBuffers: unresolved include does not cause false negative") {
    ServerHarness server;
    // Include a file that doesn't exist - this will produce an include with invalid BufferID
    auto hdl = server.openFile("test.sv", R"(`include "nonexistent_file.svh"
module test;
    logic a;
endmodule
)");

    auto tree = hdl.doc->getSyntaxTree();
    auto includes = tree->getIncludeDirectives();

    // Verify there is an include directive with an invalid buffer
    bool hasInvalidInclude = false;
    for (const auto& inc : includes) {
        if (!inc.buffer.id.valid()) {
            hasInvalidInclude = true;
        }
    }
    CHECK(hasInvalidInclude);

    // The tree should still report valid buffers (invalid includes are skipped)
    CHECK(server::hasValidBuffers(server.sourceManager(), tree));
}

TEST_CASE("hasValidBuffers: valid include stays valid") {
    ServerHarness server("two_layer_include");
    auto hdl = server.openFile("top.sv");

    auto tree = hdl.doc->getSyntaxTree();
    auto includes = tree->getIncludeDirectives();

    // Should have at least one valid include
    bool hasValidInclude = false;
    for (const auto& inc : includes) {
        if (inc.buffer.id.valid()) {
            hasValidInclude = true;
        }
    }
    CHECK(hasValidInclude);

    CHECK(server::hasValidBuffers(server.sourceManager(), tree));
}

TEST_CASE("hasValidBuffers: getAnalysis is stable after fix") {
    ServerHarness server;
    // Include a nonexistent file - before the fix, this would cause hasValidBuffers
    // to check an invalid BufferID and return false, causing getAnalysis to recreate
    // the analysis on every call
    auto hdl = server.openFile("test.sv", R"(`include "does_not_exist.svh"
module test;
    logic a;
endmodule
)");

    auto a1 = hdl.doc->getAnalysis();
    auto a2 = hdl.doc->getAnalysis();
    CHECK(a1.get() == a2.get());
}
