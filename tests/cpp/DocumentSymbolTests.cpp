// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Document symbol ranges cover their declarations") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module outer;
    class inner;
        int value;
    endclass
endmodule
)");

    auto symbols = hdl.getSymbolTree();
    REQUIRE(symbols.size() == 1);

    const auto& module = symbols.front();
    CHECK(module.name == "outer");
    CHECK(module.range.start == hdl.before("module").getPosition());
    CHECK(module.range.end == hdl.after("endmodule").getPosition());
    CHECK(module.selectionRange.start == hdl.before("outer").getPosition());
    CHECK(module.selectionRange.end == hdl.after("outer").getPosition());

    REQUIRE(module.children.has_value());
    REQUIRE(module.children->size() == 1);

    const auto& classSymbol = module.children->front();
    CHECK(classSymbol.name == "inner");
    CHECK(classSymbol.range.start == hdl.before("class").getPosition());
    CHECK(classSymbol.range.end == hdl.after("endclass").getPosition());
    CHECK(classSymbol.selectionRange.start == hdl.before("inner").getPosition());
    CHECK(classSymbol.selectionRange.end == hdl.after("inner").getPosition());
}

TEST_CASE("Document symbols include member declarations") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module outer;
    localparam int FIRST = 1, SECOND = 2;
    typedef logic [7:0] word_t;

    child #(
        .WIDTH(8)
    ) u_child (
        .value()
    ), u_child2 (
        .value()
    );

    always_ff @(posedge clk) begin : process_block
        logic local_value;
        if (enabled) begin : conditional_block
            for (int i = 0; i < 2; i++) begin : loop_block
                case (state)
                    default: local_value = '0;
                endcase
            end : loop_block
        end : conditional_block
        else if (retry) begin : retry_block
            local_value = '1;
        end : retry_block
        else begin : fallback_block
            local_value = '0;
        end : fallback_block
    end : process_block

    assign sink = source;
endmodule
)");

    auto symbols = hdl.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols.front().children.has_value());
    const auto& members = *symbols.front().children;
    REQUIRE(members.size() == 6);

    CHECK(members[0].name == "FIRST");
    CHECK(members[1].name == "SECOND");

    const auto& type = members[2];
    CHECK(type.name == "word_t");
    CHECK(type.kind == lsp::SymbolKind::Struct);
    CHECK(type.range.start == hdl.before("typedef").getPosition());
    CHECK(type.range.end == hdl.after("word_t;").getPosition());
    CHECK(type.selectionRange.start == hdl.before("typedef logic").getPosition());
    CHECK(type.selectionRange.end == hdl.after("typedef").getPosition());

    const auto& instantiation = members[3];
    CHECK(instantiation.name == "child");
    REQUIRE(instantiation.detail.has_value());
    CHECK(instantiation.detail->find("WIDTH") != std::string::npos);
    CHECK(instantiation.range.start == hdl.before("child #(").getPosition());
    CHECK(instantiation.range.end == hdl.after("    );").getPosition());
    CHECK(instantiation.selectionRange.start == hdl.before("child").getPosition());
    CHECK(instantiation.selectionRange.end == hdl.after("child").getPosition());
    REQUIRE(instantiation.children.has_value());
    REQUIRE(instantiation.children->size() == 2);

    const auto& firstInstance = (*instantiation.children)[0];
    CHECK(firstInstance.name == "u_child");
    CHECK(firstInstance.range.start == hdl.before("u_child").getPosition());
    CHECK(firstInstance.range.end == hdl.before("    ),").after("    )").getPosition());
    CHECK(firstInstance.selectionRange.start == hdl.before("u_child").getPosition());
    CHECK(firstInstance.selectionRange.end == hdl.after("u_child").getPosition());

    const auto& secondInstance = (*instantiation.children)[1];
    CHECK(secondInstance.name == "u_child2");
    CHECK(secondInstance.range.start == hdl.before("u_child2").getPosition());
    CHECK(secondInstance.range.end == hdl.before("    );").after("    )").getPosition());

    const auto& always = members[4];
    CHECK(always.name == "always_ff");
    CHECK(always.range.start == hdl.before("always_ff").getPosition());
    CHECK(always.range.end == hdl.after("end : process_block").getPosition());
    CHECK(always.selectionRange.start == hdl.before("always_ff").getPosition());
    CHECK(always.selectionRange.end == hdl.after("always_ff").getPosition());
    REQUIRE(always.children.has_value());
    REQUIRE(always.children->size() == 2);
    CHECK((*always.children)[0].name == "local_value");

    const auto& conditional = (*always.children)[1];
    CHECK(conditional.name == "if");
    CHECK(conditional.range.start == hdl.before("if (enabled)").getPosition());
    CHECK(conditional.range.end == hdl.after("end : fallback_block").getPosition());
    CHECK(conditional.selectionRange.end == hdl.after("if").getPosition());
    REQUIRE(conditional.children.has_value());
    REQUIRE(conditional.children->size() == 2);

    const auto& loop = conditional.children->front();
    CHECK(loop.name == "for");
    CHECK(loop.range.start == hdl.before("for (int").getPosition());
    CHECK(loop.range.end == hdl.after("end : loop_block").getPosition());
    CHECK(loop.selectionRange.end == hdl.after("for").getPosition());
    REQUIRE(loop.children.has_value());
    REQUIRE(loop.children->size() == 1);

    const auto& caseStatement = loop.children->front();
    CHECK(caseStatement.name == "case");
    CHECK(caseStatement.range.start == hdl.before("case (state)").getPosition());
    CHECK(caseStatement.range.end == hdl.after("endcase").getPosition());
    CHECK(caseStatement.selectionRange.end == hdl.after("case").getPosition());

    const auto& elseIf = (*conditional.children)[1];
    CHECK(elseIf.name == "else if");
    CHECK(elseIf.range.start == hdl.before("else if").getPosition());
    CHECK(elseIf.range.end == hdl.after("end : fallback_block").getPosition());
    CHECK(elseIf.selectionRange.end == hdl.after("else if").getPosition());
    REQUIRE(elseIf.children.has_value());
    REQUIRE(elseIf.children->size() == 1);

    const auto& fallback = elseIf.children->front();
    CHECK(fallback.name == "else");
    CHECK(fallback.range.start == hdl.before("else begin").getPosition());
    CHECK(fallback.range.end == hdl.after("end : fallback_block").getPosition());

    const auto& assign = members[5];
    CHECK(assign.name == "assign");
    CHECK(assign.range.start == hdl.before("assign").getPosition());
    CHECK(assign.range.end == hdl.after("assign sink = source;").getPosition());
}

