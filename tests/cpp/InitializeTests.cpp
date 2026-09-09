// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "utils/ServerHarness.h"
#include <fmt/format.h>
#include <string>
#include <string_view>

#include "slang/util/VersionInfo.h"

TEST_CASE("Initialize accepts a compatible editor extension") {
    auto paramsJson = std::string(R"(
{
  "capabilities": {
    "experimental": {
      "otherClientFeature": true,
      "slangClient": {
        "name": "vscode-slang",
        "version": "SERVER_VERSION"
      }
    }
  }
}
)");
    const auto compatibleVersion = fmt::format("{}.{}.0", slang::VersionInfo::getMajor(),
                                               slang::VersionInfo::getMinor());
    paramsJson.replace(paramsJson.find("SERVER_VERSION"), std::string_view("SERVER_VERSION").size(),
                       compatibleVersion);
    auto params = rfl::json::read<lsp::InitializeParams>(paramsJson);
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
