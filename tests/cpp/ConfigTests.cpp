// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <fstream>

#include "slang/util/OS.h"

namespace {

struct ConfigWorkspace {
    inline static unsigned nextId = 0;
    fs::path root = fs::weakly_canonical(fs::temp_directory_path()) /
                    fmt::format("slang_config_{}_{}", slang::OS::getpid(), nextId++);

    ConfigWorkspace() { fs::create_directories(root); }
    ~ConfigWorkspace() { fs::remove_all(root); }

    void write(const fs::path& path, std::string_view text) {
        fs::create_directories((root / path).parent_path());
        std::ofstream(root / path) << text;
    }

    ServerHarness server() const {
        return ServerHarness(lsp::InitializeParams{
            .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "config"}}}});
    }
};

} // namespace

TEST_CASE("Config incdirs append across workspace user and local files") {
    ConfigWorkspace workspace;
    workspace.write("workspace.json", R"({"incdirs":["shared"], "flags":"-DSHARED"})");
    workspace.write("user.json", R"({"incdirs":["personal"], "flags":"-DUSER"})");
    workspace.write("local.json", R"({"incdirs":["local"], "flags":"-DLOCAL"})");
    auto result = Config::fromFiles((workspace.root / "workspace.json").string(),
                                    (workspace.root / "user.json").string(),
                                    (workspace.root / "local.json").string());
    REQUIRE(result);
    const auto& config = *result;
    CHECK(config.incdirs.value() == std::vector<std::string>{"shared", "personal", "local"});
    CHECK(config.flags.value() == "-DSHARED -DLOCAL");
}

TEST_CASE("Config incdirs resolve against the workspace and follow flag directories") {
    ConfigWorkspace workspace;
    workspace.write("headers with spaces/nested.svh", "typedef int value_t;\n");
    workspace.write("headers with spaces/entry.svh", "`include \"nested.svh\"\n");
    workspace.write("headers with spaces/choice.svh", "typedef int wrong_t;\n");
    workspace.write("configured/choice.svh", "typedef int chosen_t;\n");
    rfl::Generic::Object config;
    config["incdirs"] = rfl::to_generic(std::vector<std::string>{"headers with spaces"});
    config["flags"] = "-I " + (workspace.root / "configured").generic_string();
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));
    auto server = workspace.server();
    auto doc = server.openFile("source.sv", "`include \"entry.svh\"\n"
                                            "`include \"choice.svh\"\n"
                                            "module source(output value_t a, output chosen_t b);\n"
                                            "assign a = '0; assign b = '0; endmodule\n");
    INFO(rfl::json::write(doc.getDiagnostics()));
    CHECK(doc.getDiagnostics().empty());
    auto tree = doc.doc->getSyntaxTree();
    REQUIRE(tree->getIncludeDirectives().size() == 3);
    CHECK(server.sourceManager().getFullPath(tree->getIncludeDirectives().back().buffer.id) ==
          workspace.root / "configured/choice.svh");
}

