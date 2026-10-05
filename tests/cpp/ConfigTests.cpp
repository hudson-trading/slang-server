// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Log.h"
#include "util/Process.h"
#include "utils/ServerHarness.h"
#include <catch2/generators/catch_generators.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <rfl/from_generic.hpp>

#include "slang/parsing/Preprocessor.h"
#include "slang/util/OS.h"
#include "slang/util/ScopeGuard.h"

namespace {

struct ConfigEnvironment {
    const char* name = "SLANG_SERVER_CONFIG_TEST_PATH";
    std::optional<std::string> previous;

    ConfigEnvironment() {
        if (const auto* value = std::getenv(name))
            previous = value;
    }

    ~ConfigEnvironment() { set(previous ? previous->c_str() : nullptr); }

    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name, value ? value : "");
#else
        if (value)
            setenv(name, value, true);
        else
            unsetenv(name);
#endif
    }
};

struct ConfigWorkspace {
    inline static unsigned nextId = 0;
    fs::path root = fs::weakly_canonical(fs::temp_directory_path()) /
                    fmt::format("slang_config_{}_{}", slang::OS::getpid(), nextId++);

    ConfigWorkspace(std::string_view suffix = {}) {
        root += suffix;
        fs::create_directories(root);
    }
    ~ConfigWorkspace() { fs::remove_all(root); }

    void write(const fs::path& path, std::string_view text) {
        fs::create_directories((root / path).parent_path());
        std::ofstream(root / path) << text;
    }

    std::string read(const fs::path& path) const {
        std::ifstream file(root / path);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    void git(std::initializer_list<std::string> arguments) const {
        std::vector<std::string> command{"git", "-C", root.string()};
        command.insert(command.end(), arguments.begin(), arguments.end());
        auto result = server::runProcess(command);
        if (!result)
            SKIP("Git is unavailable");
        INFO(result->output);
        REQUIRE(result->exitCode == 0);
    }

    ServerHarness server() const {
        return ServerHarness(lsp::InitializeParams{
            .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "config"}}}});
    }
};

} // namespace

TEST_CASE("Config path environment variables expand once and reject unresolved references") {
    ConfigEnvironment environment;
    const std::string reference = GENERATE("$SLANG_SERVER_CONFIG_TEST_PATH",
                                           "${SLANG_SERVER_CONFIG_TEST_PATH}",
                                           "$(SLANG_SERVER_CONFIG_TEST_PATH)");
    environment.set("library with spaces/${SLANG_SERVER_CONFIG_TEST_PATH}");
    CHECK(Config::expandPathVariables(reference + "/src").value() ==
          "library with spaces/${SLANG_SERVER_CONFIG_TEST_PATH}/src");
    CHECK(Config::expandPathVariables({}).value().empty());
    CHECK(Config::expandPathVariables("literal$").value() == "literal$");
    CHECK(Config::expandPathVariables("${unclosed").value() == "${unclosed");

    environment.set(nullptr);
    auto missing = Config::expandPathVariables(reference + "/src");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().what() == "environment variable " + reference + " is unset or empty");
    environment.set("");
    CHECK_FALSE(Config::expandPathVariables(reference + "/src"));
}

TEST_CASE("Unset config environment variables notify the client and skip unresolved paths") {
    ConfigEnvironment environment;
    environment.set(GENERATE(static_cast<const char*>(nullptr), ""));
    const bool invalidExclusion = GENERATE(false, true);
    ConfigWorkspace workspace;
    workspace.write("outside.sv", "module outside; endmodule\n");
    const std::string unresolved = "${SLANG_SERVER_CONFIG_TEST_PATH}/src";
    Config::IndexConfig entry{.dirs = std::vector<std::string>{unresolved}};
    if (invalidExclusion) {
        entry.dirs = std::vector<std::string>{"."};
        entry.excludeDirs = std::vector<std::string>{unresolved};
    }
    rfl::Generic::Object config;
    config["index"] = rfl::to_generic(std::vector<Config::IndexConfig>{entry});
    config["incdirs"] = rfl::to_generic(std::vector<std::string>{unresolved});
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));

    auto server = workspace.server();
    const std::string detail =
        " path '${SLANG_SERVER_CONFIG_TEST_PATH}/src': environment variable "
        "${SLANG_SERVER_CONFIG_TEST_PATH} is unset or empty. See [Environment setup]("
        "https://hudson-trading.github.io/slang-server/start/config/#environment-variables) "
        "for editor configuration instructions.";
    server.expectError("Invalid incdirs" + detail);
    server.expectError(
        std::string(invalidExclusion ? "Invalid index[0].excludeDirs" : "Invalid index[0].dirs") +
        detail);
    CHECK(server.m_indexer.getFilesForSymbol("outside").empty());
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{unresolved});
    auto doc = server.openFile("outside.sv");
    CHECK(doc.getDiagnostics().empty());
}

TEST_CASE("Config environment variables resolve index roots exclusions and include directories") {
    ConfigEnvironment environment;
    ConfigWorkspace workspace;
    ConfigWorkspace external(" library with spaces");
    const bool externalLibrary = GENERATE(false, true);
    auto& library = externalLibrary ? external : workspace;
    const fs::path prefix = externalLibrary ? "" : "library with spaces";
    const auto variableValue = externalLibrary ? external.root.generic_string() : prefix.string();
    environment.set(variableValue.c_str());
    library.write(prefix / "src/library_pkg.sv",
                  "package library_pkg; typedef int value_t; endpackage\n");
    library.write(prefix / "src/excluded/excluded_pkg.sv", "package excluded_pkg; endpackage\n");
    library.write(prefix / "include/header.svh", "`define LIBRARY_VALUE '0\n");
    workspace.write("outside.sv", "module outside; endmodule\n");
    const std::string indexRoot = "${SLANG_SERVER_CONFIG_TEST_PATH}/src";
    const std::string exclusion = (externalLibrary ? "" : "./") + indexRoot + "/excluded";
    const std::string includeRoot = "$(SLANG_SERVER_CONFIG_TEST_PATH)/include";
    rfl::Generic::Object config;
    config["index"] = rfl::to_generic(
        std::vector<Config::IndexConfig>{{.dirs = std::vector<std::string>{indexRoot},
                                          .excludeDirs = std::vector<std::string>{exclusion}}});
    config["incdirs"] = rfl::to_generic(std::vector<std::string>{includeRoot});
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));

    auto server = workspace.server();
    CHECK(server.m_indexer.getFilesForSymbol("library_pkg").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("excluded_pkg").empty());
    CHECK(server.m_indexer.getFilesForSymbol("outside").empty() == !externalLibrary);
    auto doc = server.openFile("source.sv", "`include \"header.svh\"\n"
                                            "import library_pkg::*;\n"
                                            "module source(output value_t value);\n"
                                            "assign value = `LIBRARY_VALUE; endmodule\n");
    INFO(rfl::json::write(doc.getDiagnostics()));
    CHECK(doc.getDiagnostics().empty());
    REQUIRE(doc.doc->getSyntaxTree()->getIncludeDirectives().size() == 1);
    CHECK(server.sourceManager().getFullPath(
              doc.doc->getSyntaxTree()->getIncludeDirectives().front().buffer.id) ==
          library.root / prefix / "include/header.svh");
    CHECK(server.getConfig().index.value().front().dirs.value() ==
          std::vector<std::string>{indexRoot});
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{includeRoot});
}

TEST_CASE("Config build paths expand environment variables without changing saved configuration") {
    ConfigEnvironment environment;
    ConfigWorkspace workspace(" with spaces");
    const std::string reference = GENERATE("$SLANG_SERVER_CONFIG_TEST_PATH",
                                           "${SLANG_SERVER_CONFIG_TEST_PATH}",
                                           "$(SLANG_SERVER_CONFIG_TEST_PATH)");
    const bool absolute = GENERATE(false, true);
    const auto value = absolute ? (workspace.root / "build files").generic_string() : "build files";
    environment.set(value.c_str());
    workspace.write("build files/design.f", "../source.sv\n");
    workspace.write("source.sv", "module source; endmodule\n");
    rfl::Generic::Object config;
    config["build"] = reference + "/design.f";
    config["buildRelativePaths"] = true;
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));
    const auto saved = workspace.read(".slang/server.json");

    auto server = workspace.server();
    REQUIRE(server.m_driver->comp);
    REQUIRE(server.m_driver->driver.syntaxTrees.size() == 1);
    CHECK(server.getConfig().build.value() == reference + "/design.f");
    CHECK(workspace.read(".slang/server.json") == saved);
    CHECK(server.openFile("source.sv").getDiagnostics().empty());
}

TEST_CASE("Unset config build variables notify the client and leave exploration available") {
    ConfigEnvironment environment;
    environment.set(GENERATE(static_cast<const char*>(nullptr), ""));
    ConfigWorkspace workspace;
    workspace.write(".slang/server.json",
                    R"({"build":"${SLANG_SERVER_CONFIG_TEST_PATH}/design.f"})");
    auto server = workspace.server();
    server.expectError("Invalid build path '${SLANG_SERVER_CONFIG_TEST_PATH}/design.f': "
                       "environment variable ${SLANG_SERVER_CONFIG_TEST_PATH} is unset or empty");
    CHECK_FALSE(server.m_driver->comp);
    CHECK(server.getConfig().build.value() == "${SLANG_SERVER_CONFIG_TEST_PATH}/design.f");
    CHECK(server.openFile("source.sv", "module source; endmodule\n").getDiagnostics().empty());
}

