// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Implicit function return variables retain declaration targets", "[scan-gaps]") {
    for (bool inClass : {false, true}) {
        CAPTURE(inClass);
        ServerHarness server;
        auto doc = server.openFile("return_values.sv",
                                   std::string(inClass ? "class Sample;" : "module sample;") + R"(
    typedef struct packed { int field; } result_t;
    function automatic int compute(int value);
        begin : body
            compute = value ? compute(value - 1) : 0;
        end
    endfunction
    function automatic result_t assemble(int value);
        assemble.field = value;
    endfunction
)" + (inClass ? "endclass" : "endmodule"));
        for (auto [use, declaration] :
             {std::pair{"compute =", "compute(int"}, std::pair{"compute(value", "compute(int"},
              std::pair{"assemble.field", "assemble(int"}}) {
            CAPTURE(use);
            auto cursor = doc.before(use);
            auto definitions = cursor.getDefinitions();
            REQUIRE(definitions.size() == 1);
            CHECK(definitions.front().targetSelectionRange.start ==
                  doc.before(declaration).getPosition());
            auto hover = doc.getHoverAt(cursor.m_offset);
            REQUIRE(hover);
            auto contents = rfl::get<lsp::MarkupContent>(hover->contents).value;
            CHECK(contents.find("function automatic") != std::string::npos);
        }
        auto field = doc.after("assemble.");
        auto definitions = field.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetSelectionRange.start == doc.before("field;").getPosition());
    }
}