TEST_CASE("Shallow include fallback chooses the nearest header separately for each source") {
    ConfigWorkspace workspace;
    workspace.write("first/inc/types.svh", "typedef int first_t;\n");
    workspace.write("second/inc/types.svh", "typedef int second_t;\n");
    workspace.write("first/src/top.sv",
                    "`define HEADER \"types.svh\"\n`include `HEADER\n"
                    "module first(output first_t v); assign v = 0; endmodule\n");
    workspace.write("second/src/top.sv",
                    "`include \"types.svh\"\n"
                    "module second(output second_t v); assign v = 0; endmodule\n");
    auto server = workspace.server();
    auto header = server.openFile("first/inc/types.svh");
    CHECK(
        server.sourceManager().getFullPath(header.doc->getSyntaxTree()->getSourceBufferIds()[0]) ==
        workspace.root / "first/src/top.sv");
    auto first = server.openFile("first/src/top.sv");
    auto second = server.openFile("second/src/top.sv");
    CHECK(first.getDiagnostics().empty());
    CHECK(second.getDiagnostics().empty());
    auto firstIncludes = first.doc->getSyntaxTree()->getIncludeDirectives();
    auto secondIncludes = second.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(firstIncludes.size() == 1);
    REQUIRE(secondIncludes.size() == 1);
    REQUIRE(firstIncludes.front().buffer);
    REQUIRE(secondIncludes.front().buffer);
    CHECK(server.sourceManager().getFullPath(firstIncludes.front().buffer.id) ==
          workspace.root / "first/inc/types.svh");
    CHECK(server.sourceManager().getFullPath(secondIncludes.front().buffer.id) ==
          workspace.root / "second/inc/types.svh");
    CHECK(first.doc->getSyntaxTree()
              ->options()
              .getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths == std::vector<fs::path>{workspace.root / "first/inc"});
    CHECK(second.doc->getSyntaxTree()
              ->options()
              .getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths == std::vector<fs::path>{workspace.root / "second/inc"});
    CHECK(server.m_driver->options.getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths.empty());
    CHECK(header.doc->getCompilation() == first.doc->getCompilation());
    CHECK(server.m_indexer.getFilesIncluding(workspace.root / "first/inc/types.svh") ==
          std::vector<fs::path>{workspace.root / "first/src/top.sv"});
    auto links = server.getDocDocumentLink({.textDocument = {second.m_uri}});
    REQUIRE(links);
    REQUIRE(links->size() == 1);
    CHECK(links->front().target == URI::fromFile(workspace.root / "second/inc/types.svh"));
    CHECK_FALSE(server.sourceManager().readHeader("types.svh", {}, nullptr, false, {}));
    CHECK(server.getConfig().incdirs.value().empty());
}

TEST_CASE(
    "Shallow include fallback measures distance from nested includers and matches full paths") {
    ConfigWorkspace workspace;
    workspace.write("lib/include/entry.svh", "`include \"detail/types.svh\"\n");
    workspace.write("lib/include/types.svh", "typedef int wrong_name_t;\n");
    workspace.write("lib/types/detail/types.svh", "typedef int value_t;\n");
    workspace.write("rtl/detail/types.svh", "typedef int wrong_context_t;\n");
    workspace.write("rtl/top.sv", "`include \"entry.svh\"\n"
                                  "module top(output value_t v); assign v = 0; endmodule\n");
    auto server = workspace.server();
    auto doc = server.openFile("rtl/top.sv");
    CHECK(doc.getDiagnostics().empty());
    auto includes = doc.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(includes.size() == 2);
    REQUIRE(includes.back().buffer);
    CHECK(server.sourceManager().getFullPath(includes.back().buffer.id) ==
          workspace.root / "lib/types/detail/types.svh");
}

TEST_CASE("Configured include paths take precedence over nearer indexed headers") {
    ConfigWorkspace workspace;
    workspace.write("first/inc/types.svh", "typedef int nearest_t;\n");
    workspace.write("first/inc/extra.svh", "typedef int extra_t;\n");
    workspace.write("second/inc/types.svh", "typedef int configured_t;\n");
    workspace.write("first/src/top.sv", "`include \"types.svh\"\n`include \"extra.svh\"\n"
                                        "module top(output configured_t v, output extra_t e);\n"
                                        "assign v = 0; assign e = 0; endmodule\n");
    workspace.write(".slang/server.json", R"({"incdirs":["second/inc"]})");
    auto server = workspace.server();
    auto doc = server.openFile("first/src/top.sv");
    CHECK(doc.getDiagnostics().empty());
    auto includes = doc.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(includes.size() == 2);
    REQUIRE(includes.front().buffer);
    CHECK(server.sourceManager().getFullPath(includes.front().buffer.id) ==
          workspace.root / "second/inc/types.svh");
    REQUIRE(includes.back().buffer);
    CHECK(server.sourceManager().getFullPath(includes.back().buffer.id) ==
          workspace.root / "first/inc/extra.svh");
}

