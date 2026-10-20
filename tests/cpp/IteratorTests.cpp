// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Foreach variables retain declaration targets and index types", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("foreach.sv", R"(
module top;
    int values[3];
    int entries[string];
    string index;
    initial begin
        foreach (values[index]) values[index] = index;
        foreach (entries[key]) entries[key] = key.len();
        index = "outer";
    end
endmodule
)");
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
    for (auto [use, declaration, type] :
         {std::tuple{"index])", "index])", "int"}, std::tuple{"index] =", "index])", "int"},
          std::tuple{"index;", "index])", "int"}, std::tuple{"key])", "key])", "string"},
          std::tuple{"key] =", "key])", "string"}, std::tuple{"key.len", "key])", "string"},
          std::tuple{"index = \"outer\"", "index;", "string"}}) {
        CAPTURE(use);
        auto cursor = doc.before(use, doc.after("initial").m_offset);
        auto definitions = cursor.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetSelectionRange.start ==
              doc.before(declaration).getPosition());
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find(type) != std::string::npos);
    }
}

TEST_CASE("Array with iterators support implicit names and explicit declarations", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("with.sv", R"(
module top;
    int values[3];
    string item;
    int total;
    initial begin
        total = values.sum() with (item);
        total = values.sum(element) with (element);
        item = "outer";
    end
endmodule
)");
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
    for (auto name : {"item", "element"}) {
        CAPTURE(name);
        auto cursor = doc.after(std::string("with (") + name);
        auto hover = doc.getHoverAt(cursor.m_offset - 1);
        REQUIRE(hover);
        auto content = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CHECK(content.find("int") != std::string::npos);
        auto items = cursor.getCompletions();
        CHECK(std::ranges::any_of(items,
                                  [&](const auto& item) { return item.m_item.label == name; }));
    }
    auto definitions = doc.after("with (").getDefinitions();
    CHECK(definitions.empty());
    auto named = doc.before("element);", doc.after("with (item)").m_offset).getDefinitions();
    REQUIRE(named.size() == 1);
    CHECK(named.front().targetSelectionRange.start == doc.before("element) with").getPosition());
    auto outer = doc.before("item =").getDefinitions();
    REQUIRE(outer.size() == 1);
    CHECK(outer.front().targetSelectionRange.start == doc.before("item;").getPosition());
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
}

TEST_CASE("Array iterators retain nested and constraint contexts", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("nested_with.sv", R"(
class Limits;
    rand int weights[3];
    constraint bounded { weights.sum() with (int'(item > 0)) == 1; }
endclass
module top;
    int values[2][3];
    int total;
    initial total = values.sum(row) with (row.sum() with (item));
endmodule
)");
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
    for (auto use : {"item >", "row.sum", "item));"}) {
        CAPTURE(use);
        auto cursor = doc.before(use);
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find("int") != std::string::npos);
        auto items = cursor.getCompletions();
        std::string_view name = std::string_view(use).starts_with("row") ? "row" : "item";
        CHECK(std::ranges::any_of(items,
                                  [&](const auto& item) { return item.m_item.label == name; }));
    }
    auto definitions = doc.before("row.sum").getDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front().targetSelectionRange.start == doc.before("row) with").getPosition());
    CHECK(std::ranges::none_of(doc.getDiagnostics(), [](const auto& diagnostic) {
        return diagnostic.severity == lsp::DiagnosticSeverity::Error;
    }));
}