TEST_CASE("Config reload retains working state when environment variables are missing") {
    ConfigEnvironment environment;
    environment.set(nullptr);
    ConfigWorkspace workspace;
    workspace.write("source.sv", "module source; endmodule\n");
    workspace.write("design.f", "source.sv\n");
    workspace.write(".slang/server.json", R"({"build":"design.f","flags":"-DWIDTH=8"})");
    auto server = workspace.server();
    auto doc = server.openFile("source.sv", "module source; endmodule\n// unsaved edit\n");
    REQUIRE(doc.getDiagnostics().empty());
    auto* originalDriver = server.m_driver.get();
    auto originalDoc = server.getDoc(doc.m_uri);
    rfl::Generic::Object invalid;
    invalid["build"] = "design.f";
    invalid["flags"] = "-DWIDTH=16";
    const std::string unresolved = "${SLANG_SERVER_CONFIG_TEST_PATH}";
    fs::path changed = ".slang/server.json";
    SECTION("Build path") {
        invalid["build"] = unresolved + "/design.f";
    }
    SECTION("Include directory") {
        invalid["incdirs"] = rfl::to_generic(std::vector<std::string>{unresolved});
    }
    SECTION("Index directory") {
        invalid["index"] = rfl::to_generic(
            std::vector<Config::IndexConfig>{{.dirs = std::vector<std::string>{unresolved}}});
    }
    SECTION("Index exclusion") {
        invalid["index"] = rfl::to_generic(std::vector<Config::IndexConfig>{
            {.dirs = std::vector<std::string>{"."},
             .excludeDirs = std::vector<std::string>{unresolved}}});
    }
    SECTION("Config flags") {
        invalid["flags"] = "-DWIDTH=" + unresolved;
    }
    SECTION("Build file contents") {
        changed = "design.f";
        workspace.write(changed, unresolved + "/source.sv\n");
    }
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(invalid)));
    server.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{.uri = URI::fromFile(workspace.root / changed),
                      .type = lsp::FileChangeType::Changed}}});
    CHECK(server.m_driver.get() == originalDriver);
    CHECK(server.getDoc(doc.m_uri) == originalDoc);
    CHECK(server.getConfig().build.value() == "design.f");
    CHECK(server.getConfig().flags.value() == "-DWIDTH=8");
    CHECK(server.getDoc(doc.m_uri)->getText() == doc.m_text + '\0');

    workspace.write(".slang/server.json", R"({"build":"design.f","flags":"-DWIDTH=16"})");
    workspace.write("design.f", "source.sv\n");
    server.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{.uri = URI::fromFile(workspace.root / "source.sv"),
                      .type = lsp::FileChangeType::Changed}}});
    CHECK(server.m_driver.get() != originalDriver);
    CHECK(server.getConfig().flags.value() == "-DWIDTH=16");
    CHECK(server.m_driver->comp);
    CHECK(server.m_driver->isDocumentOpen(doc.m_uri));
    CHECK(server.getDoc(doc.m_uri)->getText() == doc.m_text + '\0');
    CHECK(server.m_driver->options.getOrDefault<parsing::PreprocessorOptions>().predefines ==
          std::vector<std::string>{"WIDTH=16"});
}

TEST_CASE("Unset flag and file-list variables notify the client before applying arguments") {
    ConfigEnvironment environment;
    environment.set(GENERATE(static_cast<const char*>(nullptr), ""));
    ConfigWorkspace workspace;
    std::string flags;
    std::string build;
    std::string errorPrefix;
    std::string finalError;
    const std::string invalid = "${SLANG_SERVER_CONFIG_TEST_PATH}\n"
                                "\"$SLANG_SERVER_CONFIG_TEST_PATH/source.sv\"\n"
                                "$(SLANG_SERVER_CONFIG_TEST_PATH)/source.sv\nsource.sv\n";
    workspace.write("source.sv", "module source; endmodule\n");
    SECTION("Config flags") {
        flags = invalid;
        finalError = "Failed to parse config flags";
    }
    SECTION("Nested file lists") {
        const std::string option = GENERATE("-f", "-F", "-C");
        const bool configuredBuild = GENERATE(false, true);
        workspace.write("outer.f", option + " \"" +
                                       (workspace.root / "nested/inner.f").generic_string() +
                                       "\"\n");
        workspace.write("nested/inner.f", invalid);
        errorPrefix = (workspace.root / "nested/inner.f").make_preferred().string() + ":1:1: ";
        if (configuredBuild) {
            build = "outer.f";
            finalError = "Failed to process build file: " + (workspace.root / "outer.f").string();
        }
        else {
            flags = "-f \"" + (workspace.root / "outer.f").generic_string() + "\"";
            finalError = "Failed to parse config flags";
        }
    }
    rfl::Generic::Object config;
    config["flags"] = flags;
    if (!build.empty())
        config["build"] = build;
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));
    auto server = workspace.server();
    server.expectError(errorPrefix + "environment variable ${SLANG_SERVER_CONFIG_TEST_PATH} "
                                     "is unset or empty");
    server.expectError(finalError);
    CHECK(server.m_driver->driver.syntaxTrees.empty());
}

TEST_CASE("Flag environment validation respects quotes comments and one-time expansion") {
    ConfigEnvironment environment;
    ConfigWorkspace workspace;
    const bool fileList = GENERATE(false, true);
    std::string text;
    std::vector<std::string> expected;
    SECTION("Comments and literal dollars do not require variables") {
        environment.set(nullptr);
        text = R"(# $SLANG_SERVER_CONFIG_TEST_PATH
// ${SLANG_SERVER_CONFIG_TEST_PATH}
/* $(SLANG_SERVER_CONFIG_TEST_PATH) */
+define+SINGLE='$SLANG_SERVER_CONFIG_TEST_PATH'
+define+ESCAPED=\$SLANG_SERVER_CONFIG_TEST_PATH
)";
        expected = {"SINGLE=$SLANG_SERVER_CONFIG_TEST_PATH",
                    "ESCAPED=$SLANG_SERVER_CONFIG_TEST_PATH"};
    }
    SECTION("Expanded values are not expanded again") {
        environment.set("${SLANG_SERVER_CONFIG_TEST_PATH}");
        text = "+define+VALUE=\"${SLANG_SERVER_CONFIG_TEST_PATH}\"";
        expected = {"VALUE=${SLANG_SERVER_CONFIG_TEST_PATH}"};
    }
    rfl::Generic::Object config;
    if (fileList) {
        workspace.write("design.f", text);
        config["build"] = "design.f";
    }
    else {
        config["flags"] = text;
    }
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));
    auto server = workspace.server();
    CHECK(server.m_driver->options.getOrDefault<parsing::PreprocessorOptions>().predefines ==
          expected);
}

TEST_CASE("Flag and nested file-list variables preserve quoted paths containing spaces") {
    ConfigEnvironment environment;
    ConfigWorkspace workspace;
    environment.set("sources with spaces");
    workspace.write("sources with spaces/source.sv", "module source; endmodule\n");
    workspace.write("nested.f", "\"${SLANG_SERVER_CONFIG_TEST_PATH}/source.sv\"\n");
    workspace.write("outer.f", "-f nested.f\n");
    workspace.write(".slang/server.json", R"({"build":"outer.f", "buildRelativePaths":true,
        "flags":"+define+VALUE=\"$(SLANG_SERVER_CONFIG_TEST_PATH)\""})");
    auto server = workspace.server();
    REQUIRE(server.m_driver->comp);
    REQUIRE(server.m_driver->driver.syntaxTrees.size() == 1);
    CHECK(server.openFile("sources with spaces/source.sv").getDiagnostics().empty());
    CHECK(server.m_driver->options.getOrDefault<parsing::PreprocessorOptions>().predefines ==
          std::vector<std::string>{"VALUE=sources with spaces"});
}

TEST_CASE("Auto-configure preserves environment variables when saving discovered paths") {
    ConfigEnvironment environment;
    ConfigWorkspace workspace(" with spaces");
    environment.set(workspace.root.string().c_str());
    workspace.git({"init", "-q"});
    workspace.write(".gitignore", "hw/generated/\n");
    workspace.write("hw/generated/generated.sv", "module generated; endmodule\n");
    workspace.write("hw/excluded/excluded.sv", "module excluded; endmodule\n");
    workspace.write("hw/include/header.svh", "typedef int header_t;\n");
    workspace.write("hw/extra/extra.svh", "typedef int extra_t;\n");
    workspace.write("hw/rtl/source.sv", "`include \"header.svh\"\n`include \"extra.svh\"\n"
                                        "module source(output header_t a, output extra_t b);\n"
                                        "assign a = '0; assign b = '0; endmodule\n");
    workspace.write(".slang/server.json", R"({
        "index": [{"dirs": ["${SLANG_SERVER_CONFIG_TEST_PATH}/hw"],
                   "excludeDirs": ["${SLANG_SERVER_CONFIG_TEST_PATH}/hw/excluded"]}],
        "incdirs": ["${SLANG_SERVER_CONFIG_TEST_PATH}/hw/include"]
    })");
    auto server = workspace.server();
    auto result = server.executeCommand({.command = "slang.autoConfigure"});
    REQUIRE(result);
    CHECK(result->to_bool().value());
    CHECK(server.getConfig().index.value().front().dirs.value() ==
          std::vector<std::string>{"${SLANG_SERVER_CONFIG_TEST_PATH}/hw"});
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"${SLANG_SERVER_CONFIG_TEST_PATH}/hw/excluded", "generated"});
    CHECK(server.getConfig().incdirs.value() ==
          std::vector<std::string>{"${SLANG_SERVER_CONFIG_TEST_PATH}/hw/include", "hw/extra"});
    CHECK(server.m_indexer.getFilesForSymbol("generated").empty());
    CHECK(server.m_indexer.getFilesForSymbol("excluded").empty());
    const auto saved = workspace.read(".slang/server.json");
    result = server.executeCommand({.command = "slang.autoConfigure"});
    REQUIRE(result);
    CHECK(result->to_bool().value());
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure narrows environment workspace roots and preserves their spelling") {
    ConfigEnvironment environment;
    ConfigWorkspace workspace(" with spaces");
    const std::string reference = GENERATE("$SLANG_SERVER_CONFIG_TEST_PATH",
                                           "${SLANG_SERVER_CONFIG_TEST_PATH}",
                                           "$(SLANG_SERVER_CONFIG_TEST_PATH)");
    const bool absolute = GENERATE(false, true);
    const auto value = absolute ? workspace.root.generic_string() : ".";
    environment.set(value.c_str());
    CAPTURE(reference, absolute);

    std::vector<std::string> sourceDirectories;
    std::vector<std::string> expected;
    SECTION("A common source subtree") {
        sourceDirectories = {"hw/rtl", "hw/include"};
        expected = {reference + "/hw"};
    }
    SECTION("Disconnected source subtrees") {
        sourceDirectories = {"a/b", "c/d"};
        expected = {reference + "/a/b", reference + "/c/d"};
    }
    SECTION("A source at the workspace root prevents narrowing") {
        sourceDirectories = {".", "hw"};
        expected = {reference};
    }
    workspace.write(fs::path(sourceDirectories[0]) / "top.sv", "module top; endmodule\n");
    workspace.write(fs::path(sourceDirectories[1]) / "types.svh", "typedef int value_t;\n");
    workspace.write("tools/generate.py", "# Not a source file\n");
    rfl::Generic::Object config;
    config["index"] = rfl::to_generic(
        std::vector<Config::IndexConfig>{{.dirs = std::vector<std::string>{reference}}});
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));

    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() == expected);
    CHECK(server.m_indexer.getFilesForInclude("top.sv") ==
          std::vector<fs::path>{
              (workspace.root / sourceDirectories[0] / "top.sv").lexically_normal()});
    CHECK(server.m_indexer.getFilesForInclude("types.svh") ==
          std::vector<fs::path>{workspace.root / sourceDirectories[1] / "types.svh"});
    const auto saved = workspace.read(".slang/server.json");
    CHECK(saved.find(reference) != std::string::npos);
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

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

