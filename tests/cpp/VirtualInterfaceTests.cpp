// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

#include "slang/ast/symbols/InstanceSymbols.h"

TEST_CASE("Interface arrays retain member features with unknown bounds", "[interface-array]") {
    for (auto count : {"parameter int COUNT", "parameter int COUNT = 2"}) {
        CAPTURE(count);
        ServerHarness server;
        auto doc = server.openFile("interface_arrays.sv", std::string(R"(
typedef struct packed { bit enabled; int tag; } payload_t;
interface channel_if #(parameter type T = logic);
    T data;
    bit valid;
    bit hidden;
    modport sink(input data, valid);
endinterface
module sample #(
)") + count + R"(
)(channel_if.sink requests[COUNT], channel_if.sink matrix[COUNT][2]);
    $static_assert(type(requests[0].T) == type(payload_t));
    $static_assert(type(matrix[0][1].T) == type(payload_t));
    channel_if #(payload_t) local_channels[COUNT]();
    channel_if #(payload_t) local_matrix[2][COUNT]();
    localparam int i = 0;
        wire flag = requests[i].data.enabled;
        wire flag2 = matrix[i][1].data.enabled;
        assign local_channels[i].valid = requests[i].valid;
        assign local_channels[i].data = requests[i].data;
        wire flag3 = local_channels[i].data.enabled;
        wire flag4 = local_matrix[1][i].data.enabled;
        wire bad1 = local_channels[i][i].valid;
        wire bad2 = local_channels[i:i].valid;
        wire bad3 = local_matrix[0].valid;
        wire bad4 = local_matrix[3][i].valid;
endmodule
)");
        for (auto prefix : {"requests[i].data.", "matrix[i][1].data.", "local_channels[i].data.",
                            "local_matrix[1][i].data."}) {
            CAPTURE(prefix);
            auto cursor = doc.after(prefix);
            auto definitions = cursor.getDefinitions();
            REQUIRE(definitions.size() == 1);
            CHECK(definitions.front().targetSelectionRange.start ==
                  doc.before("enabled;").getPosition());
            auto hover = doc.getHoverAt(cursor.m_offset);
            REQUIRE(hover);
            CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find("enabled") !=
                  std::string::npos);
            auto items = cursor.getCompletions(".");
            CHECK(std::ranges::any_of(items, [](const CompletionHandle& item) {
                return item.m_item.label == "enabled";
            }));
        }
        auto local = doc.after("assign ");
        auto definitions = local.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetSelectionRange.start ==
              doc.before("local_channels[COUNT]").getPosition());
        REQUIRE(doc.getHoverAt(local.m_offset));
        auto member = doc.after("assign local_channels[i].");
        auto memberDefinitions = member.getDefinitions();
        REQUIRE(memberDefinitions.size() == 1);
        CHECK(memberDefinitions.front().targetSelectionRange.start ==
              doc.before("valid;").getPosition());
        REQUIRE(doc.getHoverAt(member.m_offset));
        auto items = doc.after("requests[i].").getCompletions(".");
        CHECK(std::ranges::none_of(items, [](const CompletionHandle& item) {
            return item.m_item.label == "hidden";
        }));
        for (auto prefix : {"local_channels[i][i].", "local_channels[i:i].", "local_matrix[0].",
                            "local_matrix[3][i]."}) {
            CAPTURE(prefix);
            auto invalid = doc.after(prefix);
            CHECK(invalid.getDefinitions().empty());
            auto invalidItems = invalid.getCompletions(".");
            CHECK(std::ranges::none_of(invalidItems, [](const CompletionHandle& item) {
                return item.m_item.label == "valid";
            }));
        }
    }
}

TEST_CASE("Virtual interface type arguments navigate to the interface", "[virtual-interface]") {
    ServerHarness server("virtual_interface");
    auto doc = server.openFile("src/client.svh", R"(
class base #(type BIND_T);
    BIND_T link;
endclass
class client extends base #(.BIND_T(virtual interface bus_if #(.INITIAL(7)).monitor));
    function int read(); return link.value; endfunction
endclass
)");
    const auto& compilation = doc.doc->getCompilation();
    REQUIRE(compilation->tryGetDefinition("bus_if", compilation->getRoot()).definition);
    auto iface = server.openFile("bus_if.sv");
    for (auto* name : {"virtual", "interface bus_if", "bus_if #"}) {
        CAPTURE(name);
        auto cursor = doc.before(name);
        auto definitions = cursor.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetUri == iface.m_uri);
        CHECK(definitions.front().targetSelectionRange.start ==
              iface.before("bus_if").getPosition());
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto text = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CHECK(text.find("bus_if") != std::string::npos);
        CHECK(text.find("class base") == std::string::npos);
    }
    for (auto* name : {"INITIAL", "monitor"}) {
        CAPTURE(name);
        auto definitions = doc.before(name).getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetUri == iface.m_uri);
        CHECK(definitions.front().targetSelectionRange.start == iface.before(name).getPosition());
    }
    auto definitions = doc.after("link.").getDefinitions();
    REQUIRE(definitions.size() == 2);
    for (auto* target : {"value =", "value);"}) {
        CHECK(std::ranges::any_of(definitions, [&](const auto& definition) {
            return definition.targetUri == iface.m_uri &&
                   definition.targetSelectionRange.start == iface.before(target).getPosition();
        }));
    }
    auto items = doc.after("link.").getCompletions(".");
    CHECK(
        std::ranges::any_of(items, [](const auto& item) { return item.m_item.label == "value"; }));
    CHECK(std::ranges::none_of(items,
                               [](const auto& item) { return item.m_item.label == "INITIAL"; }));
}

