// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "BuiltinMethodDocs.h"
#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <map>
#include <set>

#include "slang/ast/symbols/ClassSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/types/AllTypes.h"

using namespace server;
using namespace slang;

TEST_CASE("Randomization controls use property and constraint receiver kinds",
          "[builtin-methods]") {
    ServerHarness server;
    auto doc = server.openFile("randomization_controls.sv", R"(
class Sample;
    rand int value;
    constraint positive { value > 0; }
    function void run();
        value.rand_mode(0);
        this.value.rand_mode(1);
        positive.constraint_mode(0);
        this.positive.constraint_mode(1);
    endfunction
endclass
)");
    CHECK(doc.getDiagnostics().empty());
    for (auto [prefix, method] :
         {std::pair{"        value.", "rand_mode"}, std::pair{"this.value.", "rand_mode"},
          std::pair{"        positive.", "constraint_mode"},
          std::pair{"this.positive.", "constraint_mode"}}) {
        CAPTURE(prefix);
        auto cursor = doc.after(prefix);
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find(method) !=
              std::string::npos);
        CHECK(cursor.getDefinitions().empty());
        auto items = cursor.getCompletions(".");
        CHECK(std::ranges::any_of(items,
                                  [&](const auto& item) { return item.m_item.label == method; }));
    }
    CHECK(doc.getDiagnostics().empty());

    auto plain = server.openFile("plain.sv", R"(
class Plain;
    int value;
    function void run(); value.; endfunction
endclass
)");
    auto items = plain.after("value.").getCompletions(".");
    CHECK(std::ranges::none_of(items,
                               [](const auto& item) { return item.m_item.label == "rand_mode"; }));
}

TEST_CASE("Built-in methods use expression result types", "[builtin-methods]") {
    ServerHarness server;
    auto doc = server.openFile("receiver_types.sv", R"(
module top;
    typedef enum { Idle, Busy } state_t;
    state_t state;
    string names[2];
    function automatic string name(); return "sample"; endfunction
    initial begin
        $display(state.name().len());
        $display(names[0].len());
        $display(name().len());
    end
endmodule
)");
    auto diagnostics = doc.getDiagnostics();
    for (auto [prefix, method] : {std::pair{"state.name().", "len"}, std::pair{"names[0].", "len"},
                                  std::pair{"$display(name().", "len"}}) {
        CAPTURE(prefix);
        auto cursor = doc.after(prefix);
        auto definition = doc.getDefinitionInfoAt(cursor.m_offset);
        REQUIRE(definition);
        REQUIRE(definition->systemSubroutine());
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find(method) !=
              std::string::npos);
        CHECK(cursor.getDefinitions().empty());
        auto items = cursor.getCompletions(".");
        auto item = std::ranges::find(items, method,
                                      [](const auto& item) { return item.m_item.label; });
        REQUIRE(item != items.end());
        REQUIRE(item->m_item.documentation);
    }
    CHECK(doc.getDiagnostics().size() == diagnostics.size());
}

TEST_CASE("Built-in methods retain unpacked arrays with unknown bounds", "[builtin-methods]") {
    ServerHarness server;
    auto doc = server.openFile("array_methods.sv", R"(
package sample_pkg;
    class Worker #(int COUNT);
        byte values[COUNT];
        bit [COUNT-1:0] packed_value;
        function void run();
            values.shuffle();
            packed_value.nonexistent();
        endfunction
    endclass
endpackage
)");
    auto diagnostics = doc.getDiagnostics();
    auto cursor = doc.after("values.");
    auto definition = doc.getDefinitionInfoAt(cursor.m_offset);
    REQUIRE(definition);
    REQUIRE(definition->systemSubroutine());
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    CHECK(rfl::get<lsp::MarkupContent>(hover->contents).value.find("shuffle") != std::string::npos);
    CHECK(cursor.getDefinitions().empty());
    auto items = cursor.getCompletions(".");
    auto item = std::ranges::find(items, "shuffle",
                                  [](const auto& item) { return item.m_item.label; });
    REQUIRE(item != items.end());
    REQUIRE(item->m_item.documentation);
    auto packed = doc.after("packed_value.").getCompletions(".");
    CHECK(std::ranges::none_of(packed,
                               [](const auto& item) { return item.m_item.label == "shuffle"; }));
    CHECK(doc.getDiagnostics().size() == diagnostics.size());
}

