// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Return assignment patterns use the enclosing function type", "[assignment-patterns]") {
    ServerHarness server;
    auto doc = server.openFile("return_patterns.sv", R"(
package records;
    typedef struct { int code; bit enabled; } entry_t;
    typedef struct { entry_t entries[1]; } batch_t;
    function automatic entry_t make(int code);
        if (code) begin
            return '{code: code, enabled: 1};
        end
        return '{code: code, enabled: 0};
    endfunction
    function automatic batch_t make_batch(int code);
        return '{entries: '{'{code: code, enabled: 1}}};
    endfunction
    class Builder #(type T = entry_t);
        function T make(int code);
            return '{code: code, enabled: 1};
        endfunction
    endclass
endpackage
)");
    auto source = doc.getText();
    for (size_t offset = source.find("code:"); offset != std::string::npos;
         offset = source.find("code:", offset + 1)) {
        auto key = doc.before("code:", offset);
        auto definitions = key.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetSelectionRange.start == doc.before("code;").getPosition());
        auto hover = doc.getHoverAt(key.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find("**Field** `code`") !=
              std::string::npos);
        auto items = key.getCompletions();
        CHECK(std::ranges::any_of(items,
                                  [](const auto& item) { return item.m_item.label == "code"; }));
        auto value = doc.getDefinitionInfoAt(key.after("code: ").m_offset);
        REQUIRE(value);
        REQUIRE(value->symbol());
        CHECK(value->symbol()->kind == slang::ast::SymbolKind::FormalArgument);
    }
}

TEST_CASE("Unfinished return patterns complete fields and delimiters", "[assignment-patterns]") {
    ServerHarness server;
    auto doc = server.openFile("unfinished_return.sv", R"(
package records;
    typedef struct { int code; bit enabled; } entry_t;
    function automatic entry_t make();
        return '{)");
    auto items = doc.after("return '{").getCompletions("{");
    REQUIRE(items.size() == 1);
    CHECK(items.front().m_item.label == "'{code, enabled}");
    CHECK(items.front().m_item.insertText == "\n\tcode: $1,\n\tenabled: $2\n\\};");
}

TEST_CASE("Array assignment patterns resolve nested struct keys", "[assignment-patterns]") {
    for (const auto* declaration : {
             "entry_t entries[2] = '{'{code: code, enabled: enabled}, "
             "'{code: code, enabled: enabled}};",
             "entry_t entries[1][1] = '{'{'{code: code, enabled: enabled}}};",
             "entry_t entries[2] = '{2{'{code: code, enabled: enabled}}};",
             "entry_t entries[2] = '{default: '{code: code, enabled: enabled}};",
         }) {
        CAPTURE(declaration);
        ServerHarness server;
        auto doc = server.openFile("array_patterns.sv", std::string(R"(
package records;
    typedef struct { int code; bit enabled; } entry_t;
    int code = 7;
    bit enabled = 1;
)") + declaration + "\nendpackage\n");
        auto source = doc.getText();
        for (std::string field : {"code", "enabled"}) {
            auto expectedField = doc.before(field + ";").getPosition();
            auto expectedValue = doc.before(field + " =").getPosition();
            for (size_t offset = source.find(field + ":"); offset != std::string::npos;
                 offset = source.find(field + ":", offset + 1)) {
                auto key = doc.before(field + ":", offset);
                auto definitions = key.getDefinitions();
                REQUIRE(definitions.size() == 1);
                CHECK(definitions.front().targetSelectionRange.start == expectedField);
                auto hover = doc.getHoverAt(key.m_offset);
                REQUIRE(hover);
                CHECK(rfl::get<lsp::MarkupContent>(hover->contents)
                          .value.find("**Field** `" + field + "`") != std::string::npos);

                auto completions = key.getCompletions();
                auto completion = std::ranges::find(completions, field, [](const auto& item) {
                    return item.m_item.label;
                });
                REQUIRE(completion != completions.end());
                CHECK(completion->m_item.kind == lsp::CompletionItemKind::Field);
                REQUIRE(completion->m_item.textEdit);
                CHECK(rfl::get<lsp::TextEdit>(*completion->m_item.textEdit).newText == field);

                auto valueDefinitions = key.after(field + ": ").getDefinitions();
                REQUIRE(valueDefinitions.size() == 1);
                CHECK(valueDefinitions.front().targetSelectionRange.start == expectedValue);
            }
        }
    }
}

TEST_CASE("Array assignment pattern completions support incomplete struct keys",
          "[assignment-patterns]") {
    ServerHarness server;
    auto doc = server.openFile("incomplete_array_patterns.sv", R"(
package records;
    typedef struct { int code; bit enabled; } entry_t;
    entry_t empty[1] = '{'{}};
    entry_t partial[1] = '{'{co}};
    entry_t next[1] = '{'{code: 7, }};
    entry_t existing[1] = '{'{code: 7, en: 1}};