TEST_CASE("Config environment overrides merge by name and remain literal") {
    ConfigEnvironment environment;
    environment.set("inherited");
    ConfigWorkspace workspace;
    workspace.write("workspace.json", R"({"env":{"SHARED":"workspace","WORKSPACE":"kept",
        "SLANG_SERVER_CONFIG_TEST_PATH":"configured"}})");
    workspace.write("user.json", R"({"env":{"SHARED":"user","USER":"kept"}})");
    const bool localOverride = GENERATE(false, true);
    workspace.write("local.json",
                    localOverride
                        ? R"({"env":{"SHARED":"local","LITERAL":"${UNEXPANDED}","EMPTY":""}})"
                        : R"({"env":{}})");
    auto result = Config::fromFiles((workspace.root / "workspace.json").string(),
                                    (workspace.root / "user.json").string(),
                                    (workspace.root / "local.json").string());
    REQUIRE(result);
    const auto& config = *result;
    CHECK(config.env.value().at("SHARED") == (localOverride ? "local" : "user"));
    CHECK(config.env.value().at("WORKSPACE") == "kept");
    CHECK(config.env.value().at("USER") == "kept");
    CHECK(std::string(std::getenv(environment.name)) == "inherited");
    if (localOverride) {
        CHECK(config.env.value().at("LITERAL") == "${UNEXPANDED}");
        CHECK(config.env.value().at("EMPTY").empty());
    }
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

TEST_CASE("Auto-configure persists incdirs only when invoked and preserves settings") {
    ConfigWorkspace workspace;
    workspace.write("headers/entry.svh", "`include \"nested.svh\"\n");
    workspace.write("nested/nested.svh", "typedef int value_t;\n");
    workspace.write("source.sv", "`include \"entry.svh\"\nmodule source; value_t v; endmodule\n");
    workspace.write(".slang/server.json", R"({"flags":"-DSHARED", "indexingThreads":1})");
    workspace.write(".slang/local/server.json", R"({"flags":"-DLOCAL"})");
    const auto original = workspace.read(".slang/server.json");
    const auto local = workspace.read(".slang/local/server.json");
    auto server = workspace.server();
    CHECK_FALSE(server.sourceManager().readHeader("entry.svh", {}, nullptr, false, {}));
    auto doc = server.openFile("source.sv");
    CHECK(doc.doc->getSyntaxTree()->diagnostics().empty());
    doc.append("// unsaved edit\n");
    doc.publishChanges();
    CHECK(workspace.read(".slang/server.json") == original);
    CHECK(server.getConfig().incdirs.value().empty());

    const auto result = server.executeCommand({.command = "slang.autoConfigure"});
    REQUIRE(result);
    CHECK(result->to_bool().value());
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"headers", "nested"});
    CHECK(server.getConfig().flags.value() == "-DSHARED -DLOCAL");
    CHECK(server.getConfig().indexingThreads.value() == 1);
    auto updated = server.m_driver->getDocument(doc.m_uri);
    REQUIRE(updated);
    CHECK(updated->getText().find("// unsaved edit") != std::string_view::npos);
    CHECK(updated->getSyntaxTree()->diagnostics().empty());

    const auto saved = workspace.read(".slang/server.json");
    CHECK(saved != original);
    CHECK(workspace.read(".slang/local/server.json") == local);
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
    CHECK(workspace.read(".slang/local/server.json") == local);
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"headers", "nested"});
}

TEST_CASE("Only auto-configure infers and logs workspace include directories") {
    const bool useGlobs = GENERATE(false, true);
    CAPTURE(useGlobs);
    ConfigWorkspace workspace;
    workspace.write("headers/entry.svh", "typedef int value_t;\n");
    workspace.write("source.sv", "`include \"entry.svh\"\n"
                                 "module source(output value_t v); assign v = 0; endmodule\n");
    rfl::Generic::Object config;
    config["indexingThreads"] = 1;
    if (useGlobs)
        config["indexGlobs"] = rfl::to_generic(std::vector<std::string>{
            (workspace.root / "*.sv").string(), (workspace.root / "headers/*.svh").string()});
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));

    auto* output = std::tmpfile();
    REQUIRE(output);
    auto* previousOutput = server::logging::setOutput(output);
    slang::ScopeGuard cleanup([&] {
        server::logging::setOutput(previousOutput);
        std::fclose(output);
    });
    auto logs = [&] {
        REQUIRE(std::fflush(output) == 0);
        auto size = std::ftell(output);
        REQUIRE(size >= 0);
        REQUIRE(std::fseek(output, 0, SEEK_SET) == 0);
        std::string text(static_cast<size_t>(size), '\0');
        REQUIRE(std::fread(text.data(), 1, text.size(), output) == text.size());
        REQUIRE(std::fseek(output, 0, SEEK_END) == 0);
        return text;
    };

    auto server = workspace.server();
    CHECK(server.m_indexer.getIncludeDirectories().empty());
    CHECK(server.m_indexer.getFilesIncluding(workspace.root / "headers/entry.svh") ==
          std::vector<fs::path>{workspace.root / "source.sv"});
    auto doc = server.openFile("source.sv");
    CHECK(doc.getDiagnostics().empty());
    doc.append("// saved edit\n");
    doc.save();
    auto current = server.getConfig();
    server.loadConfig(current, true);
    CHECK(server.m_indexer.getIncludeDirectories().empty());
    CHECK(logs().find("inferred include directories") == std::string::npos);
    CHECK(logs().find("Inferred include directory:") == std::string::npos);
    CHECK(logs().find("Include-directory inference") == std::string::npos);

    REQUIRE(server.autoConfigure({}));
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"headers"});
    auto configuredLogs = logs();
    CHECK(configuredLogs.find("Found 1 inferred include directories") != std::string::npos);
    CHECK(configuredLogs.find("Inferred include directory: " +
                              (workspace.root / "headers").string()) != std::string::npos);
}

TEST_CASE("External index roots supplement the default workspace scan") {
    const bool relative = GENERATE(false, true);
    ConfigWorkspace files;
    const auto root = files.root / "workspace";
    const auto external = files.root / "workspace_extra";
    files.write("workspace/rtl/top.sv", "module top; endmodule\n");
    files.write("workspace/generated/local.sv", "module local_source; endmodule\n");
    files.write("workspace_extra/library.sv", "module library_source; endmodule\n");
    files.write("workspace_extra/generated/hidden.sv", "module hidden; endmodule\n");
    const std::vector<Config::IndexConfig> index{
        {.dirs = std::vector<std::string>{relative ? "../workspace_extra" : external.string()},
         .excludeDirs = std::vector<std::string>{"generated"}}};
    Indexer indexer;
    indexer.startIndexing(index, root.string());
    for (const auto* name : {"top", "local_source", "library_source"})
        CHECK(indexer.getFilesForSymbol(name).size() == 1);
    CHECK(indexer.getFilesForSymbol("hidden").empty());
    CHECK(indexer.getIncludeDirectories().empty());
    CHECK(indexer.getSuggestedIndexDirectories(root).empty());

    Indexer withoutWorkspace;
    withoutWorkspace.startIndexing(index, std::nullopt);
    CHECK(withoutWorkspace.getFilesForSymbol("top").empty());
    CHECK(withoutWorkspace.getFilesForSymbol("library_source").size() == (relative ? 0 : 1));
}

TEST_CASE("Explicit workspace index scopes replace the default scan") {
    ConfigWorkspace files;
    const auto root = files.root / "workspace";
    files.write("workspace/rtl/top.sv", "module top; endmodule\n");
    files.write("workspace/unselected/hidden.sv", "module hidden; endmodule\n");
    files.write("external/library.sv", "module library_source; endmodule\n");
    std::vector<Config::IndexConfig> index{
        {.dirs = std::vector<std::string>{(files.root / "external").string()}}};
    std::vector<std::string> directories;
    bool selected = true;
    SECTION("Relative workspace subdirectory") {
        directories = {"./rtl/../rtl"};
    }
    SECTION("Absolute workspace subdirectory") {
        directories = {(root / "rtl").string()};
    }
    SECTION("Workspace ancestor already covers the workspace") {
        directories = {".."};
    }
    SECTION("Explicit empty entry disables the implicit workspace") {
        selected = false;
    }
    SECTION("External alias of a workspace subdirectory") {
        std::error_code ec;
        fs::create_directory_symlink(root / "rtl", files.root / "alias", ec);
        if (ec)
            SKIP("Directory symlinks are unavailable");
        directories = {(files.root / "alias").string()};
    }
    index.push_back({.dirs = directories, .excludeDirs = std::vector<std::string>{"./unselected"}});
    Indexer indexer;
    indexer.startIndexing(index, root.string());
    CHECK(indexer.getFilesForSymbol("top").size() == (selected ? 1 : 0));
    CHECK(indexer.getFilesForSymbol("hidden").empty());
    CHECK(indexer.getFilesForSymbol("library_source").size() == 1);
}