TEST_CASE("BuiltinClassMethods", "[builtin-methods]") {
    const std::map<std::string, std::map<std::string, std::string>> calls{
        {"process",
         {{"self", "process::self()"},
          {"status", "proc.status()"},
          {"kill", "proc.kill()"},
          {"await", "proc.await()"},
          {"suspend", "proc.suspend()"},
          {"resume", "proc.resume()"},
          {"srandom", "proc.srandom(1)"},
          {"get_randstate", "proc.get_randstate()"},
          {"set_randstate", "proc.set_randstate(state)"}}},
        {"semaphore",
         {{"new", "sem = semaphore::new()"},
          {"put", "sem.put()"},
          {"get", "sem.get()"},
          {"try_get", "sem.try_get()"}}},
        {"mailbox",
         {{"new", "messages = IntMailbox::new()"},
          {"num", "messages.num()"},
          {"put", "messages.put(value)"},
          {"try_put", "messages.try_put(value)"},
          {"get", "messages.get(value)"},
          {"try_get", "messages.try_get(value)"},
          {"peek", "messages.peek(value)"},
          {"try_peek", "messages.try_peek(value)"}}},
        {"weak_reference",
         {{"new", "weak_handle = SampleRef::new(object)"},
          {"get", "weak_handle.get()"},
          {"clear", "weak_handle.clear()"},
          {"get_id", "SampleRef::get_id(object)"}}},
        {"Sample",
         {{"randomize", "object.randomize()"},
          {"pre_randomize", "object.pre_randomize()"},
          {"post_randomize", "object.post_randomize()"},
          {"srandom", "object.srandom(1)"},
          {"get_randstate", "object.get_randstate()"},
          {"set_randstate", "object.set_randstate(state)"},
          {"rand_mode", "object.rand_mode(1)"},
          {"constraint_mode", "object.constraint_mode(1)"}}},
        {"std", {{"randomize", "std::randomize(value)"}}},
    };
    std::string source = R"(
class Sample;
    rand int value;
endclass
typedef mailbox #(int) IntMailbox;
typedef weak_reference #(Sample) SampleRef;
module top;
    process proc;
    semaphore sem;
    IntMailbox messages;
    SampleRef weak_handle;
    Sample object;
    int value;
    string state;
    initial begin
)";
    for (auto& [owner, methods] : calls) {
        for (auto& [name, call] : methods)
            source += "        " + call + ";\n";
    }
    source += "    end\nendmodule\n";
    ServerHarness server;
    Config config;
    config.flagsByFile.value().push_back({"test", "--std=1800-2023"});
    server.loadConfig(config);
    auto doc = server.openFile("builtin_methods.sv", source);
    for (auto& diagnostic : doc.getDiagnostics()) {
        INFO(diagnostic.message);
        CHECK(diagnostic.severity != lsp::DiagnosticSeverity::Error);
    }
    auto analysis = doc.doc->getAnalysis();
    auto& compilation = *analysis->getCompilation();
    auto& stdPackage = compilation.getStdPackage();
    std::set<std::string> standardClasses;
    std::set<std::string> freeFunctions;
    for (auto& member : stdPackage.members()) {
        if (member.kind == ast::SymbolKind::ClassType ||
            member.kind == ast::SymbolKind::GenericClassDef)
            standardClasses.emplace(member.name);
        else if (member.kind == ast::SymbolKind::Subroutine)
            freeFunctions.emplace(member.name);
    }
    CHECK(standardClasses ==
          std::set<std::string>{"process", "semaphore", "mailbox", "weak_reference"});
    CHECK(freeFunctions == std::set<std::string>{"randomize"});

    const std::map<std::string, std::string> variables{{"process", "proc"},
                                                       {"semaphore", "sem"},
                                                       {"mailbox", "messages"},
                                                       {"weak_reference", "weak_handle"},
                                                       {"Sample", "object"}};
    JsonGoldenTest golden;
    for (auto& [owner, methods] : calls) {
        CAPTURE(owner);
        const ast::Scope* scope = &stdPackage;
        if (owner != "std") {
            auto definition = doc.getDefinitionInfoAt(
                doc.before(variables.at(owner) + ";").m_offset);
            REQUIRE(definition);
            REQUIRE(definition->symbol());
            auto* declaredType = definition->symbol()->getDeclaredType();
            REQUIRE(declaredType);
            scope = &declaredType->getType().getCanonicalType().as<ast::ClassType>();
        }
        std::set<std::string> actualMethods;
        for (auto& member : scope->members()) {
            if (member.kind == ast::SymbolKind::Subroutine)
                actualMethods.emplace(member.name);
        }
        std::set<std::string> coveredMethods;
        for (auto& [name, call] : methods)
            coveredMethods.insert(name);
        REQUIRE(actualMethods == coveredMethods);

        for (auto& [name, call] : methods) {
            CAPTURE(name, call);
            auto* method = scope->find(name)->as_if<ast::SubroutineSymbol>();
            REQUIRE(method);
            auto* methodDoc = getBuiltinMethodDoc(*method);
            REQUIRE(methodDoc);
            CHECK_FALSE(methodDoc->description.empty());
            CHECK_FALSE(methodDoc->ieeeSection.empty());

            auto start = doc.before(call).m_offset + call.find(name + "(");
            auto hover = doc.getHoverAt(start);
            REQUIRE(hover);
            auto text = rfl::get<lsp::MarkupContent>(hover->contents).value;
            CHECK(text.find(formatBuiltinMethodSignature(*method)) != std::string::npos);
            CHECK(text.find(methodDoc->description) != std::string::npos);
            CHECK(Cursor(doc, start).getDefinitions().empty());
            golden.record(owner + "::" + name, *hover);

            auto items = Cursor(doc, start)
                             .getCompletions(
                                 call[start - doc.before(call).m_offset - 1] == ':' ? ":" : ".");
            auto item = std::ranges::find_if(items,
                                             [&](auto& item) { return item.m_item.label == name; });
            REQUIRE(item != items.end());
            REQUIRE(item->m_item.documentation);
            auto completionDoc = rfl::get<lsp::MarkupContent>(*item->m_item.documentation).value;
            CHECK(completionDoc.find(formatBuiltinMethodSignature(*method)) != std::string::npos);
            CHECK(completionDoc.find(methodDoc->description) != std::string::npos);
        }
    }
}

