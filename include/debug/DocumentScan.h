// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "lsp/LspTypes.h"

namespace server {

/// Coverage findings for a single snapshot of an open document.
struct DebugScanResult {
    /// Document whose current in-memory contents were scanned.
    URI uri;
    /// Position encoding negotiated with the client for diagnostic ranges.
    std::string positionEncoding;
    /// Number of source identifiers considered, including skipped declaration-only syntax.
    size_t identifiers = 0;
    /// Resolved declaration sites checked without requesting completions.
    size_t declarations = 0;
    /// Reference sites checked for feature coverage.
    size_t references = 0;
    /// Number of real hover requests checked, excluding synthetic debug hovers.
    size_t hoverChecks = 0;
    /// Number of source definition requests checked, excluding built-in symbols.
    size_t definitionChecks = 0;
    /// Number of reference sites checked for their expected completion label.
    size_t completionChecks = 0;
    /// Findings with stable codes and syntax / completion context in diagnostic data.
    std::vector<lsp::Diagnostic> diagnostics;
};

} // namespace server