TEST_CASE("Auto-configure adds workspace roots alongside external-only index settings") {
    const bool inherited = GENERATE(false, true);
    const bool sourceAtRoot = GENERATE(false, true);
    const bool ignored = GENERATE(false, true);
    CAPTURE(inherited, sourceAtRoot, ignored);
    ConfigWorkspace workspace;
    ConfigWorkspace external;
    workspace.write(sourceAtRoot ? "top.sv" : "hardware/rtl/top.sv", "module top; endmodule\n");
    workspace.write("hardware/include/types.svh", "typedef int value_t;\n");
    external.write("library.sv", "module library_source; endmodule\n");
    external.write("generated/hidden.sv", "module hidden; endmodule\n");
    if (ignored) {
        workspace.git({"init", "-q"});
        workspace.write("hardware/.gitignore", "/build/\n");
        workspace.write("hardware/build/stale.sv", "module stale; endmodule\n");
    }
    rfl::Generic::Object externalEntry;
    externalEntry["dirs"] = rfl::to_generic(std::vector<std::string>{external.root.string()});
    externalEntry["excludeDirs"] = rfl::to_generic(std::vector<std::string>{"generated"});
    externalEntry["note"] = "preserve external settings";
    rfl::Generic::Object original;
    original["index"] = rfl::Generic::Array{externalEntry};
    const auto configPath = inherited ? ".slang/local/server.json" : ".slang/server.json";
    const auto originalText = rfl::json::write(rfl::Generic(original));
    workspace.write(configPath, originalText);
    auto server = workspace.server();
    CHECK(server.m_indexer.getFilesForSymbol("top").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("library_source").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("hidden").empty());
    REQUIRE(server.autoConfigure({}));

    if (!sourceAtRoot || ignored) {
        REQUIRE(server.getConfig().index.value().size() == 2);
        const auto& index = server.getConfig().index.value()[inherited ? 0 : 1];
        CHECK(index.dirs.value() == std::vector<std::string>{sourceAtRoot ? "." : "hardware"});
        if (ignored)
            CHECK(index.excludeDirs.value() == std::vector<std::string>{"build"});
        else
            CHECK_FALSE(index.excludeDirs.value());
        const auto saved = rfl::json::read<rfl::Generic>(workspace.read(".slang/server.json"));
        REQUIRE(saved);
        const auto entries = saved->to_object()->at("index").to_array();
        REQUIRE(entries);
        CHECK(entries->size() == (inherited ? 1 : 2));
        if (!inherited)
            CHECK(rfl::json::write(entries->front()) ==
                  rfl::json::write(rfl::Generic(externalEntry)));
    }
    else {
        CHECK(workspace.read(configPath) == originalText);
        CHECK(fs::exists(workspace.root / ".slang/server.json") == !inherited);
    }
    if (inherited)
        CHECK(workspace.read(configPath) == originalText);
    CHECK(server.m_indexer.getFilesForSymbol("top").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("library_source").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("stale").empty());
    CHECK(server.m_indexer.getFilesForSymbol("hidden").empty());
    const auto saved = workspace.read(".slang/server.json");
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure saves the deepest common index directory and preserves open edits") {
    const bool inferIncludes = GENERATE(false, true);
    CAPTURE(inferIncludes);
    ConfigWorkspace workspace;
    workspace.write("src/hardware/include/types.svh", "typedef int value_t;\n");
    workspace.write("src/hardware/rtl/top.sv",
                    std::string(inferIncludes ? "`include \"types.svh\"\n" : "") +
                        "module top; endmodule\n");
    workspace.write("scripts/generate.py", "# Not a source file\n");
    workspace.write(".slang/server.json", R"({"flags":"-DPRESERVED", "indexingThreads":1,
                                             "index":[], "indexGlobs":[], "excludeDirs":[]})");
    const auto original = workspace.read(".slang/server.json");
    auto server = workspace.server();
    auto doc = server.openFile("src/hardware/rtl/top.sv");
    doc.append("// unsaved edit\n");
    doc.publishChanges();
    CHECK(server.getConfig().index.value().empty());
    CHECK(workspace.read(".slang/server.json") == original);

    const auto result = server.executeCommand({.command = "slang.autoConfigure"});
    REQUIRE(result);
    CHECK(result->to_bool().value());
    const auto& config = server.getConfig();
    REQUIRE(config.index.value().size() == 1);
    CHECK(config.index.value().front().dirs.value() == std::vector<std::string>{"src/hardware"});
    CHECK(config.flags.value() == "-DPRESERVED");
    CHECK(config.indexingThreads.value() == 1);
    CHECK(config.incdirs.value() == (inferIncludes
                                         ? std::vector<std::string>{"src/hardware/include"}
                                         : std::vector<std::string>{}));
    auto updated = server.m_driver->getDocument(doc.m_uri);
    REQUIRE(updated);
    CHECK(updated->getText().find("// unsaved edit") != std::string_view::npos);
    CHECK(updated->getSyntaxTree()->diagnostics().empty());

    const auto saved = workspace.read(".slang/server.json");
    CHECK(saved != original);
    auto json = rfl::json::read<rfl::Generic>(saved);
    REQUIRE(json);
    REQUIRE(json->to_object());
    CHECK(json->to_object()->count("incdirs") == (inferIncludes ? 1 : 0));
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
    CHECK(server.getConfig().index.value().size() == 1);
}

TEST_CASE("Auto-configure refreshes the file list before choosing the index directory") {
    ConfigWorkspace workspace;
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    auto server = workspace.server();
    std::string expected;
    SECTION("A single source uses its containing directory") {
        expected = "hardware/rtl";
    }
    SECTION("New headers widen the common directory") {
        workspace.write("hardware/include/types.svh", "typedef int value_t;\n");
        expected = "hardware";
    }
    SECTION("Directory components must match completely") {
        workspace.write("hardware/rtl_extra/types.svh", "typedef int value_t;\n");
        expected = "hardware";
    }
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() ==
          std::vector<std::string>{expected});
    CHECK(server.getConfig().incdirs.value().empty());
}

TEST_CASE("Auto-configure chooses separate roots for disconnected source trees") {
    const bool explicitRoot = GENERATE(false, true);
    CAPTURE(explicitRoot);
    ConfigWorkspace workspace;
    if (explicitRoot)
        workspace.write(".slang/server.json", R"({"index":[{"dirs":["."]}]})");
    std::vector<std::string> directories;
    SECTION("Nested source trees in separate top-level directories") {
        directories = {"a/b", "c/d"};
    }
    SECTION("Matching text prefixes do not merge directory components") {
        directories = {"hardware", "hardware_extra"};
    }
    workspace.write(fs::path(directories[0]) / "top.sv", "module top; endmodule\n");
    workspace.write(fs::path(directories[1]) / "types.svh", "typedef int value_t;\n");
    workspace.write("tools/scripts/generate.py", "# Not a source file\n");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() == directories);
    CHECK(server.m_indexer.getFilesForInclude("top.sv") ==
          std::vector<fs::path>{workspace.root / directories[0] / "top.sv"});
    CHECK(server.m_indexer.getFilesForInclude("types.svh") ==
          std::vector<fs::path>{workspace.root / directories[1] / "types.svh"});
    const auto saved = workspace.read(".slang/server.json");
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure balances crawl size against additional index roots") {
    ConfigWorkspace workspace;
    workspace.write("hardware/left/src/one.sv", "module one; endmodule\n");
    workspace.write("hardware/left/include/one.svh", "typedef int one_t;\n");
    workspace.write("hardware/right/rtl/two.sv", "module two; endmodule\n");
    workspace.write("hardware/right/include/two.svh", "typedef int two_t;\n");
    std::vector<std::string> expected;
    fs::path unrelated;
    SECTION("Dense branches stay together") {
        expected = {"hardware"};
    }
    SECTION("Unrelated files above dense branches favor separate parents") {
        unrelated = "hardware/docs";
        expected = {"hardware/left", "hardware/right"};
    }
    SECTION("Sparse and dense branches choose different depths") {
        unrelated = "hardware/left/docs";
        expected = {"hardware/left/include", "hardware/left/src", "hardware/right"};
    }
    SECTION("Sources directly in a parent require its coverage") {
        unrelated = "hardware/left/docs";
        workspace.write("hardware/top.sv", "module top; endmodule\n");
        expected = {"hardware"};
    }
    if (!unrelated.empty()) {
        for (int i = 0; i < 128; ++i)
            workspace.write(unrelated / fmt::format("entry{}.txt", i), "unrelated\n");
    }
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() == expected);
    for (const auto* filename : {"one.sv", "one.svh", "two.sv", "two.svh"})
        CHECK(server.m_indexer.getFilesForInclude(filename).size() == 1);
}

TEST_CASE(
    "Auto-configure keeps large source trees compact unless splitting saves substantial work") {
    ConfigWorkspace workspace;
    for (const auto* branch : {"left", "right"}) {
        for (int unit = 0; unit < 12; ++unit) {
            const auto path = fs::path("hardware") / branch / fmt::format("unit{}", unit);
            workspace.write(path / "rtl/unit.sv", "module unit; endmodule\n");
            workspace.write(path / "include/types.svh", "typedef int value_t;\n");
            for (int i = 0; i < 64; ++i)
                workspace.write(path / "notes" / fmt::format("entry{}.txt", i), "unrelated\n");
        }
    }
    std::vector<std::string> expected;
    SECTION("Small savings in many branches do not justify dozens of roots") {
        expected = {"hardware"};
    }
    SECTION("Two roots skip a large unrelated subtree") {
        for (int i = 0; i < 4000; ++i)
            workspace.write(fmt::format("hardware/docs/entry{}.txt", i), "unrelated\n");
        expected = {"hardware/left", "hardware/right"};
    }
    workspace.write(".slang/server.json", R"({"index":[{"dirs":["."]}]})");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() == expected);
    CHECK(server.m_indexer.getFilesForInclude("unit.sv").size() == 24);
    CHECK(server.m_indexer.getFilesForInclude("types.svh").size() == 24);
}