TEST_CASE("Virtual interface members preserve specialization through aliases and base classes",
          "[virtual-interface]") {
    ServerHarness server("virtual_interface");
    auto doc = server.openFile("src/client.svh", R"(
typedef virtual bus_if #(7) handle_t;
class base #(type BIND_T);
    BIND_T link;
endclass
class client extends base #(.BIND_T(handle_t));
    function int read(); return link.INITIAL + link.value; endfunction
endclass
)");
    auto hover = doc.getHoverAt(doc.after("link.").m_offset);
    REQUIRE(hover);
    auto text = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(text.find("7") != std::string::npos);
    auto items = doc.after("link.").getCompletions(".");
    for (auto* name : {"INITIAL", "value", "monitor"}) {
        CAPTURE(name);
        auto item = std::ranges::find_if(items, [&](const auto& item) {
            return item.m_item.label == name;
        });
        REQUIRE(item != items.end());
        item->resolve();
        REQUIRE(item->m_item.documentation);
        CHECK(rfl::get<lsp::MarkupContent>(*item->m_item.documentation).value.find(name) !=
              std::string::npos);
    }
    auto definitions = doc.after("+ link.").getDefinitions();
    REQUIRE(definitions.size() == 1);
    CHECK(definitions.front().targetUri == URI::fromFile(fs::current_path() / "bus_if.sv"));
}

TEST_CASE("Virtual interface members remain available with interface-port diagnostics",
          "[virtual-interface]") {
    ServerHarness server("virtual_interface");
    auto iface = server.openFile("bus_if.sv", R"(
interface wire_if;
    int value;
endinterface
interface bus_if(wire_if bus);
    int marker;
    function void send(); endfunction
endinterface
)");
    auto doc = server.openFile("src/client.svh", R"(
class base #(type BIND_T);
    BIND_T link;
endclass
class client extends base #(.BIND_T(virtual bus_if));
    function void write(); link.marker = 1; link.send(); endfunction
endclass
)");
    auto diagnostics = doc.getDiagnostics();
    CHECK(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("interface with ports that reference other interfaces") !=
               std::string::npos;
    }));
    for (auto* name : {"marker", "send"}) {
        CAPTURE(name);
        auto cursor = doc.before(name);
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find(name) != std::string::npos);
        auto definitions = cursor.getDefinitions();
        REQUIRE(definitions.size() == 1);
        CHECK(definitions.front().targetUri == iface.m_uri);
        CHECK(definitions.front().targetSelectionRange.start == iface.before(name).getPosition());
        auto items = cursor.getCompletions(".");
        auto item = std::ranges::find_if(items, [&](const auto& item) {
            return item.m_item.label == name;
        });
        REQUIRE(item != items.end());
        item->resolve();
        REQUIRE(item->m_item.documentation);
        CHECK(rfl::get<lsp::MarkupContent>(*item->m_item.documentation).value.find(name) !=
              std::string::npos);
        REQUIRE(item->m_item.textEdit);
        CHECK(rfl::get<lsp::TextEdit>(*item->m_item.textEdit).newText == name);
    }
}

TEST_CASE("Unresolved virtual interface types do not resolve to an enclosing class",
          "[virtual-interface]") {
    ServerHarness server;
    auto doc = server.openFile("unknown_virtual.sv", R"(
class base #(type BIND_T); endclass
class client extends base #(.BIND_T(virtual missing_if)); endclass
)");
    for (auto* token : {"virtual", "missing_if"}) {
        CAPTURE(token);
        auto cursor = doc.before(token);
        CHECK(cursor.getDefinitions().empty());
        CHECK_FALSE(doc.getHoverAt(cursor.m_offset));
    }
}
