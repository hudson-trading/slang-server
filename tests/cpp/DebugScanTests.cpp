// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#ifdef SLANG_SERVER_DEBUG
#    include "debug/WorkspaceScan.h"
#    include <fstream>
#    include <sstream>
#endif

#include "slang/util/ScopeGuard.h"

using namespace server;

namespace {

class ScanHarness : public ServerHarness {
public:
    using ServerHarness::ServerHarness;
    using SlangServer::getCommandList;

    bool omitCompletions = false;

    rfl::Variant<std::vector<lsp::CompletionItem>, lsp::CompletionList, std::monostate>
    getDocCompletion(const lsp::CompletionParams& params) override {
        if (omitCompletions)
            return std::vector<lsp::CompletionItem>{};
        return SlangServer::getDocCompletion(params);
    }
};

} // namespace

TEST_CASE("Debug scan command is gated by its build option", "[debug-scan]") {
    ScanHarness server("repo1");
    auto commands = server.getCommandList();
    bool advertised = std::ranges::find(commands, "slang.debug.scanDocument") != commands.end();
#ifdef SLANG_SERVER_DEBUG
    CHECK(advertised);
#else
    CHECK_FALSE(advertised);
#endif
}

#ifdef SLANG_SERVER_DEBUG
TEST_CASE("Debug scan completes existing assignment pattern tokens", "[debug-scan]") {
    ScanHarness server;
    auto doc = server.openFile("patterns.sv", R"(
module sample;
    typedef struct packed { int first_field; int second_field; } pair_t;
    int first_value, second_value;
    pair_t named_pair = '{first_field: first_value, second_field: second_value};
    int positional_values[2] = '{first_value, second_value};
endmodule
)");
    auto result = server.debugScanDocument({doc.m_uri});
    for (const auto& diagnostic : result.diagnostics)
        INFO(rfl::json::write(diagnostic));
    CHECK(result.diagnostics.empty());
}

TEST_CASE("Debug scan checks real hovers and preserves compiler diagnostics", "[debug-scan]") {
    ScanHarness server("repo1");
    auto doc = server.openFile("scan.sv",
                               "module top; initial begin /* é */ missing(); end endmodule");
    auto original = doc.getDiagnostics();
    REQUIRE_FALSE(original.empty());

    ServerDriver::s_debugHoversEnabled = true;
    slang::ScopeGuard restore([] { ServerDriver::s_debugHoversEnabled = false; });
    REQUIRE(doc.getHoverAt(doc.before("missing").m_offset).has_value());

    auto response = server.executeCommand({
        .command = "slang.debug.scanDocument",
        .arguments =
            std::vector<lsp::LSPAny>{rfl::to_generic(lsp::TextDocumentIdentifier{doc.m_uri})},
    });
    REQUIRE(response);
    auto parsed = rfl::from_generic<DebugScanResult>(*response);
    REQUIRE(parsed);
    auto result = *parsed;
    CHECK(result.positionEncoding == "utf-16");
    REQUIRE(result.diagnostics.size() == 2);
    std::set<std::string> codes;
    for (const auto& diag : result.diagnostics) {
        codes.insert(rfl::get<std::string>(*diag.code));
        auto start = doc.before("missing").getPosition();
        auto end = doc.after("missing").getPosition();
        --start.character;
        --end.character;
        CHECK(diag.range == (lsp::Range{start, end}));
        REQUIRE(diag.data);
        CHECK(diag.source == "slang-server-debug");
    }
    CHECK(codes == std::set<std::string>{"missing-hover-goto", "missing-completion"});
    const auto& missing = result.diagnostics.front();
    CHECK(missing.message == "Missing hover and go-to-definition for 'missing'");
    CHECK(rfl::json::write(missing.data)
              .find("\"missingFeatures\":[\"hover\",\"go-to-definition\"]") != std::string::npos);
    CHECK(doc.getDiagnostics().size() == original.size() + 2);
    server.debugScanDocument({doc.m_uri});
    CHECK(doc.getDiagnostics().size() == original.size() + 2);

    lsp::RequestContext ctx("workspace/executeCommand", lsp::ID_t{1});
    ctx.cancel();
    CHECK_THROWS_AS(server.debugScanDocument({doc.m_uri}, ctx), lsp::RequestCancelled);
    CHECK(doc.getDiagnostics().size() == original.size() + 2);

    doc.replaceAll("module top; endmodule");
    doc.publishChanges();
    for (const auto& diag : doc.getDiagnostics())
        CHECK(diag.source != "slang-server-debug");
    CHECK(server.debugScanDocument({doc.m_uri}).diagnostics.empty());
}