TEST_CASE("Index-directory suggestions use only explicit crawl metadata") {
    ConfigWorkspace workspace;
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    workspace.write("hardware/include/types.svh", "typedef int value_t;\n");
    auto server = workspace.server();
    auto& indexer = server.m_indexer;
    CHECK(indexer.getSuggestedIndexDirectories(workspace.root).empty());
    indexer.startIndexing(std::vector<Config::IndexConfig>{}, workspace.root.string(), true);
    CHECK(indexer.getSuggestedIndexDirectories(workspace.root) ==
          std::vector<fs::path>{"hardware"});
    for (int i = 0; i < 128; ++i)
        workspace.write(fmt::format("hardware/docs/entry{}.txt", i), "unrelated\n");
    CHECK(indexer.getSuggestedIndexDirectories(workspace.root) ==
          std::vector<fs::path>{"hardware"});
    indexer.startIndexing(std::vector<Config::IndexConfig>{}, workspace.root.string(), true);
    CHECK(indexer.getSuggestedIndexDirectories(workspace.root) ==
          std::vector<fs::path>{"hardware/include", "hardware/rtl"});
    indexer.startIndexing(std::vector<Config::IndexConfig>{}, workspace.root.string());
    CHECK(indexer.getSuggestedIndexDirectories(workspace.root).empty());
}

TEST_CASE("Auto-configure leaves the default index when narrowing cannot cover every source") {
    const bool explicitRoot = GENERATE(false, true);
    CAPTURE(explicitRoot);
    ConfigWorkspace workspace;
    if (explicitRoot)
        workspace.write(".slang/server.json", R"({"index":[{"dirs":["."]}]})");
    SECTION("Empty workspace") {
        workspace.write("scripts/generate.py", "# Not a source file\n");
    }
    SECTION("Source in the workspace root") {
        workspace.write("top.sv", "module top; endmodule\n");
        workspace.write("hardware/types.svh", "typedef int value_t;\n");
    }
    SECTION("Verilog sources and headers also constrain the index") {
        const auto extension = GENERATE(".v", ".vh");
        CAPTURE(extension);
        workspace.write("hardware/top.sv", "module top; endmodule\n");
        workspace.write(std::string("legacy") + extension, "`define WIDTH 8\n");
    }
    const auto original = workspace.read(".slang/server.json");
    auto server = workspace.server();
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == original);
    CHECK(fs::exists(workspace.root / ".slang/server.json") == explicitRoot);
}

TEST_CASE("Auto-configure narrows the workspace root while preserving exclusions and other roots") {
    const auto spelling = GENERATE(".", "./", "absolute");
    const bool sameEntry = GENERATE(false, true);
    CAPTURE(spelling, sameEntry);
    ConfigWorkspace workspace;
    ConfigWorkspace external;
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    workspace.write("hardware/include/types.svh", "typedef int value_t;\n");
    workspace.write("hardware/generated/hidden.sv", "module hidden; endmodule\n");
    workspace.write("generated/ignored.sv", "module ignored; endmodule\n");
    workspace.write("tools/build.py", "# Not a source file\n");
    external.write("vendor.svh", "typedef int vendor_t;\n");
    external.write("generated/hidden_vendor.svh", "typedef int hidden_vendor_t;\n");
    const auto externalDirectory = external.root.generic_string();
    const auto workspaceDirectory = std::string_view(spelling) == "absolute"
                                        ? workspace.root.generic_string()
                                        : std::string(spelling);
    std::vector<Config::IndexConfig> index{{.dirs = std::vector<std::string>{workspaceDirectory},
                                            .excludeDirs = std::vector<std::string>{"generated"}}};
    if (sameEntry)
        index.front().dirs.value().push_back(externalDirectory);
    else
        index.push_back({.dirs = std::vector<std::string>{externalDirectory},
                         .excludeDirs = std::vector<std::string>{"generated", "build"}});
    rfl::Generic::Object config;
    config["index"] = rfl::to_generic(index);
    config["flags"] = "-DPRESERVED";
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));

    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    index.front().dirs.value().front() = "hardware";
    config["index"] = rfl::to_generic(index);
    CHECK(workspace.read(".slang/server.json") ==
          rfl::json::write(rfl::Generic(config), YYJSON_WRITE_PRETTY_TWO_SPACES) + '\n');
    CHECK(rfl::json::write(server.getConfig().index.value()) == rfl::json::write(index));
    for (const auto* filename : {"top.sv", "types.svh", "vendor.svh"})
        CHECK(server.m_indexer.getFilesForInclude(filename).size() == 1);
    for (const auto* filename : {"hidden.sv", "ignored.sv", "hidden_vendor.svh"})
        CHECK(server.m_indexer.getFilesForInclude(filename).empty());
    const auto saved = workspace.read(".slang/server.json");
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure preserves explicit workspace and inherited index settings") {
    const auto field = GENERATE("index", "indexGlobs", "excludeDirs");
    const bool local = GENERATE(false, true);
    CAPTURE(field, local);
    ConfigWorkspace workspace;
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    rfl::Generic::Object config;
    if (std::string_view(field) == "index") {
        config["index"] = rfl::to_generic(std::vector<Config::IndexConfig>{
            {.dirs = std::vector<std::string>{local ? "." : "hardware"},
             .excludeDirs = std::vector<std::string>{"generated"}}});
    }
    else {
        config[field] = rfl::to_generic(std::vector<std::string>{
            std::string_view(field) == "indexGlobs" ? "hardware/.../*.sv*" : "generated"});
    }
    const auto configPath = local ? ".slang/local/server.json" : ".slang/server.json";
    const auto original = rfl::json::write(rfl::Generic(config));
    workspace.write(configPath, original);
    slang::ScopeGuard restoreDirectory(
        [previous = fs::current_path()] { fs::current_path(previous); });
    fs::current_path(workspace.root);
    auto server = workspace.server();
    const auto previous = rfl::json::write(server.getConfig());
    REQUIRE(server.autoConfigure({}));
    CHECK(workspace.read(configPath) == original);
    CHECK(rfl::json::write(server.getConfig()) == previous);
    if (local)
        CHECK_FALSE(fs::exists(workspace.root / ".slang/server.json"));
}

TEST_CASE("Auto-configure excludes Git ignored folders and preserves tracked files") {
    ConfigWorkspace workspace(" with spaces & dollars$ and 'quotes'");
    ConfigWorkspace external;
    workspace.git({"init", "-q"});
    workspace.write(".gitignore", "/build/\n*.tmp.sv\n!keep.tmp.sv\n");
    workspace.write("build/first.sv", "module first; endmodule\n");
    workspace.write("build/second.svh", "typedef int second_t;\n");
    workspace.write("build/tracked.sv", "module tracked; endmodule\n");
    workspace.write("tracked.tmp.sv", "module tracked_tmp; endmodule\n");
    workspace.write("keep.tmp.sv", "module kept; endmodule\n");
    workspace.write("file with spaces and 'quotes'.tmp.sv", "module ignored; endmodule\n");
    workspace.write("rtl/.gitignore", "local/\n");
    workspace.write("rtl/local/generated.sv", "module generated; endmodule\n");
    workspace.write("rtl/build/source.sv", "module source; endmodule\n");
    workspace.git({"add", "-f", "build/tracked.sv", "tracked.tmp.sv"});
    external.write("build/external.sv", "module external_source; endmodule\n");
    rfl::Generic::Object config;
    config["index"] = rfl::to_generic(std::vector<Config::IndexConfig>{
        {.dirs = std::vector<std::string>{".", external.root.generic_string()}}});
    workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));
    auto server = workspace.server();

    const auto matches = server.m_indexer.getGitIgnoreMatches(workspace.root);
    REQUIRE(matches.size() == 3);
    CHECK(matches[0].rule == ".gitignore:1: /build/");
    CHECK(matches[0].files.size() == 2);
    CHECK(matches[0].files.front() == "build/first.sv");
    CHECK(matches[0].directories.empty());
    CHECK(matches[1].rule == ".gitignore:2: *.tmp.sv");
    CHECK(matches[1].files.size() == 1);
    CHECK(matches[1].files.front() == "file with spaces and 'quotes'.tmp.sv");
    CHECK(matches[1].directories.empty());
    CHECK(matches[2].rule == "rtl/.gitignore:1: local/");
    CHECK(matches[2].files.size() == 1);
    CHECK(matches[2].directories == std::vector<fs::path>{"rtl/local"});

    std::string saved;
    for (int i = 0; i < 2; ++i) {
        REQUIRE(server.autoConfigure({}));
        server.client.expectWarning("found 3 indexed files matched by Git ignore rules");
        REQUIRE(server.getConfig().index.value().size() == 1);
        CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
              std::vector<std::string>{"local"});
        if (i == 0)
            saved = workspace.read(".slang/server.json");
        else
            CHECK(workspace.read(".slang/server.json") == saved);
        CHECK(server.m_indexer.getFilesForInclude("first.sv").size() == 1);
        CHECK(server.m_indexer.getFilesForInclude("generated.sv").empty());
        CHECK(server.m_indexer.getFilesForSymbol("generated").empty());
        CHECK(server.m_indexer.getFilesForInclude("tracked.sv").size() == 1);
        CHECK(server.m_indexer.getFilesForInclude("keep.tmp.sv").size() == 1);
    }
}

