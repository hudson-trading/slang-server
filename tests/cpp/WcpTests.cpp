// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

TEST_CASE("WCP Tests") {
    ServerHarness server("");

    server.loadConfig(Config{.build = "test3.f"});

    // This will actually load the compilation
    server.onInitialized(lsp::InitializedParams{});

    const std::string file("test3.sv");
    auto uri = URI::fromFile(fs::absolute(file));
    auto doc = server.openFile(file);


    SECTION("Instances No Results") {
        server.checkGetInstances(doc.before("module test"), {});
    }

    SECTION("Instances Declaration") {
        server.checkGetInstances(doc.before("foo, bar;"), {"test.foo"});
    }

    SECTION("Instances Reference") {
        server.checkGetInstances(doc.before("foo = bar;"), {"test.foo"});
    }

    SECTION("Instances Multiple") {
        server.checkGetInstances(--doc.after("always_comb baz"),
                                 {{"test.the_sub_1.baz"}, {"test.the_sub_2.baz"}});
    }

    SECTION("Instances Interface Instance") {
        server.checkGetInstances(--doc.after("intf the_intf_1"),
                                 {{"test.the_sub_1.the_intf_1"}, {"test.the_sub_2.the_intf_1"}});
    }

    SECTION("Instances Interface Reference") {
        server.checkGetInstances(--doc.after("intf2 intf_port"),
                                 {"test.the_other_sub.the_sub_w_intf.intf_port"});
    }

    // TODO -- this works differently than member selects below, which way should this work?
    SECTION("Instances Interface Modport Reference Signal") {
        server.checkGetInstances(doc.before("all_in_port.def);"),
                                 {"test.the_other_sub.the_sub_w_intf.all_in_port.def"});
    }

    SECTION("Instances Interface Reference Signal") {
        server.checkGetInstances(doc.before("intf_port.abc);"),
                                 {"test.the_other_sub.the_sub_w_intf.intf_port.abc"});
    }

    SECTION("Instances Interface Modport Reference Signal Genscope") {
        server.checkGetInstances(doc.before("all_in_port.abc);"),
                                 {"test.the_other_sub.the_sub_w_intf.all_in_port.abc"});
    }

    SECTION("Instances Interface Signal") {
        server.checkGetInstances(doc.before("sig1;"), {
                                                          {"test.the_sub_1.the_intf_1.sig1"},
                                                          {"test.the_sub_1.the_intf_2.sig1"},
                                                          {"test.the_sub_2.the_intf_1.sig1"},
                                                          {"test.the_sub_2.the_intf_2.sig1"},
                                                      });
    }

    SECTION("Instances Fields") {
        server.checkGetInstances(doc.before(".abc);"), {{"test.the_other_sub.t1.t2.abc"}});
    }

    SECTION("Instances Aggregate Field") {
        server.checkGetInstances(doc.before("t2.abc);"), {{"test.the_other_sub.t1.t2"}});
    }

    SECTION("Instances Aggregate Var") {
        server.checkGetInstances(doc.before("t1.t2.abc);"), {{"test.the_other_sub.t1"}});
    }

    // TODO -- slice (elements and ranges) vs whole array
    SECTION("Instances Whole Array") {
        server.checkGetInstances(doc.before("the_array[4]);"), {{"test.the_other_sub.the_array"}});
    }

    SECTION("Instances Enum Var") {
        server.checkGetInstances(doc.before("the_enum = BAR;"), {{"test.the_other_sub.the_enum"}});
    }


    SECTION("Goto Hit") {
        auto cursor = doc.before("baz;");
        server.checkGotoDeclaration("test.the_sub_2.baz", &cursor);
    }

    SECTION("Goto Miss") {
        server.checkGotoDeclaration("blargh.ack");
    }

    SECTION("Goto Interface Signal") {
        auto cursor = doc.before("sig1;");
        server.checkGotoDeclaration("test.the_sub_2.the_intf_1.sig1", &cursor);
    }

    // TODO -- goto declaration vs definition
    SECTION("Goto Field") {
        auto cursor = doc.before("t1;");
        server.checkGotoDeclaration("test.the_other_sub.t1.t2.def", &cursor);
    }

    SECTION("Goto Enum Var") {
        auto cursor = doc.before("the_enum;");
        server.checkGotoDeclaration("test.the_other_sub.the_enum", &cursor);
    }

    SECTION("Goto Array Slice") {
        auto cursor = doc.before("the_array");
        server.checkGotoDeclaration("test.the_other_sub.the_array[4]", &cursor);
    }

    SECTION("Is Var Var") {
        CHECK(server.m_driver->comp->isWcpVariable("test.foo"));
    }

    SECTION("Is Var Mod") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_sub_1"));
    }

    SECTION("Is Var Whole Struct") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_other_sub.t1"));
    }

    SECTION("Is Var Sub Struct") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_other_sub.t1.t2"));
    }

    SECTION("Is Var Struct Field") {
        CHECK(server.m_driver->comp->isWcpVariable("test.the_other_sub.t1.t2.abc"));
    }

    SECTION("Is Var Array") {
        CHECK(!server.m_driver->comp->isWcpVariable("test.the_other_sub.the_array"));
    }

    SECTION("Is Var Slice") {
        CHECK(server.m_driver->comp->isWcpVariable("test.the_other_sub.the_array[4]"));
    }
}