TEST_CASE("Debug scan finds missing completions for resolved references", "[debug-scan]") {
    ScanHarness server("repo1");
    auto doc = server.openFile("scan.sv", R"(
module top;
    int value;
    initial value = value + 1;
endmodule
)");
    CHECK(server.debugScanDocument({doc.m_uri}).diagnostics.empty());
    server.omitCompletions = true;
    auto result = server.debugScanDocument({doc.m_uri});
    CHECK(result.completionChecks == 2);
    CHECK(result.references == 2);
    CHECK(result.declarations == 2);
    REQUIRE(result.diagnostics.size() == 2);
    for (const auto& diag : result.diagnostics) {
        CHECK(rfl::get<std::string>(*diag.code) == "missing-completion");
        CHECK(rfl::json::write(diag.data).find("\"resolved\":true") != std::string::npos);
        CHECK(rfl::json::write(diag.data).find("\"role\":\"reference\"") != std::string::npos);
    }
    doc.close();
    for (const auto& diag : doc.getDiagnostics())
        CHECK(diag.source != "slang-server-debug");
    CHECK_THROWS_WITH(server.debugScanDocument({doc.m_uri}),
                      "slang.debug.scanDocument requires an open document URI");
}

TEST_CASE("Debug scan excludes inactive text and sourceless system definitions", "[debug-scan]") {
    ScanHarness server("repo1");
    auto doc = server.openFile("scan.sv", R"(
`define NUMBER 1
module top;
    int value;
    // missing_comment
    initial begin : block_name
        $display("missing_string", `NUMBER, value);
    end : block_name
`ifdef UNDEFINED_SCAN_FLAG
    initial missing_disabled();
    initial $missing_disabled_system();
`ifdef NESTED_UNDEFINED_FLAG
    initial $missing_nested_system();
`else
    initial $missing_nested_else_system();
`endif
`else
    initial $display("active branch");
`endif
endmodule
)");
    auto result = server.debugScanDocument({doc.m_uri});
    for (const auto& diag : result.diagnostics)
        INFO(rfl::json::write(diag));
    CHECK(result.diagnostics.empty());
    CHECK(result.completionChecks >= 3);
}

TEST_CASE("Debug scan preserves active design instances", "[debug-scan]") {
    ScanHarness server("active_instance_hover");
    server.setBuildFile("design.f");
    auto doc = server.openFile("top.sv");
    REQUIRE(server.setActiveInstance("top.leaf16"));
    auto before = rfl::json::write(server.getActiveInstance("leaf"));
    auto notifications = server.client.m_activeInstanceChanges.size();
    server.debugScanDocument({doc.m_uri});
    CHECK(rfl::json::write(server.getActiveInstance("leaf")) == before);
    CHECK(server.client.m_activeInstanceChanges.size() == notifications);
    CHECK(server.client.m_showDocuments.empty());
}

TEST_CASE("Debug scan validates symbol and declaration names", "[debug-scan]") {
    ScanHarness server;
    auto doc = server.openFile("scan.sv", R"(
module top;
    int value, value1;
    initial value1 = 1;
endmodule
)");
    auto definition = doc.getDefinitionInfoAt(doc.before("value1;").m_offset);
    REQUIRE(definition);
    auto* symbol = const_cast<slang::ast::Symbol*>(definition->symbol());
    REQUIRE(symbol);
    // Corrupt an indexed target to exercise errors that an existence-only scan would miss.
    SECTION("A plain declaration prefix is not an enum range") {
        auto other = doc.getDefinitionInfoAt(doc.before("value,").m_offset);
        REQUIRE(other);
        symbol->setSyntax(*other->symbol()->getSyntax());
    }
    SECTION("The semantic symbol must also have the expected name") {
        symbol->name = "unrelated";
    }
    auto result = server.debugScanDocument({doc.m_uri});
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return rfl::get<std::string>(*diagnostic.code) == "definition-name-mismatch" &&
               rfl::json::write(diagnostic.data).find("\"token\":\"value1\"") != std::string::npos;
    }));
}