endpackage
)");
    auto allFields = doc.after("empty[1] = '{'{").getCompletions("{");
    REQUIRE(allFields.size() == 1);
    CHECK(allFields.front().m_item.label == "'{code, enabled}");
    CHECK(allFields.front().m_item.insertText == "\n\tcode: $1,\n\tenabled: $2\n");
    CHECK(!allFields.front().m_item.additionalTextEdits);

    for (std::string closeBrace : {"", "}"}) {
        CAPTURE(closeBrace);
        auto unfinished = server.openFile("unfinished_array_pattern.sv", R"(
package records;
    typedef struct { int code; bit enabled; } entry_t;
    entry_t entries[2] = '{'{)" + closeBrace);
        auto items = unfinished.after("entries[2] = '{'{").getCompletions("{");
        REQUIRE(items.size() == 1);
        CHECK(items.front().m_item.label == "'{code, enabled}");
        if (closeBrace.empty()) {
            CHECK(items.front().m_item.insertText == "\n\tcode: $1,\n\tenabled: $2\n\\},");
        }
        else {
            CHECK(items.front().m_item.insertText == "\n\tcode: $1,\n\tenabled: $2\n");
            REQUIRE(items.front().m_item.additionalTextEdits);
            REQUIRE(items.front().m_item.additionalTextEdits->size() == 1);
            CHECK(items.front().m_item.additionalTextEdits->front().newText == ",");
        }
    }

    auto partial = doc.after("partial[1] = '{'{co").getCompletions();
    auto code = std::ranges::find(partial, "code",
                                  [](const auto& item) { return item.m_item.label; });
    REQUIRE(code != partial.end());
    REQUIRE(code->m_item.textEdit);
    CHECK(rfl::get<lsp::TextEdit>(*code->m_item.textEdit).newText == "code: $1");

    auto next = doc.after("next[1] = '{'{code: 7, ").getCompletions();
    REQUIRE(next.size() == 2);
    auto enabled = std::ranges::find(next, "enabled",
                                     [](const auto& item) { return item.m_item.label; });
    REQUIRE(enabled != next.end());
    REQUIRE(enabled->m_item.textEdit);
    CHECK(rfl::get<lsp::TextEdit>(*enabled->m_item.textEdit).newText == "enabled: $1");
    CHECK(std::ranges::none_of(next, [](const auto& item) { return item.m_item.label == "code"; }));

    auto existing = doc.after("existing[1] = '{'{code: 7, en").getCompletions();
    enabled = std::ranges::find(existing, "enabled",
                                [](const auto& item) { return item.m_item.label; });
    REQUIRE(enabled != existing.end());
    REQUIRE(enabled->m_item.textEdit);
    CHECK(rfl::get<lsp::TextEdit>(*enabled->m_item.textEdit).newText == "enabled");
}

TEST_CASE("Port assignment patterns use specialized formal types", "[scan-gaps]") {
    for (bool named : {false, true}) {
        CAPTURE(named);
        ServerHarness server;
        auto doc = server.openFile(
            "port_patterns.sv",
            std::string(R"(
module sink #(parameter type T = int)(input bit enable, input T payload); endmodule
module sample;
    typedef struct packed { int shared_field; } inner_t;
    typedef struct packed { int shared_field; inner_t nested; } packet_t;
    int shared_field;
    sink #(.T(packet_t)) receiver(
)") + (named ? ".enable(1'b1), .payload(" : "1'b1, ") +
                R"('{shared_field: shared_field, nested: '{shared_field: shared_field}})" +
                (named ? ")" : "") + R"();
endmodule
)");
        auto outer = doc.after("'{");
        auto inner = doc.after("nested: '{");
        for (auto cursor : {outer, inner}) {
            auto definitions = cursor.getDefinitions();
            REQUIRE(definitions.size() == 1);
            auto expected = cursor.m_offset == outer.m_offset
                                ? doc.after("inner_t;").before("shared_field;")
                                : doc.before("shared_field;");
            CHECK(definitions.front().targetSelectionRange.start == expected.getPosition());
            auto hover = doc.getHoverAt(cursor.m_offset);
            REQUIRE(hover);
            auto items = cursor.getCompletions();
            CHECK(std::ranges::any_of(items, [](const CompletionHandle& item) {
                return item.m_item.label == "shared_field";
            }));
        }
        auto value = doc.after("'{shared_field: ");
        auto definitions = value.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetSelectionRange.start ==
              doc.after("packet_t;").before("shared_field;").getPosition());
    }
}