TEST_CASE("Builtin mailbox signatures follow every specialization", "[builtin-methods]") {
    for (auto* type : {"", "int", "string", "Sample"}) {
        CAPTURE(type);
        std::string mailboxType = *type ? fmt::format("mailbox #({})", type) : "mailbox";
        auto valueType = *type ? type : "int";
        auto source = fmt::format(R"(
class Sample; endclass
module top;
    {} messages;
    {} value;
    initial begin
        messages = new();
        messages.num();
        messages.put(value);
        messages.try_put(value);
        messages.get(value);
        messages.try_get(value);
        messages.peek(value);
        messages.try_peek(value);
    end
endmodule
)",
                                  mailboxType, valueType);
        ServerHarness server;
        auto doc = server.openFile("mailbox_types.sv", source);
        for (auto* name : {"new", "num", "put", "try_put", "get", "try_get", "peek", "try_peek"}) {
            CAPTURE(name);
            auto cursor = doc.before(std::string(name) + "(");
            auto definition = doc.getDefinitionInfoAt(cursor.m_offset);
            REQUIRE(definition);
            auto* method = definition->symbol()->as_if<ast::SubroutineSymbol>();
            REQUIRE(method);
            CHECK(method->name == name);
            auto hover = doc.getHoverAt(cursor.m_offset);
            REQUIRE(hover);
            CHECK(rfl::get<lsp::MarkupContent>(hover->contents)
                      .value.find(formatBuiltinMethodSignature(*method)) != std::string::npos);
            if (std::string_view(name) != "new" && std::string_view(name) != "num") {
                REQUIRE(method->getArguments().size() == 1);
                CHECK(method->getArguments().front()->getType().toString() ==
                      (*type ? type : "untyped"));
            }
        }
    }
}

TEST_CASE("Builtin constructor hover uses the declared or assigned class", "[builtin-methods]") {
    ServerHarness server;
    Config config;
    config.flagsByFile.value().push_back({"test", "--std=1800-2023"});
    server.loadConfig(config);
    auto doc = server.openFile("constructors.sv", R"(
class Sample; endclass
module top;
    semaphore sem = new(2);
    mailbox #(int) messages = new(3);
    weak_reference #(Sample) weak_handle = new(null);
    initial begin
        sem = new(4);
        messages = new(5);
        weak_handle = new(null);
    end
endmodule
)");
    for (auto* call : {"new(2)", "new(3)", "new(4)", "new(5)", "new(null)"}) {
        CAPTURE(call);
        auto cursor = doc.before(call);
        auto hover = doc.getHoverAt(cursor.m_offset);
        REQUIRE(hover);
        auto text = rfl::get<lsp::MarkupContent>(hover->contents).value;
        CHECK(text.find("function new(") != std::string::npos);
        CHECK(text.find("function void new(") == std::string::npos);
        CHECK(cursor.getDefinitions().empty());
    }
}

TEST_CASE("User methods do not receive built-in class documentation", "[builtin-methods]") {
    ServerHarness server;
    auto doc = server.openFile("user_methods.sv", R"(
package user_pkg;
    class mailbox;
        function int try_get(); return 7; endfunction
    endclass
endpackage
module top;
    user_pkg::mailbox messages;
    initial messages.try_get();
endmodule
)");
    auto cursor = doc.after("messages.");
    auto definition = doc.getDefinitionInfoAt(cursor.m_offset);
    REQUIRE(definition);
    auto* method = definition->symbol()->as_if<ast::SubroutineSymbol>();
    REQUIRE(method);
    CHECK_FALSE(getBuiltinMethodDoc(*method));
    auto hover = doc.getHoverAt(cursor.m_offset);
    REQUIRE(hover);
    auto text = rfl::get<lsp::MarkupContent>(hover->contents).value;
    CHECK(text.find("return 7") != std::string::npos);
    CHECK(text.find("IEEE 1800-2023") == std::string::npos);
    REQUIRE(cursor.getDefinitions().size() == 1);
}