TEST_CASE("Git ignore reporting follows the refreshed index and skips submodules") {
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write(".gitignore", "build/\n");
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    workspace.write("hardware/build/excluded/hidden.sv", "module hidden; endmodule\n");
    workspace.write("build/outside.sv", "module outside; endmodule\n");
    workspace.write("hardware/vendor/build/submodule.sv", "module submodule_source; endmodule\n");
    workspace.git({"update-index", "--add", "--cacheinfo",
                   "160000,1111111111111111111111111111111111111111,hardware/vendor"});
    workspace.write(".slang/server.json",
                    R"({"index":[{"dirs":["hardware"],"excludeDirs":["excluded"]}]})");
    auto server = workspace.server();
    CHECK(server.m_indexer.getGitIgnoreMatches(workspace.root).empty());

    workspace.write("hardware/build/new.sv", "module new_source; endmodule\n");
    REQUIRE(server.autoConfigure({}));
    const auto matches = server.m_indexer.getGitIgnoreMatches(workspace.root);
    CHECK(matches.empty());
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"excluded", "./hardware/build"});
    CHECK(server.m_indexer.getFilesForInclude("new.sv").empty());
    CHECK(server.m_indexer.getFilesForSymbol("new_source").empty());
    CHECK(server.m_indexer.getFilesForInclude("hidden.sv").empty());
    CHECK(server.m_indexer.getFilesForInclude("outside.sv").empty());
    CHECK(server.m_indexer.getFilesForInclude("submodule.sv").size() == 1);
}

TEST_CASE("Git ignore reporting supports workspaces below the repository root") {
    ConfigWorkspace repository;
    repository.git({"init", "-q"});
    repository.write(".gitignore", "project/build/\n");
    repository.write("project/build/generated.sv", "module generated; endmodule\n");
    repository.write("project/source.sv", "module source; endmodule\n");
    const auto root = repository.root / "project";
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "project"}}}});
    REQUIRE(server.autoConfigure({}));
    const auto matches = server.m_indexer.getGitIgnoreMatches(root);
    CHECK(matches.empty());
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"build"});
    CHECK(server.m_indexer.getFilesForSymbol("generated").empty());
}

TEST_CASE("Git ignore snapshots protect tracked paths inside nested workspaces") {
    ConfigWorkspace repository;
    repository.git({"init", "-q"});
    repository.write("project/.gitignore", "build*/\n*.tmp.sv\n");
    repository.write("project/tracked [1].tmp.sv", "module tracked; endmodule\n");
    repository.write("project/build_source/kept.sv", "module kept; endmodule\n");
    repository.write("project/build_source/untracked.sv", "module untracked; endmodule\n");
    repository.write("project/build_docs/readme.txt", "tracked non-source file\n");
    repository.write("project/build_docs/generated.sv", "module generated; endmodule\n");
    repository.write("project/build_output/removed.sv", "module removed; endmodule\n");
    repository.git({"add", "-f", "project/tracked [1].tmp.sv", "project/build_source/kept.sv",
                    "project/build_docs/readme.txt"});
    const auto root = repository.root / "project";
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "project"}}}});

    const auto matches = server.m_indexer.getGitIgnoreMatches(root);
    REQUIRE(matches.size() == 1);
    CHECK(matches.front().files == std::vector<fs::path>{"build_docs/generated.sv",
                                                         "build_output/removed.sv",
                                                         "build_source/untracked.sv"});
    CHECK(matches.front().directories == std::vector<fs::path>{"build_output"});
    REQUIRE(server.autoConfigure({}));
    server.client.expectWarning("found 2 indexed files matched by Git ignore rules");
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"build_output"});
    CHECK(server.m_indexer.getFilesForSymbol("tracked").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("kept").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("removed").empty());
}

TEST_CASE("Auto-configure saves nested Git ignore folders before inferring include directories") {
    const bool explicitRoot = GENERATE(false, true);
    CAPTURE(explicitRoot);
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write("hardware/.gitignore", "/build/\n/out*/\n");
    workspace.write("hardware/top.sv", "`include \"live.svh\"\nmodule live; endmodule\n");
    workspace.write("hardware/include/live.svh", "typedef int live_t;\n");
    workspace.write("hardware/build/rtl/stale.sv",
                    "`include \"only_generated.svh\"\n`define STALE_MACRO 1\n"
                    "module stale; unused_dependency dep(); endmodule\n");
    workspace.write("hardware/headers/only_generated.svh", "typedef int generated_t;\n");
    workspace.write("hardware/build_extra/kept.sv", "module kept; endmodule\n");
    workspace.write("hardware/out_debug/debug.sv", "module debug_output; endmodule\n");
    workspace.write("hardware/out_release/release.sv", "module release_output; endmodule\n");
    workspace.write("software/build/tool.sv", "module tool; endmodule\n");
    workspace.write("software/top.sv", "module software_top; endmodule\n");
    if (explicitRoot)
        workspace.write(".slang/server.json", R"({"index":[{"dirs":["."]}]})");
    auto server = workspace.server();
    CHECK(server.m_indexer.getFilesForSymbol("stale").size() == 1);
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    const auto& index = server.getConfig().index.value().front();
    CHECK(index.dirs.value() == std::vector<std::string>{"hardware", "software"});
    CHECK(index.excludeDirs.value() ==
          std::vector<std::string>{"./hardware/build", "out_debug", "out_release"});
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"hardware/include"});
    CHECK(server.m_indexer.getFilesForSymbol("stale").empty());
    CHECK(server.m_indexer.getFilesForMacro("STALE_MACRO").empty());
    CHECK(server.m_indexer.getFilesReferencingSymbol("unused_dependency").empty());
    for (const auto* name : {"stale.sv", "debug.sv", "release.sv"})
        CHECK(server.m_indexer.getFilesForInclude(name).empty());
    for (const auto* name : {"kept.sv", "tool.sv", "live.svh"})
        CHECK(server.m_indexer.getFilesForInclude(name).size() == 1);
    const auto saved = workspace.read(".slang/server.json");
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure saves plain directory names and replaces existing paths and globs") {
    const auto previous = GENERATE("none", "paths", "globs");
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write("hardware/.gitignore", "outputs*/\n**/synth/**/.cache/\n");
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    workspace.write("hardware/unit/synth/board/outputs/rtl/generated.sv",
                    "module generated; endmodule\n");
    workspace.write("hardware/unit/synth/board/.cache/nested/stub.v", "module stub; endmodule\n");
    workspace.write("hardware/outputs_debug/types.svh", "typedef int value_t;\n");
    workspace.write("hardware/synth/.cache/stub.vh", "`define CACHE_VALUE 1\n");
    workspace.write("hardware/sim/.cache_extra/kept.sv", "module simulation; endmodule\n");
    workspace.write("software/synth/cached_source/kept.sv", "module software; endmodule\n");
    workspace.write("software/outputs_source/kept.sv", "module output_source; endmodule\n");
    workspace.write("hardware/rtl/outputs_file.sv", "module output_file; endmodule\n");
    if (std::string_view(previous) == "paths")
        workspace.write(".slang/server.json",
                        R"({"index":[{"dirs":["hardware","software"],"excludeDirs":[
            "./hardware/unit/synth/board/outputs", "./hardware/unit/synth/board/.cache"]}]})");
    else if (std::string_view(previous) == "globs")
        workspace.write(".slang/server.json",
                        R"({"index":[{"dirs":["hardware","software"],"excludeDirs":[
            "./hardware/**/outputs*", "./hardware/**/synth/**/.cache"]}]})");
    else
        workspace.write(".slang/server.json", R"({"index":[{"dirs":["hardware","software"]}]})");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    const auto& index = server.getConfig().index.value();
    REQUIRE(index.size() == 1);
    CHECK(index.front().excludeDirs.value() ==
          std::vector<std::string>{"outputs_debug", "outputs", ".cache"});
    for (const auto* name : {"generated", "stub"})
        CHECK(server.m_indexer.getFilesForSymbol(name).empty());
    for (const auto* name : {"top", "simulation", "software", "output_source", "output_file"})
        CHECK(server.m_indexer.getFilesForSymbol(name).size() == 1);
    CHECK(server.m_indexer.getFilesForInclude("types.svh").empty());
    CHECK(server.m_indexer.getFilesForMacro("CACHE_VALUE").empty());

    workspace.write("hardware/other/outputs/new.sv", "module new_output; endmodule\n");
    workspace.write("hardware/new/synth/.cache/new.v", "module new_cache; endmodule\n");
    const auto saved = workspace.read(".slang/server.json");
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
    CHECK(server.m_indexer.getFilesForSymbol("new_output").empty());
    CHECK(server.m_indexer.getFilesForSymbol("new_cache").empty());
}

TEST_CASE("Plain name and exact path exclusions respect boundaries and explicit index roots") {
    const auto extraExclusions = GENERATE(0, 6, 7, 8, 15, 16, 32);
    CAPTURE(extraExclusions);
    ConfigWorkspace workspace;
    workspace.write("hardware/unit/out_release/nested/hidden.sv", "module hidden; endmodule\n");
    workspace.write("hardware/unit/deeper/out_release/kept.sv", "module deeper; endmodule\n");
    workspace.write("hardware/cache/hidden.sv", "module cache_root; endmodule\n");
    workspace.write("hardware/unit/cache/hidden.sv", "module cache_nested; endmodule\n");
    workspace.write("hardware/unit/cache_extra/kept.sv", "module cache_extra; endmodule\n");
    workspace.write("hardware/unit/rtl.sv", "module live; endmodule\n");
    workspace.write("software/cache1/kept.sv", "module software; endmodule\n");
    workspace.write("hardware/cache.sv", "module same_file_name; endmodule\n");
    std::vector<std::string> exclusions{
        (workspace.root / "hardware/unit/out_release").generic_string(), "cache", "cache*"};
    for (int i = 0; i < extraExclusions; ++i) {
        exclusions.push_back(fmt::format("unused_{}", i));
        exclusions.push_back(fmt::format("./unused_paths/{}", i));
    }
    const std::vector<Config::IndexConfig> configs{
        {.dirs = std::vector<std::string>{"hardware", "software",
                                          "hardware/unit/out_release/nested", "hardware/cache"},
         .excludeDirs = std::move(exclusions)}};
    Indexer indexer;
    indexer.startIndexing(configs, workspace.root.string());
    for (const auto* name : {"hidden", "cache_root", "cache_nested"})
        CHECK(indexer.getFilesForSymbol(name).empty());
    for (const auto* name : {"deeper", "cache_extra", "live", "software", "same_file_name"})
        CHECK(indexer.getFilesForSymbol(name).size() == 1);
}

