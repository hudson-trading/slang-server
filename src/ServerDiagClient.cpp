//------------------------------------------------------------------------------
// ServerDiagClient.cpp
// Server diagnostic client implementation
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ServerDiagClient.h"

#include "fmt/format.h"
#include "lsp/LspTypes.h"
#include "lsp/URI.h"
#include "util/Converters.h"
#include "util/Logging.h"
#include <optional>
#include <string_view>
#include <sys/types.h>
#include <unordered_map>

#include "slang/diagnostics/CompilationDiags.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
namespace server {

lsp::DiagnosticSeverity convertSeverity(slang::DiagnosticSeverity severity) {
    switch (severity) {
        case slang::DiagnosticSeverity::Ignored:
            return lsp::DiagnosticSeverity::Hint;
        case slang::DiagnosticSeverity::Note:
            return lsp::DiagnosticSeverity::Information;
        case slang::DiagnosticSeverity::Warning:
            return lsp::DiagnosticSeverity::Warning;
        case slang::DiagnosticSeverity::Error:
            return lsp::DiagnosticSeverity::Error;
        case slang::DiagnosticSeverity::Fatal:
            return lsp::DiagnosticSeverity::Error;
    }
    return lsp::DiagnosticSeverity::Error;
}

void ServerDiagClient::report(const slang::ReportedDiagnostic& diag) {

    // Ignore this- happens all the time in explore mode, for example when looking at include files
    if (diag.originalDiagnostic.code == slang::diag::NoTopModules) {
        return;
    }

    // TODO: show include stack?
    // if (diag.shouldShowIncludeStack) {
    //     SmallVector<SourceLocation> includeStack;
    //     getIncludeStack(diag.location.buffer(), includeStack);

    //     // Show the stack in reverse.
    //     for (int i = int(includeStack.size()) - 1; i >= 0; i--) {
    //         SourceLocation loc = includeStack[size_t(i)];
    //         buffer->format("in file included from {}:{}:\n", getFileName(loc),
    //                        sourceManager->getLineNumber(loc));
    //     }
    // }

    // TODO: show hierarchy? Maybe also set the hierarchy when clicking on it?
    // Print out the hierarchy where the diagnostic occurred, if we know it.
    // auto& od = diag.originalDiagnostic;
    // auto& symbolPathCB = engine->getSymbolPathCB();
    // if (od.symbol && symbolPathCB && (od.coalesceCount)) {
    //     if (!od.coalesceCount || od.coalesceCount == 1u)
    //         buffer->append("  in instance: "sv);
    //     else
    //         buffer->format("  in {} instances, e.g. ", *od.coalesceCount);

    //     buffer->append(fmt::emphasis::bold, symbolPathCB(*od.symbol));
    //     buffer->append("\n"sv);
    // }

    // Slang diags have a location and range:
    //       /location
    // ~~~~~^~~~~~
    // range range (in ranges)
    // But LSP diags just have a range, so we need to combine them.

    auto getLocation = [&](SourceLocation loc, std::span<const SourceRange> ranges,
                           std::string_view message) -> std::optional<lsp::Location> {
        bool hasLocation = loc.buffer() != SourceLocation::NoLocation.buffer();
        if (ranges.empty()) {
            if (hasLocation) {
                return toLocation(loc, m_sourceManager);
            }
            else {
                ERROR("Diagnostic has no ranges and no location: {}", message);
            }
        }
        else {
            // collapse ranges into one, if they're all in the same buffer
            SourceRange totalRange = ranges[0];
            for (auto& range : ranges.subspan(1)) {
                if (range.start().buffer() != totalRange.start().buffer()) {
                    ERROR("Diagnostic has ranges in multiple buffers: {}", message);
                }
                else {
                    totalRange.start() = std::min(totalRange.start(), range.start());
                    totalRange.end() = std::max(totalRange.end(), range.end());
                }
            }
            if (hasLocation) {
                if (loc.buffer() != totalRange.start().buffer()) {
                    ERROR("Diagnostic location and ranges are in different buffers: {}", message);
                }
                else {
                    totalRange.start() = std::min(totalRange.start(), loc);
                    totalRange.end() = std::max(totalRange.end(), loc);
                }
            }
            return toLocation(totalRange, m_sourceManager);
        }
        return std::nullopt;
    };

    // Code similar to TextDiagnosticClient::report
    SmallVector<SourceRange> mappedRanges;
    engine->mapSourceRanges(diag.location, diag.ranges, mappedRanges);

    auto mainLoc = getLocation(diag.location, mappedRanges, diag.formattedMessage);
    if (!mainLoc) {
        return;
    }

    std::vector<lsp::DiagnosticRelatedInformation> related;
    for (auto it = diag.expansionLocs.rbegin(); it != diag.expansionLocs.rend(); it++) {
        SourceLocation loc = *it;
        std::string name(sourceManager->getMacroName(loc));
        if (name.empty())
            name = "expanded from here";
        else
            name = fmt::format("expanded from macro '{}'", name);

        SmallVector<SourceRange> macroRanges;
        engine->mapSourceRanges(loc, diag.ranges, macroRanges);

        auto relatedLoc = getLocation(sourceManager->getFullyOriginalLoc(loc), macroRanges, name);
        if (relatedLoc) {
            related.emplace_back(lsp::DiagnosticRelatedInformation{
                .location = *relatedLoc, .message = std::string{diag.formattedMessage}});
        }
    }
    // end of text diag related code

    auto uri = mainLoc->uri;
    m_dirtyUris.emplace(uri);
    m_diagnostics[uri].push_back(lsp::Diagnostic{
        .range = mainLoc->range,
        .severity = convertSeverity(diag.severity),
        .message = std::string{diag.formattedMessage},
        .relatedInformation = related.empty() ? std::nullopt : std::optional{related},
    });

    // Add diag code link if any
    std::string_view optionName = engine->getOptionName(diag.originalDiagnostic.code);
    if (!optionName.empty()) {
        m_diagnostics[uri].back().code = {std::string(optionName)};
        m_diagnostics[uri].back().codeDescription = lsp::CodeDescription{
            .href = URI::fromWeb("sv-lang.com/warning-ref.html#" + std::string(optionName))};

        if (optionName.starts_with("unused")) {
            m_diagnostics[uri].back().tags = {lsp::DiagnosticTag::Unnecessary};
        }
    }
}

void ServerDiagClient::clear(URI uri) {
    auto it = m_diagnostics.find(uri);
    if (it != m_diagnostics.end()) {
        m_diagnostics.erase(it);
    }
    m_dirtyUris.emplace(uri);
}

void ServerDiagClient::clear() {
    for (const auto& [uri, diags] : m_diagnostics) {
        m_client.onDocPublishDiagnostics(
            lsp::PublishDiagnosticsParams{.uri = uri, .diagnostics = {}});
    }
    for (auto& uri : m_dirtyUris) {
        m_client.onDocPublishDiagnostics(
            lsp::PublishDiagnosticsParams{.uri = uri, .diagnostics = {}});
    }
    m_diagnostics.clear();
    m_dirtyUris.clear();
}

void ServerDiagClient::updateDiags() {
    for (auto& uri : m_dirtyUris) {
        auto it = m_diagnostics.find(uri);
        if (it != m_diagnostics.end()) {
            m_client.onDocPublishDiagnostics(
                lsp::PublishDiagnosticsParams{.uri = uri, .diagnostics = it->second});
        }
        else {
            m_client.onDocPublishDiagnostics(
                lsp::PublishDiagnosticsParams{.uri = uri, .diagnostics = {}});
        }
    }
    m_dirtyUris.clear();
}

} // namespace server