TEST_CASE("Tree-local include paths are replaced when a document changes its includes") {
    ConfigWorkspace workspace;
    workspace.write("first/first.svh", "typedef int first_t;\n");
    workspace.write("second/second.svh", "typedef int second_t;\n");
    workspace.write("src/top.sv", "`include \"first.svh\"\nmodule top; endmodule\n");
    auto server = workspace.server();
    auto doc = server.openFile("src/top.sv");
    CHECK(doc.doc->getSyntaxTree()
              ->options()
              .getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths == std::vector<fs::path>{workspace.root / "first"});
    doc.replaceAll("`include \"second.svh\"\nmodule top; endmodule\n");
    doc.publishChanges();
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
    CHECK(doc.doc->getSyntaxTree()
              ->options()
              .getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths == std::vector<fs::path>{workspace.root / "second"});
    CHECK(server.m_driver->options.getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths.empty());
    CHECK(server.getConfig().incdirs.value().empty());
}

TEST_CASE("Conflicting header choices within one syntax tree remain unresolved") {
    ConfigWorkspace workspace;
    workspace.write("left/entry.svh", "`include \"types.svh\"\n");
    workspace.write("left/inc/types.svh", "typedef int left_t;\n");
    workspace.write("right/entry.svh", "`include \"types.svh\"\n");
    workspace.write("right/inc/types.svh", "typedef int right_t;\n");
    workspace.write("top.sv", "`include \"left/entry.svh\"\n`include \"right/entry.svh\"\n"
                              "module top; endmodule\n");
    auto server = workspace.server();
    auto doc = server.openFile("top.sv");
    auto tree = doc.doc->getSyntaxTree();
    REQUIRE(tree->getIncludeDirectives().size() == 4);
    CHECK_FALSE(tree->diagnostics().empty());
    for (const auto& include : tree->getIncludeDirectives()) {
        if (include.path == "types.svh")
            CHECK_FALSE(include.buffer);
    }
    CHECK(tree->options()
              .getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths.empty());
}

TEST_CASE("Shallow include fallback resolves tied matches in canonical path order") {
    ConfigWorkspace workspace;
    workspace.write("a/types.svh", "typedef int first_t;\n");
    workspace.write("b/types.svh", "typedef int second_t;\n");
    fs::create_directory_symlink(workspace.root / "b", workspace.root / "0alias");
    workspace.write("src/top.sv", "`include \"types.svh\"\n"
                                  "module top(output first_t v); assign v = 0; endmodule\n");
    auto server = workspace.server();
    for (bool reverse : {false, true}) {
        std::vector<std::string> files{(workspace.root / "0alias/types.svh").string(),
                                       (workspace.root / "b/types.svh").string(),
                                       (workspace.root / "a/types.svh").string(),
                                       (workspace.root / "src/top.sv").string()};
        if (reverse)
            std::ranges::reverse(files);
        server.m_indexer.startIndexing(files, {});
        CHECK(server.m_indexer.getNearestFileForInclude(
                  "types.svh", workspace.root / "src/top.sv") == workspace.root / "a/types.svh");
    }
    CHECK(server.m_indexer.getFilesIncluding(workspace.root / "a/types.svh") ==
          std::vector<fs::path>{workspace.root / "src/top.sv"});
    CHECK(server.m_indexer.getFilesIncluding(workspace.root / "b/types.svh").empty());
    auto doc = server.openFile("src/top.sv");
    CHECK(doc.getDiagnostics().empty());
    auto tree = doc.doc->getSyntaxTree();
    REQUIRE(tree->getIncludeDirectives().size() == 1);
    REQUIRE(tree->getIncludeDirectives().front().buffer);
    CHECK(server.sourceManager().getFullPath(tree->getIncludeDirectives().front().buffer.id) ==
          workspace.root / "a/types.svh");
    CHECK(tree->options()
              .getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths == std::vector<fs::path>{workspace.root / "a"});
    CHECK(server.m_driver->options.getOrDefault<slang::parsing::PreprocessorOptions>()
              .additionalIncludePaths.empty());
    CHECK(server.getConfig().incdirs.value().empty());
}

