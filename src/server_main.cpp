//------------------------------------------------------------------------------
// server_main.cpp
// Generates C++ headers for SystemVerilog types
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

#include "SlangServer.h"
#ifdef SLANG_SERVER_DEBUG
#    include "debug/WorkspaceScan.h"
#    include <fstream>
#endif
#include <fmt/format.h>
#include <rfl/DefaultIfMissing.hpp>

#include "slang/util/CommandLine.h"
#include "slang/util/VersionInfo.h"

using namespace slang;
using namespace server;

int main(int argc, char** argv) {

#ifdef _WIN32
    // By default windows accesses streams in text mode. This mostly means that
    // line feeds are converted to carriage return-line feed (CRLF), this messes
    // up the language server protocol. The combat this to solution is to explicitly
    // set the IO line feed to use binary mode instead of text mode.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    OS::setupConsole();

    CommandLine cmdline;

    std::optional<bool> showHelp;
    cmdline.add("-h,--help", showHelp, "Display available options");

    std::optional<bool> showVersion;
    cmdline.add("--version", showVersion, "Display version information and exit");

    std::optional<bool> configSchema;
    cmdline.add("--config-schema", configSchema, "Print json schema of config file and exit");

#ifdef SLANG_SERVER_DEBUG
    std::optional<std::string> scanTarget, scanWorkspace, scanOutput;
    cmdline.add("--debug-scan", scanTarget, "Scan an HDL file or directory and report feature gaps",
                "path");
    cmdline.add("--workspace", scanWorkspace, "Workspace configuration root for --debug-scan",
                "path");
    cmdline.add("--output", scanOutput, "Write the scan JSON to this file instead of stdout",
                "path");
#endif

    if (!cmdline.parse(argc, argv)) {
        for (const auto& error : cmdline.getErrors())
            std::cerr << error.message << '\n';
        return 2;
    }

    if (showHelp == true) {
        OS::print(cmdline.getHelpText("Slang Language Server"));
        return 0;
    }

    if (showVersion == true) {
        OS::print(fmt::format("slang-server version {}.{}.{}+{}\n", VersionInfo::getMajor(),
                              VersionInfo::getMinor(), VersionInfo::getPatch(),
                              VersionInfo::getHash()));
        return 0;
    }

    if (configSchema == true) {
        try {
            const std::string schema = rfl::json::to_schema<Config, rfl::DefaultIfMissing>(
                rfl::json::pretty | YYJSON_WRITE_PRETTY_TWO_SPACES
                // Add this when comment support is added
                // , "@generated from `include/Config.h`"
            );
            OS::print(schema);
            OS::print("\n");
        }
        catch (const std::exception& e) {
            OS::print(fmt::format("Error generating config schema: {}\n", e.what()));
            return 1;
        }
        return 0;
    }
#ifdef SLANG_SERVER_DEBUG
    if (scanTarget) {
        auto report = debugScanWorkspace(*scanTarget, scanWorkspace.value_or(""));
        auto payload = rfl::json::write<rfl::UnderlyingEnums>(report, rfl::json::pretty) + "\n";
        if (!scanOutput) {
            std::cout << payload;
        }
        else {
            std::ofstream output(*scanOutput, std::ios::binary);
            output << payload;
            output.close();
            if (!output) {
                std::cerr << "Cannot write scan report: " << *scanOutput << '\n';
                return 2;
            }
        }
        std::cerr << rfl::json::write(report.summary) << '\n';
        for (const auto& group : report.groups)
            std::cerr << group.count << ' ' << group.code << ' ' << group.syntaxKind << ' '
                      << group.role << ' ' << group.completionKind << '\n';
        return !report.summary.complete ? 2 : report.summary.issues ? 1 : 0;
    }
    if (scanWorkspace || scanOutput) {
        std::cerr << "--workspace and --output require --debug-scan\n";
        return 2;
    }
#endif
    SlangLspClient client;
    SlangServer server(client);
    server.run();

    return 0;
}