TEST_CASE("Cached directory exclusions preserve path boundaries with short and long lists") {
    const auto extraExclusions = GENERATE(0, 7, 8, 15, 16, 32);
    CAPTURE(extraExclusions);
    ConfigWorkspace workspace;
    workspace.write("hardware/output/nested/hidden.sv", "module hidden; endmodule\n");
    workspace.write("hardware/output/decls.svh", "`define HIDDEN 1\ntypedef int hidden_t;\n");
    workspace.write("hardware/outputs/kept.sv", "module prefix_neighbor; endmodule\n");
    workspace.write("hardware/unit/output/kept.sv", "module same_name; endmodule\n");
    workspace.write("hardware/output.sv", "module same_file_name; endmodule\n");
    Indexer indexer;
    indexer.startIndexing(std::vector<Config::IndexConfig>{}, workspace.root.string(), true);
    indexer.excludeDirectories({});
    REQUIRE(indexer.getFilesForSymbol("hidden").size() == 1);
    REQUIRE(indexer.getFilesForMacro("HIDDEN").size() == 1);
    REQUIRE(indexer.getHeadersForSymbol("hidden_t").size() == 1);

    std::vector<fs::path> excluded{workspace.root / "hardware/./output/../output/"};
    for (int i = 0; i < extraExclusions; ++i)
        excluded.push_back(workspace.root / fmt::format("hardware/unused_{}", i));
    for (int repeat = 0; repeat < 2; ++repeat) {
        indexer.excludeDirectories(excluded);
        CHECK(indexer.getFilesForSymbol("hidden").empty());
        CHECK(indexer.getFilesForMacro("HIDDEN").empty());
        CHECK(indexer.getHeadersForSymbol("hidden_t").empty());
        CHECK(indexer.getFilesForInclude("decls.svh").empty());
        for (const auto* name : {"prefix_neighbor", "same_name", "same_file_name"})
            CHECK(indexer.getFilesForSymbol(name).size() == 1);
    }
    excluded.front() = workspace.root;
    indexer.excludeDirectories(excluded);
    CHECK(indexer.getSymbolCount() == 0);
    CHECK(indexer.getFilesForInclude("kept.sv").empty());
}

TEST_CASE("Git directory exclusions keep specific paths when names would hide other files") {
    const bool tracked = GENERATE(false, true);
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write("hardware/.gitignore",
                    tracked ? "outputs*/\n" : "outputs*/\n!two/outputs_build/\n");
    workspace.write("hardware/rtl/top.sv", "module top; endmodule\n");
    workspace.write("hardware/one/outputs_build/generated.sv", "module generated; endmodule\n");
    if (tracked) {
        workspace.write("hardware/two/outputs_build/readme.txt", "tracked non-source file\n");
        workspace.git({"add", "-f", "hardware/two/outputs_build/readme.txt"});
    }
    else
        workspace.write("hardware/two/outputs_build/source.sv", "module kept; endmodule\n");
    workspace.write(".slang/server.json", R"({"index":[{"dirs":["hardware"]}]})");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"./hardware/one/outputs_build"});
    CHECK(server.m_indexer.getFilesForSymbol("generated").empty());
    if (!tracked)
        CHECK(server.m_indexer.getFilesForSymbol("kept").size() == 1);
}

TEST_CASE("Auto-configure omits ignored siblings when narrowing a nested workspace") {
    ConfigWorkspace repository;
    repository.git({"init", "-q"});
    repository.write("project/.gitignore", "build*/\n");
    repository.write("project/rtl/top.sv", "module top; endmodule\n");
    repository.write("project/unit/build_debug/generated.v", "module generated; endmodule\n");
    repository.write("project/unit/build_release/generated.vh", "`define GENERATED 1\n");
    const auto root = repository.root / "project";
    ServerHarness server(lsp::InitializeParams{
        .workspaceFolders = {{lsp::WorkspaceFolder{URI::fromFile(root), "project"}}}});
    REQUIRE(server.autoConfigure({}));
    CHECK(server.getConfig().index.value().front().dirs.value() == std::vector<std::string>{"rtl"});
    CHECK_FALSE(server.getConfig().index.value().front().excludeDirs.value());
    CHECK(server.m_indexer.getFilesForSymbol("generated").empty());
    CHECK(server.m_indexer.getFilesForMacro("GENERATED").empty());
}

TEST_CASE("Auto-configure excludes ignored directory names even without HDL files") {
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write("top.sv", "module top; endmodule\n");
    workspace.write("hardware/.gitignore", "build/\n.cache/\n");
    workspace.write("hardware/unit/build/logs/tool.log", "tool output\n");
    workspace.write("hardware/other/build/tool.log", "more output\n");
    workspace.write("hardware/unit/.cache/state.json", "{}\n");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"build", ".cache"});
    CHECK(server.m_indexer.getFilesForSymbol("top").size() == 1);
    const auto saved = workspace.read(".slang/server.json");
    CHECK(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure infers exclusions only inside its final index roots") {
    const auto scope = GENERATE("default", "workspace", "hardware");
    CAPTURE(scope);
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write(".gitignore", "/.backup/\n/logs/\n/target/\ncache/\n");
    workspace.write("hardware/.gitignore", "output/\n.temp/\n");
    workspace.write(".backup/tool.log", "backup\n");
    workspace.write("logs/tool.log", "log\n");
    workspace.write("target/generated.sv", "module outside_generated; endmodule\n");
    workspace.write("cache/tool.log", "cache\n");
    workspace.write("software/logs/readme.txt", "tracked file\n");
    workspace.git({"add", "software/logs/readme.txt"});
    workspace.write("hardware/top.sv", "module top; endmodule\n");
    workspace.write("hardware/cache/tool.log", "cache\n");
    workspace.write("hardware/output/generated.sv", "module inside_generated; endmodule\n");
    workspace.write("hardware/.temp/tool.log", "temporary output\n");
    if (std::string_view(scope) != "default") {
        rfl::Generic::Object config;
        config["index"] = rfl::to_generic(std::vector<Config::IndexConfig>{
            {.dirs = std::vector<std::string>{std::string_view(scope) == "workspace" ? "."
                                                                                     : "hardware"},
             .excludeDirs = std::vector<std::string>{"manual", "./manual/path"}}});
        workspace.write(".slang/server.json", rfl::json::write(rfl::Generic(config)));
    }
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 1);
    const auto& index = server.getConfig().index.value().front();
    CHECK(index.dirs.value() == std::vector<std::string>{"hardware"});
    std::vector<std::string> expected;
    if (std::string_view(scope) != "default")
        expected = {"manual", "./manual/path"};
    expected.insert(expected.end(), {"cache", "output", ".temp"});
    CHECK(index.excludeDirs.value() == expected);
    CHECK(server.m_indexer.getFilesForSymbol("top").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("outside_generated").empty());
    CHECK(server.m_indexer.getFilesForSymbol("inside_generated").empty());
    const auto saved = workspace.read(".slang/server.json");
    REQUIRE(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure assigns exclusions separately after replacing the workspace root") {
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write(".gitignore", ".backup/\noutput/\ncache/\n");
    workspace.write(".backup/tool.log", "backup\n");
    workspace.write("hardware/top.sv", "module top; endmodule\n");
    workspace.write("hardware/output/tool.log", "hardware output\n");
    workspace.write("hardware_extra/cache/tool.log", "unrelated cache\n");
    workspace.write("tools/cache/tool.log", "tools cache\n");
    workspace.write(".slang/server.json",
                    R"({"index":[{"dirs":["."],"note":"keep"},{"dirs":["tools"]}]})");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    REQUIRE(server.getConfig().index.value().size() == 2);
    const auto& index = server.getConfig().index.value();
    CHECK(index[0].dirs.value() == std::vector<std::string>{"hardware"});
    CHECK(index[0].excludeDirs.value() == std::vector<std::string>{"output"});
    CHECK(index[1].dirs.value() == std::vector<std::string>{"tools"});
    CHECK(index[1].excludeDirs.value() == std::vector<std::string>{"cache"});
    const auto saved = workspace.read(".slang/server.json");
    const auto json = rfl::json::read<rfl::Generic>(saved);
    REQUIRE(json);
    const auto entries = json->to_object()->at("index").to_array();
    REQUIRE(entries);
    CHECK(entries->front().to_object()->at("note").to_string().value() == "keep");
    REQUIRE(server.autoConfigure({}));
    CHECK(workspace.read(".slang/server.json") == saved);
}

TEST_CASE("Auto-configure keeps re-included directories and file-only ignore matches") {
    ConfigWorkspace workspace;
    workspace.git({"init", "-q"});
    workspace.write("hardware/.gitignore",
                    "generated/*\n!generated/keep/\n"
                    "generated/keep/*.tmp.sv\n!generated/keep/live.tmp.sv\n");
    workspace.write("hardware/top.sv", "module top; endmodule\n");
    workspace.write("hardware/generated/drop/stale.sv", "module stale; endmodule\n");
    workspace.write("hardware/generated/keep/live.tmp.sv", "module live; endmodule\n");
    workspace.write("hardware/generated/keep/scratch.tmp.sv", "module scratch; endmodule\n");
    workspace.write(".slang/server.json", R"({"index":[{"dirs":["hardware"]}]})");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    server.client.expectWarning("found 1 indexed file matched by Git ignore rules");
    CHECK(server.getConfig().index.value().front().excludeDirs.value() ==
          std::vector<std::string>{"drop"});
    CHECK(server.m_indexer.getFilesForSymbol("stale").empty());
    CHECK(server.m_indexer.getFilesForSymbol("live").size() == 1);
    CHECK(server.m_indexer.getFilesForSymbol("scratch").size() == 1);
}