TEST_CASE("Document symbols include typedef forms") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(package types;
    typedef struct packed {
        logic [7:0] field;
    } struct_t;
    typedef enum int {
        FIRST,
        GROUP[3],
        SECOND
    } enum_t;
    typedef class forward_t;
endpackage
)");

    auto symbols = hdl.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols.front().children.has_value());
    const auto& types = *symbols.front().children;
    REQUIRE(types.size() == 3);

    CHECK(types[0].name == "struct_t");
    CHECK(types[0].kind == lsp::SymbolKind::Struct);
    REQUIRE(types[0].children.has_value());
    REQUIRE(types[0].children->size() == 1);
    CHECK(types[0].children->front().name == "field");
    CHECK(types[0].children->front().kind == lsp::SymbolKind::Field);

    CHECK(types[1].name == "enum_t");
    CHECK(types[1].kind == lsp::SymbolKind::Enum);
    CHECK(types[1].range.start == hdl.before("typedef enum").getPosition());
    CHECK(types[1].range.end == hdl.after("} enum_t;").getPosition());
    CHECK(types[1].selectionRange.start == hdl.before("typedef enum").getPosition());
    CHECK(types[1].selectionRange.end == hdl.before("typedef enum").after("typedef").getPosition());
    REQUIRE(types[1].children.has_value());
    REQUIRE(types[1].children->size() == 3);
    CHECK((*types[1].children)[0].name == "FIRST");
    CHECK((*types[1].children)[1].name == "GROUP");
    CHECK((*types[1].children)[1].range.start == hdl.before("GROUP").getPosition());
    CHECK((*types[1].children)[1].range.end == hdl.after("GROUP[3]").getPosition());
    CHECK((*types[1].children)[2].name == "SECOND");

    CHECK(types[2].name == "forward_t");
    CHECK(types[2].kind == lsp::SymbolKind::Struct);
}

TEST_CASE("Document symbols recurse through conditional branches") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(module outer;
    always_ff @(posedge clk) begin
        if (reset) begin
            value <= '0;
        end else begin
            if (first) begin
                if (second) begin
                    value <= '1;
                end else if (third)
                    value <= '0;
            end
            if (fourth) begin
                value <= '0;
            end
            if (fifth) begin
                value <= '1;
            end
        end
    end
endmodule
)");

    auto symbols = hdl.getSymbolTree();
    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols.front().children.has_value());
    REQUIRE(symbols.front().children->size() == 1);

    const auto& always = symbols.front().children->front();
    CHECK(always.name == "always_ff");
    REQUIRE(always.children.has_value());
    REQUIRE(always.children->size() == 1);

    const auto& outerIf = always.children->front();
    CHECK(outerIf.name == "if");
    REQUIRE(outerIf.children.has_value());
    REQUIRE(outerIf.children->size() == 1);

    const auto& elseBranch = outerIf.children->front();
    CHECK(elseBranch.name == "else");
    REQUIRE(elseBranch.children.has_value());
    REQUIRE(elseBranch.children->size() == 3);
    CHECK((*elseBranch.children)[0].name == "if");
    CHECK((*elseBranch.children)[1].name == "if");
    CHECK((*elseBranch.children)[2].name == "if");

    const auto& firstIf = (*elseBranch.children)[0];
    REQUIRE(firstIf.children.has_value());
    REQUIRE(firstIf.children->size() == 1);
    const auto& secondIf = firstIf.children->front();
    CHECK(secondIf.name == "if");
    REQUIRE(secondIf.children.has_value());
    REQUIRE(secondIf.children->size() == 1);
    CHECK(secondIf.children->front().name == "else if");
}

TEST_CASE("Document symbols exclude macro-generated declarations") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(`define DECL(name) logic name;

module outer;
    `DECL(hidden)
    logic visible;
endmodule
)");

    auto symbols = hdl.getSymbolTree();
    REQUIRE(symbols.size() == 2);
    CHECK(symbols[0].name == "outer");
    REQUIRE(symbols[0].children.has_value());
    REQUIRE(symbols[0].children->size() == 1);
    CHECK(symbols[0].children->front().name == "visible");
    CHECK(symbols[1].name == "DECL");
}

TEST_CASE("Document symbols include macro-generated definitions") {
    ServerHarness server;
    auto hdl = server.openFile("test.sv", R"(`define DEFINE(name) \
    `define name

`DEFINE(GENERATED)
)");

    auto symbols = hdl.getSymbolTree();
    REQUIRE(symbols.size() == 2);
    CHECK(symbols[0].name == "DEFINE");
    CHECK(symbols[1].name == "GENERATED");
}
