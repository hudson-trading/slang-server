// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Function result members retain call argument lookup", "[scan-gaps]") {
    ServerHarness server;
    auto doc = server.openFile("members.sv", R"(
class Endpoint;
    function void write(int value); endfunction
    function int read(int mode = 0); return mode; endfunction
endclass
class Storage;
    Endpoint data;
endclass
class Caller;
    Storage state;
    function Storage get_state(); return state; endfunction
    function void run();
        int value;
        get_state().data.write(.value(1));
        value = get_state().data.read(.mode(1));
    endfunction
endclass
)");
    CHECK(doc.getDiagnostics().empty());
    for (auto [prefix, name, declaration] :
         {std::tuple{"get_state().", "data", "data;"},
          std::tuple{"get_state().data.", "write", "write(int"},
          std::tuple{"get_state().data.write(.", "value", "value);"},
          std::tuple{"value = get_state().data.", "read", "read(int"},
          std::tuple{"get_state().data.read(.", "mode", "mode ="}}) {
        CAPTURE(prefix);
        auto cursor = doc.after(prefix);
        auto definitions = cursor.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetSelectionRange.start ==
              doc.before(declaration).getPosition());
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find(name) != std::string::npos);
        auto items = cursor.getCompletions(".");
        CHECK(std::ranges::any_of(items,
                                  [&](const auto& item) { return item.m_item.label == name; }));
    }
    CHECK(doc.getDiagnostics().empty());
}