TEST_CASE("Git ignore exclusions cover explicitly indexed folders and preserve inherited roots") {
    const bool inheritedOverlap = GENERATE(false, true);
    ConfigWorkspace workspace;
    ConfigWorkspace external;
    workspace.git({"init", "-q"});
    workspace.write("hardware/.gitignore", "build/\n");
    workspace.write("hardware/top.sv", "module top; endmodule\n");
    workspace.write("hardware/build/stale.sv", "module stale; endmodule\n");
    external.write("library.sv", "module library_source; endmodule\n");
    workspace.write(".slang/server.json",
                    R"({"index":[{"dirs":["hardware"]},{"dirs":["hardware/build"]}]})");
    rfl::Generic::Object local;
    local["index"] = rfl::to_generic(std::vector<Config::IndexConfig>{
        {.dirs = std::vector<std::string>{inheritedOverlap ? "hardware"
                                                           : external.root.string()}}});
    workspace.write(".slang/local/server.json", rfl::json::write(rfl::Generic(local)));
    const auto localText = workspace.read(".slang/local/server.json");
    const auto original = workspace.read(".slang/server.json");
    auto server = workspace.server();
    REQUIRE(server.autoConfigure({}));
    if (inheritedOverlap) {
        server.client.expectWarning("found 1 indexed file matched by Git ignore rules");
        CHECK(workspace.read(".slang/server.json") == original);
        CHECK(server.m_indexer.getFilesForSymbol("stale").size() == 1);
    }
    else {
        REQUIRE(server.getConfig().index.value().size() == 3);
        for (size_t i = 0; i < 2; ++i)
            CHECK(server.getConfig().index.value()[i].excludeDirs.value() ==
                  std::vector<std::string>{"build"});
        CHECK(server.m_indexer.getFilesForSymbol("stale").empty());
        CHECK(server.m_indexer.getFilesForInclude("stale.sv").empty());
        CHECK(server.m_indexer.getFilesForSymbol("library_source").size() == 1);
    }
    CHECK(workspace.read(".slang/local/server.json") == localText);
    CHECK(server.m_indexer.getFilesForSymbol("top").size() == 1);
}

TEST_CASE("Auto-configure refreshes discovery and deduplicates existing configuration") {
    ConfigWorkspace workspace;
    workspace.write("headers/first.svh", "typedef int first_t;\n");
    workspace.write("source.sv", "`include \"first.svh\"\nmodule source; endmodule\n");
    workspace.write(".slang/server.json", R"({"incdirs":["./headers/"]})");
    auto server = workspace.server();
    workspace.write("extra/second.svh", "typedef int second_t;\n");
    workspace.write("source.sv", "`include \"first.svh\"\n`include \"second.svh\"\n"
                                 "module source; endmodule\n");
    server.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(workspace.root / "source.sv"), lsp::FileChangeType::Changed},
                     {URI::fromFile(workspace.root / "extra/second.svh"),
                      lsp::FileChangeType::Created}}});
    CHECK_FALSE(fs::exists(workspace.root / ".slang/local/server.json"));
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"./headers/"});
    CHECK(server.autoConfigure({}));
    auto json = rfl::json::read<rfl::Generic>(workspace.read(".slang/server.json"));
    REQUIRE(json);
    auto incdirs = rfl::from_generic<std::vector<std::string>>(json->to_object()->at("incdirs"));
    REQUIRE(incdirs);
    CHECK(*incdirs == std::vector<std::string>{"./headers/", "extra"});
    CHECK(server.getConfig().incdirs.value() == std::vector<std::string>{"./headers/", "extra"});
    CHECK_FALSE(fs::exists(workspace.root / ".slang/local/server.json"));
}

TEST_CASE("Auto-configure skips conflicting directories and ambiguous include spellings") {
    ConfigWorkspace workspace;
    workspace.write("a/first.svh", "typedef int first_t;\n");
    workspace.write("b/second.svh", "typedef int second_t;\n");
    workspace.write("a/common.svh", "typedef int a_t;\n");
    workspace.write("b/common.svh", "typedef int b_t;\n");
    workspace.write("source.sv", "`include \"first.svh\"\n`include \"second.svh\"\n"
                                 "`include \"common.svh\"\nmodule source; endmodule\n");
    auto server = workspace.server();
    CHECK(server.autoConfigure({}));
    server.client.expectWarning("conflicting headers: a, b");
    CHECK(server.getConfig().incdirs.value().empty());
    CHECK_FALSE(fs::exists(workspace.root / ".slang/server.json"));
}

TEST_CASE("Auto-configure preserves configured headers when adding workspace incdirs") {
    ConfigWorkspace workspace;
    ConfigWorkspace external;
    fs::path configured;
    SECTION("Configured headers in the workspace index") {
        configured = workspace.root / "configured";
    }
    SECTION("Configured headers outside the workspace index") {
        configured = external.root;
    }
    fs::create_directories(configured);
    std::ofstream(configured / "choice.svh") << "typedef int chosen_t;\n";
    workspace.write("extra/choice.svh", "typedef int wrong_t;\n");
    workspace.write("extra/extra.svh", "typedef int extra_t;\n");
    workspace.write("safe/safe.svh", "typedef int safe_t;\n");
    rfl::Generic::Object config;
    config["incdirs"] = rfl::to_generic(std::vector<std::string>{configured.generic_string()});
    const auto localConfig = rfl::json::write(rfl::Generic(config));
    workspace.write(".slang/local/server.json", localConfig);
    workspace.write("discovery.sv", "`include \"extra.svh\"\n`include \"safe.svh\"\n");
    workspace.write("top.sv", "`include \"choice.svh\"\n"
                              "module top(output chosen_t value); assign value = 0; endmodule\n");
    auto server = workspace.server();
    auto doc = server.openFile("top.sv");
    REQUIRE(doc.getDiagnostics().empty());

    REQUIRE(server.autoConfigure({}));
    server.client.expectWarning("conflicting headers: extra");
    CHECK(server.getConfig().incdirs.value() ==
          std::vector<std::string>{"safe", configured.generic_string()});
    CHECK(workspace.read(".slang/local/server.json") == localConfig);
    auto updated = server.openFile("top.sv");
    CHECK(updated.getDiagnostics().empty());
    auto includes = updated.doc->getSyntaxTree()->getIncludeDirectives();
    REQUIRE(includes.size() == 1);
    REQUIRE(includes.front().buffer);
    CHECK(server.sourceManager().getFullPath(includes.front().buffer.id) ==
          configured / "choice.svh");
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
    CHECK(server.autoConfigure({}));
    CHECK(server.getConfig().incdirs.value().empty());
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() ==
          std::vector<std::string>{"first", "second"});
    auto updatedHeader = server.openFile("first/inc/types.svh");
    auto updatedFirst = server.openFile("first/src/top.sv");
    CHECK(updatedHeader.doc->getCompilation() == updatedFirst.doc->getCompilation());
    CHECK(updatedFirst.getDiagnostics().empty());
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
    CHECK(server.autoConfigure({}));
    CHECK(server.getConfig().incdirs.value().empty());
    REQUIRE(server.getConfig().index.value().size() == 1);
    CHECK(server.getConfig().index.value().front().dirs.value() ==
          std::vector<std::string>{"a", "b", "src"});
    CHECK(server.openFile("src/top.sv").getDiagnostics().empty());
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

TEST_CASE("Auto-configure does not overwrite malformed workspace configuration") {
    ConfigWorkspace workspace;
    workspace.write("headers/entry.svh", "typedef int value_t;\n");
    workspace.write("source.sv", "`include \"entry.svh\"\nmodule source; endmodule\n");
    auto server = workspace.server();
    std::string text;
    SECTION("Invalid JSON") {
        text = "{unfinished";
    }
    SECTION("Invalid incdirs") {
        text = R"({"incdirs":"headers"})";
    }
    workspace.write(".slang/server.json", text);
    CHECK_FALSE(server.autoConfigure({}));
    server.expectError("Cannot auto-configure:");
    CHECK(workspace.read(".slang/server.json") == text);
}

TEST_CASE("Auto-configure requires a workspace") {
    ServerHarness server;
    CHECK_FALSE(server.autoConfigure({}));
    server.expectError("Cannot auto-configure: no workspace folder");
}

TEST_CASE("Auto-configure preserves the most recently selected compilation") {
    ConfigWorkspace workspace;
    workspace.write("headers/entry.svh", "typedef int value_t;\n");
    workspace.write("index.sv", "`include \"entry.svh\"\n");
    workspace.write("top.sv", "module top; endmodule\n");
    workspace.write("source.sv", "module source; endmodule\n");
    workspace.write("design.f", (workspace.root / "source.sv").generic_string() + "\n");
    auto server = workspace.server();
    std::string selected;
    SECTION("Top selected after build") {
        server.setBuildFile((workspace.root / "design.f").string());
        server.setTopLevel((workspace.root / "top.sv").string());
        selected = "top";
    }
    SECTION("Build selected after top") {
        server.setTopLevel((workspace.root / "top.sv").string());
        server.setBuildFile((workspace.root / "design.f").string());
        selected = "source";
    }
    REQUIRE(server.getActiveInstance(selected));
    CHECK(server.autoConfigure({}));
    CHECK(server.getActiveInstance(selected));
    server.onWorkspaceDidChangeWatchedFiles(
        {.changes = {{URI::fromFile(workspace.root / ".slang/server.json"),
                      lsp::FileChangeType::Changed}}});
    CHECK(server.getActiveInstance(selected));
    CHECK_FALSE(server.getActiveInstance(selected == "top" ? "source" : "top"));
}
