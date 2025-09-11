//------------------------------------------------------------------------------
// server_main.cpp
// Generates C++ headers for SystemVerilog types
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "SlangServer.h"
#include <fmt/format.h>
#include <rfl/DefaultIfMissing.hpp>

#include "slang/util/CommandLine.h"

using namespace slang;
using namespace server;

int main(int argc, char** argv) {
    OS::setupConsole();

    CommandLine cmdline;

    std::optional<bool> showHelp;
    cmdline.add("-h,--help", showHelp, "Display available options");

    std::optional<bool> configSchema;
    cmdline.add("--config-schema", configSchema, "Print json schema of config file and exit");

    cmdline.parse(argc, argv);

    if (showHelp == true) {
        OS::print(cmdline.getHelpText("Slang Language Server"));
        return 0;
    }

    if (configSchema == true) {
        try {
            const std::string schema = rfl::json::to_schema<Config, rfl::DefaultIfMissing>(
                rfl::json::pretty);
            OS::print(schema);
        }
        catch (const std::exception& e) {
            OS::print(fmt::format("Error generating config schema: {}\n", e.what()));
            return 1;
        }
        return 0;
    }
    SlangLspClient client;
    SlangServer server(client);
    server.run();

    return 0;
}
