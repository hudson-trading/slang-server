// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <cstdlib>

using namespace server;

TEST_CASE("BasicInsertion") {
    ServerHarness server;
    auto doc = server.openFile("top.sv", R"(module top;
    logic [3:0] a = 4'd?;
endmodule
)");

    doc.after("?;").write("    logic inserted\n");
    doc.publishChanges();

    doc.save(); // This validates the buffer
    doc.close();
}

TEST_CASE("FileLifeCycle") {
    ServerHarness server;
    auto hdl = server.openFile("tb.sv", R"(module top;
    logic [3:0] a = 4'd?;
endmodule
)");

    hdl.insert(26, "    logic inserted\n");
    hdl.publishChanges();
}

TEST_CASE("MultiChange") {
    ServerHarness server;
    auto doc = server.openFile("tb.sv", R"(module top;
    logic [3:0] a = 4'd?;
endmodule
)");
    doc.after("?;").write("    logic inserted;\n");
    doc.after("top;").write("    logic inserted2;\n");
    doc.after("endmodule").write("    module foo\n");
    doc.publishChanges();
    // This validates the buffer
    doc.save();
    doc.close();
}

TEST_CASE("MultiChangeWithAdditionsAndDeletions") {
    ServerHarness server;
    auto doc = server.openFile("test.sv", R"(module test;
    logic [7:0] data;
    logic clk;
    logic reset;
    logic enable;
endmodule
)");

    // Delete a line and add new content
    auto clkPos = doc.getText().find("    logic clk;\n");
    doc.erase(clkPos, clkPos + 15);
    doc.after("data;").write("\n    logic [31:0] counter;");
    doc.after("enable;").write("\n    logic valid;\n    logic ready;");

    doc.publishChanges();
    doc.save();
    doc.close();
}

TEST_CASE("DeletionFollowedByInsertionAtSameLocation") {
    ServerHarness server;
    auto doc = server.openFile("replace.sv", R"(module replace;
    logic old_signal;
    logic keep_this;
endmodule
)");

    // Find the position of "old_signal"
    auto oldSignalPos = doc.getText().find("old_signal");

    // Delete "old_signal" and replace with "new_signal"
    doc.erase(oldSignalPos, oldSignalPos + 10);
    doc.insert(oldSignalPos, "new_signal");

    // Add more content after the replacement
    doc.after("new_signal;").write("\n    logic [7:0] data_bus;");

    doc.publishChanges();
    doc.save();
    doc.close();
}

TEST_CASE("MultipleDeletionsAcrossDocument") {
    ServerHarness server;
    auto doc = server.openFile("deletions.sv", R"(module deletions;
    // Comment 1
    logic signal1;
    // Comment 2
    logic signal2;
    // Comment 3
    logic signal3;
    // Comment 4
    logic signal4;
endmodule
)");

    // Delete all comments (working backwards to maintain offsets)
    auto text = doc.getText();
    auto pos4 = text.find("    // Comment 4\n");
    auto pos3 = text.find("    // Comment 3\n");
    auto pos2 = text.find("    // Comment 2\n");
    auto pos1 = text.find("    // Comment 1\n");

    doc.erase(pos4, pos4 + 18);
    doc.erase(pos3, pos3 + 18);
    doc.erase(pos2, pos2 + 18);
    doc.erase(pos1, pos1 + 18);

    // Add some new content
    doc.after("signal2;")
        .write("\n    // New centralized comment\n    logic [31:0] combined_signal;");

    doc.publishChanges();
    doc.save();
    doc.close();
}
