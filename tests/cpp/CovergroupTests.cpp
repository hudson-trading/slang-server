// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Covergroup options and methods use the body scope", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("coverage.sv", R"(
module top;
    covergroup samples with function sample(int value);
        option.per_instance = 1;
        type_option.merge_instances = 1;
        point_a: coverpoint value;
    endgroup
    samples group_h = new;
    initial begin
        group_h.set_inst_name("group_h");
        group_h.sample(1);
    end
endmodule
)");
    CHECK(doc.getDiagnostics().empty());
    for (auto [prefix, name] :
         {std::pair{"        ", "option"}, std::pair{"option.", "per_instance"},
          std::pair{"        ", "type_option"}, std::pair{"type_option.", "merge_instances"},
          std::pair{"group_h.", "set_inst_name"}, std::pair{"group_h.", "sample"}}) {
        CAPTURE(name);
        auto cursor = doc.before(std::string(prefix) + name);
        cursor.m_offset += std::string_view(prefix).size();
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find(name) != std::string::npos);
        CHECK_NOTHROW(server.getDocReferences(lsp::ReferenceParams{
            .context = {.includeDeclaration = true},
            .textDocument = {.uri = doc.m_uri},
            .position = cursor.getPosition(),
        }));
        auto items = std::string_view(prefix).ends_with('.') ? cursor.getCompletions(".")
                                                             : cursor.getCompletions();
        auto item = std::ranges::find(items, name,
                                      [](const auto& item) { return item.m_item.label; });
        REQUIRE(item != items.end());
    }
    CHECK(doc.getDiagnostics().empty());
}
