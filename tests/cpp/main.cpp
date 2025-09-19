// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include <catch2/catch_session.hpp>

#include "slang/diagnostics/Diagnostics.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include "slang/util/BumpAllocator.h"
#include "slang/util/OS.h"

#ifdef _WIN32
#    include <windows.h>
#endif

namespace slang {

BumpAllocator alloc;
Diagnostics diagnostics;

} // namespace slang

// --------------------------------------------------
// Optionally parse a custom flag with Catch2
// --------------------------------------------------

bool g_updateGoldenFlag = false;

// We can parse the flag from the command line here
int main(int argc, char** argv) {
    slang::OS::setupConsole();
    slang::syntax::SyntaxTree::getDefaultSourceManager().setDisableProximatePaths(true);

// Let slang-server know we're running tests
#ifdef _WIN32
    SetEnvironmentVariable(TEXT("SLANG_SERVER_TESTS"), TEXT("YES"));
#else
    setenv("SLANG_SERVER_TESTS", "YES", true);
#endif

    // We create a Catch2 session
    Catch::Session session;

    // Build a custom CLI parser
    using namespace Catch::Clara;
    auto cli = session.cli() | Opt(g_updateGoldenFlag)["--update"](
                                   "Update golden files instead of failing the test on mismatch");

    // Now we update the session's CLI
    session.cli(cli);

    // Let Catch parse the command line
    int returnCode = session.applyCommandLine(argc, argv);
    if (returnCode != 0) {
        return returnCode; // command line error
    }

    // Run tests
    return session.run();
}

// --------------------------------------------------
// Example function under test
// --------------------------------------------------
std::string generateOutput() {
    return "Line 1\nLine 2\nLine 3\n";
    // If we add or remove lines, it will fail unless we run --update
}