TEST_CASE("Shallow include fallback leaves system includes unresolved") {
    ConfigWorkspace workspace;
    workspace.write("a/types.svh", "typedef int first_t;\n");
    workspace.write("src/top.sv", "`include <types.svh>\nmodule top; endmodule\n");
    auto server = workspace.server();
    auto doc = server.openFile("src/top.sv");
    auto tree = doc.doc->getSyntaxTree();
    CHECK_FALSE(tree->diagnostics().empty());
    REQUIRE(tree->getIncludeDirectives().size() == 1);
    CHECK_FALSE(tree->getIncludeDirectives().front().buffer);
    CHECK(server.getConfig().incdirs.value().empty());
}

TEST_CASE("Shallow include fallback ignores deleted candidates and duplicate filesystem aliases") {
    ConfigWorkspace workspace;
    workspace.write("first/inc/types.svh", "typedef int value_t;\n");
    workspace.write("second/inc/types.svh", "typedef int value_t;\n");
    workspace.write("first/src/top.sv", "`include \"types.svh\"\n"
                                        "module top(output value_t v); assign v = 0; endmodule\n");
    auto expected = workspace.root / "first/inc/types.svh";
    auto server = workspace.server();
    SECTION("Deleted closest candidate") {
        fs::remove(expected);
        expected = workspace.root / "second/inc/types.svh";
    }
    SECTION("Multiple paths to the same candidate") {
        auto alias = workspace.root / "alias";
        fs::create_directory_symlink(workspace.root / "first/inc", alias);
        server.m_indexer.startIndexing(
            std::vector<Config::IndexConfig>{
                {.dirs = std::vector<std::string>{"first/inc", "alias", "second/inc"}}},
            workspace.root.string());
        REQUIRE(server.m_indexer.getFilesForInclude("types.svh").size() == 3);
    }
    SECTION("Include path through a directory alias") {
        auto alias = workspace.root / "alias";
        fs::create_directory_symlink(workspace.root / "first/inc", alias);
        server.m_indexer.startIndexing(
            std::vector<Config::IndexConfig>{
                {.dirs = std::vector<std::string>{"first/inc", "alias"}}},
            workspace.root.string());
        REQUIRE(server.m_indexer.getNearestFileForInclude(
                    "alias/types.svh", workspace.root / "first/src/top.sv") == alias / "types.svh");
        workspace.write("first/src/top.sv",
                        "`include \"alias/types.svh\"\n"
                        "module top(output value_t v); assign v = 0; endmodule\n");
    }
    auto doc = server.openFile("first/src/top.sv");
    CHECK(doc.getDiagnostics().empty());
    auto includes = doc.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(includes.size() == 1);
    REQUIRE(includes.front().buffer);
    CHECK(server.sourceManager().getFullPath(includes.front().buffer.id) == expected);
}

TEST_CASE("Build-file sources do not use shallow include fallback after opening or editing") {
    ConfigWorkspace workspace;
    workspace.write("headers/types.svh", "typedef int value_t;\n");
    const auto source = "`include \"types.svh\"\nmodule top; endmodule\n";
    workspace.write("src/top.sv", source);
    workspace.write("design.f", (workspace.root / "src/top.sv").generic_string() + "\n");
    auto server = workspace.server();
    auto explore = server.openFile("src/top.sv");
    CHECK(explore.doc->getSyntaxTree()->diagnostics().empty());
    server.setBuildFile((workspace.root / "design.f").string());
    auto build = server.openFile("src/top.sv");
    auto includes = build.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(includes.size() == 1);
    CHECK_FALSE(includes.front().buffer);
    build.append("// edited\n");
    build.publishChanges();
    build.save();
    includes = build.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(includes.size() == 1);
    CHECK_FALSE(includes.front().buffer);
    CHECK(server.getConfig().incdirs.value().empty());
}
