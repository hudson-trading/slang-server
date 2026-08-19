// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "lsp/LspTypes.h"
#include <filesystem>
#include <optional>
#include <string>

class DocumentHandle;

namespace slang::parsing {
class Token;
}

class CompletionCoverageScanner {
public:
    void scanDocument(DocumentHandle hdl, std::filesystem::path relativePath);

private:
    struct CompletionIssue {
        lsp::uint line;
        lsp::uint column;
        lsp::uint length;
        std::string label;
        std::string contextKind;
        std::string triggerKind;
        size_t completionCount;
        std::optional<std::string> resolutionMismatch;
    };

    static std::optional<std::string> triggerForToken(const DocumentHandle& hdl,
                                                      const slang::parsing::Token& token);

    std::optional<CompletionIssue> checkToken(DocumentHandle& hdl,
                                              const slang::parsing::Token& token);
};
