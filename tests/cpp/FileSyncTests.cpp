// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/SlangDoc.h"
#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <cstdlib>

using namespace server;

TEST_CASE("BasicInsertion") {
    ServerHarness server;
    auto doc = server.openFile("top.sv", "module top;\n"
                                         "    logic [3:0] a = 4'd?;\n"
                                         "endmodule\n");

    doc.after("?;").write("    logic inserted\n");
    doc.publishChanges();

    doc.save(); // This validates the buffer
    doc.close();
}

TEST_CASE("FileLifeCycle") {
    ServerHarness server;
    auto hdl = server.openFile("tb.sv", "module top;\n"
                                        "    logic [3:0] a = 4'd?;\n"
                                        "endmodule\n");

    hdl.insert(26, "    logic inserted\n");
    hdl.publishChanges();
}

TEST_CASE("MultiChange") {
    ServerHarness server;
    auto doc = server.openFile("tb.sv", "module top;\n"
                                        "    logic [3:0] a = 4'd?;\n"
                                        "endmodule\n");
    doc.after("?;").write("    logic inserted;\n");
    doc.after("top;").write("    logic inserted2;\n");
    doc.after("endmodule").write("    module foo\n");
    doc.publishChanges();
    // This validates the buffer
    doc.save();
    doc.close();
}