TEST_CASE("Debug scan accepts enum range declaration prefixes", "[debug-scan]") {
    ScanHarness server;
    auto doc = server.openFile("scan.sv", R"(
module top;
    typedef enum { ENTRY[2], ITEM[3:4] } kind_t;
    kind_t first = ENTRY0, second = ENTRY1, third = ITEM3, fourth = ITEM4;
endmodule
)");
    auto result = server.debugScanDocument({doc.m_uri});
    for (const auto& diagnostic : result.diagnostics)
        INFO(rfl::json::write(diagnostic));
    CHECK(result.diagnostics.empty());
}

TEST_CASE("Native scans share editor analysis and preserve workspace configuration",
          "[debug-scan]") {
    auto root = fs::canonical(fs::temp_directory_path()) /
                fmt::format("slang_native_scan_{}", slang::OS::getpid());
    fs::create_directories(root / ".slang");
    fs::create_directories(root / "include");
    fs::create_directories(root / "rtl/nested");
    fs::create_directories(root / "rtl/.git");
    slang::ScopeGuard cleanup([&] { fs::remove_all(root); });
    std::ofstream(root / ".slang/server.json")
        << R"({"flags":"-D SCAN_SWITCH","incdirs":["include"]})";
    std::ofstream(root / "include/decls.svh") << "`ifdef SCAN_SWITCH\nint value;\n`endif\n";
    std::ofstream(root / "rtl/top.sv", std::ios::binary)
        << "module top;\r\n`include \"decls.svh\"\r\n"
           "initial begin /* é */ value = missing; end\r\nendmodule\r\n";
    std::ofstream(root / "rtl/nested/valid.SV") << "module valid; endmodule\n";
    std::ofstream(root / "rtl/.git/ignored.sv") << "invalid ignored file\n";
    std::ofstream(root / "rtl/ignored.txt") << "invalid ignored file\n";

    auto previousDirectory = fs::current_path();
    std::ostringstream transport;
    auto* previousOutput = std::cout.rdbuf(transport.rdbuf());
    slang::ScopeGuard restoreOutput([&] { std::cout.rdbuf(previousOutput); });
    auto report = debugScanWorkspace(root / "rtl", root);
    CHECK(transport.str().empty());
    CHECK(fs::current_path() == previousDirectory);
    REQUIRE(report.summary.complete);
    CHECK(report.errors.empty());
    CHECK(report.messages.empty());
    CHECK(report.summary.filesSelected == 2);
    REQUIRE(report.documents.size() == 2);
    CHECK(report.documents[0].path == "rtl/nested/valid.SV");
    CHECK(report.documents[0].result.value().diagnostics.empty());
    auto& document = report.documents[1];
    CHECK(document.path == "rtl/top.sv");
    CHECK_FALSE(document.compilerDiagnostics.empty());
    CHECK(document.result.value().positionEncoding == "utf-8");
    CHECK(document.result.value().references == 2);
    REQUIRE(document.result.value().diagnostics.size() == 2);
    for (const auto& diagnostic : document.result.value().diagnostics) {
        CHECK(diagnostic.range.start == (lsp::Position{2, 31}));
        CHECK(rfl::json::write(diagnostic.data).find("\"token\":\"missing\"") != std::string::npos);
    }
    CHECK(report.summary.issues == 2);
    REQUIRE(report.groups.size() == 2);
    for (const auto& group : report.groups) {
        CHECK(group.role == "reference");
        REQUIRE(group.occurrences.size() == 1);
        CHECK(group.occurrences[0].path == "rtl/top.sv");
        CHECK(group.occurrences[0].line == 3);
        CHECK(group.occurrences[0].byteColumn == 32);
    }

    auto single = debugScanWorkspace(root / "rtl/nested/valid.SV");
    CHECK(single.summary.complete);
    CHECK(single.summary.filesSelected == 1);
    CHECK(single.summary.issues == 0);
    auto empty = debugScanWorkspace(root / "rtl/ignored.txt", root);
    CHECK_FALSE(empty.summary.complete);
    REQUIRE(empty.errors.size() == 1);
    CHECK(empty.errors[0].message == "No HDL files selected");
    auto outside = debugScanWorkspace(root / "rtl", root / "include");
    CHECK_FALSE(outside.summary.complete);
    REQUIRE(outside.errors.size() == 1);
    CHECK(outside.errors[0].message == "Target must be inside the workspace");
}
#endif
