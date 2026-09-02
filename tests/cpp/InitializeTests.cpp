// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"

TEST_CASE("Initialize accepts a compatible editor extension") {
    auto params = rfl::json::read<lsp::InitializeParams>(R"(
{
  "capabilities": {
    "experimental": {
      "otherClientFeature": true,
      "slangClient": {
        "name": "vscode-slang",
        "version": "0.2.17"
      }
    }
  }
}
)");
    REQUIRE(params);
    ServerHarness server(std::move(*params));
}

TEST_CASE("Initialize warns about an old editor extension") {
    auto params = rfl::json::read<lsp::InitializeParams>(R"(
{
  "capabilities": {
    "experimental": {
      "slangClient": {
        "name": "vscode-slang",
        "version": "0.0.0"
      }
    }
  }
}
)");
    REQUIRE(params);
    ServerHarness server(std::move(*params));
    server.client.expectWarning("vscode-slang v0.0.0 is older than the server requirement");
}
