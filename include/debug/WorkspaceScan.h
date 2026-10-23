// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "debug/DocumentScan.h"
#include <filesystem>
#include <rfl/Flatten.hpp>

namespace server {

/// An open-document scan with its workspace path and compiler diagnostics.
struct DebugScannedDocument {
    /// Shared editor scan result, flattened into the JSON document object.
    rfl::Flatten<DebugScanResult> result;
    /// Workspace-relative source path.
    std::string path;
    /// Compiler findings, separate from feature coverage findings.
    std::vector<lsp::Diagnostic> compilerDiagnostics;
};

/// A feature finding's position in the document list.
struct DebugScanOccurrence {
    /// Workspace-relative source path.
    std::string path;
    /// Index into that document's diagnostics.
    size_t diagnostic;
    /// One-based source line.
    size_t line;
    /// One-based UTF-8 byte column.
    size_t byteColumn;
};

/// Findings with the same feature, syntax, token role, and completion context.
struct DebugScanGroup {
    /// Stable feature diagnostic code.
    std::string code;
    /// Parent syntax kind from the shallow token index.
    std::string syntaxKind;
    /// Whether the token declares or references a symbol.
    std::string role;
    /// Completion query kind, empty for hover and navigation findings.
    std::string completionKind;
    /// Completion trigger character or Invoked.
    std::string trigger;
    /// Whether definition lookup found a target.
    bool resolved;
    /// Number of findings in this group.
    size_t count = 0;
    /// Locations of every finding in this group.
    std::vector<DebugScanOccurrence> occurrences;
};

/// A failure to enumerate, open, or scan source files.
struct DebugScanError {
    /// Source path, absent for workspace initialization failures.
    std::optional<std::string> path;
    /// Failure details.
    std::string message;
};

/// Aggregate coverage and completion status for a batch scan.
struct DebugScanSummary {
    /// HDL files discovered under the target.
    size_t filesSelected = 0;
    /// Files with completed document scans.
    size_t filesScanned = 0;
    /// Total feature coverage findings.
    size_t issues = 0;
    /// Distinct groups of findings.
    size_t groups = 0;
    /// True if every selected file was scanned successfully.
    bool complete = false;
};

/// Agent-readable results of scanning a file or folder in one server workspace.
struct DebugWorkspaceScanResult {
    /// Report format version.
    int schemaVersion = 1;
    /// Absolute root used for configuration and indexing.
    std::string workspace;
    /// Absolute file or directory requested for scanning.
    std::string target;
    /// Encoding used by diagnostic ranges.
    std::string positionEncoding = "utf-8";
    /// Native server version used for the scan.
    std::optional<lsp::ServerInfo> serverInfo;
    /// Completed scans in source-path order.
    std::vector<DebugScannedDocument> documents;
    /// Groups ordered by decreasing occurrence count.
    std::vector<DebugScanGroup> groups;
    /// Failures that made the scan incomplete.
    std::vector<DebugScanError> errors;
    /// Configuration and analysis messages from the server.
    std::vector<lsp::ShowMessageParams> messages;
    /// Aggregate scan status and counts.
    DebugScanSummary summary;
};

/// Scan HDL files directly through the server's shallow analysis, without an LSP transport.
/// An empty workspace uses the target directory (or a target file's parent).
DebugWorkspaceScanResult debugScanWorkspace(const std::filesystem::path& target,
                                            const std::filesystem::path& workspace = {});

} // namespace server
