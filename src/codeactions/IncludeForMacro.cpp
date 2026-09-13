//------------------------------------------------------------------------------
// IncludeForMacro.cpp
// Code action to add a `include for a macro that is used but not defined
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "codeactions/IncludeForMacro.h"

#include "Indexer.h"
#include "ServerDriver.h"
#include "util/Converters.h"
#include <algorithm>
#include <filesystem>

namespace server::codeactions {
using namespace slang;
namespace fs = std::filesystem;

// Find the macro usage node under the cursor. An undefined macro is not in the
// token index, but every usage is recorded in collectedHints.
static const syntax::MacroUsageSyntax* macroUsageAt(const ShallowAnalysis& analysis,
                                                    SourceLocation loc) {
    for (const auto& [offset, node] : analysis.syntaxes.collectedHints) {
        if (node->kind != syntax::SyntaxKind::MacroUsage)
            continue;
        auto range = node->sourceRange();
        if (range.start().buffer() == loc.buffer() && loc >= range.start() && loc <= range.end())
            return &node->as<syntax::MacroUsageSyntax>();
    }
    return nullptr;
}

// A new `include goes right after the last `include already at the top of this
// buffer, otherwise at the very start of the file.
static lsp::Position includeInsertionPoint(const syntax::SyntaxTree& tree, BufferID buffer,
                                           const SourceManager& sm) {
    lsp::Position pos{.line = 0, .character = 0};
    for (const auto& inc : tree.getIncludeDirectives()) {
        if (inc.syntax->fileName.location().buffer() != buffer)
            continue;
        auto end = toPosition(inc.syntax->sourceRange().end(), sm);
        if (end.line + 1 > pos.line)
            pos = lsp::Position{.line = end.line + 1, .character = 0};
    }
    return pos;
}

void addIncludeForMacroAction(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                              const CodeActionContext& ctx, SourceLocation loc) {
    auto* usage = macroUsageAt(ctx.analysis, loc);
    if (!usage)
        return;

    auto rawText = usage->directive.rawText();
    if (rawText.size() < 2 || rawText[0] != '`')
        return;
    auto macroName = std::string(rawText.substr(1));

    // Only when the macro is genuinely unresolved here.
    if (ctx.analysis.macros.contains(macroName))
        return;
    if (ctx.analysis.macroUsageDefinitions.contains(usage))
        return;

    auto paths = ctx.driver.getIndexer().getFilesForMacro(macroName);
    if (paths.empty())
        return;
    std::ranges::sort(paths);
    paths.erase(std::ranges::unique(paths).begin(), paths.end());

    auto tree = ctx.doc.getSyntaxTree();
    if (!tree)
        return;
    auto buffer = ctx.doc.getBuffer();

    std::vector<fs::path> alreadyIncluded;
    for (const auto& inc : tree->getIncludeDirectives()) {
        if (inc.syntax->fileName.location().buffer() != buffer || !inc.buffer.id)
            continue;
        alreadyIncluded.emplace_back(ctx.sourceManager.getFullPath(inc.buffer.id));
    }

    auto insertAt = includeInsertionPoint(*tree, buffer, ctx.sourceManager);
    fs::path thisFile(std::string(ctx.doc.getPath()));
    auto thisDir = thisFile.has_parent_path() ? thisFile.parent_path() : fs::current_path();

    for (const auto& macroPath : paths) {
        std::error_code ec;
        if (macroPath == thisFile || fs::equivalent(macroPath, thisFile, ec))
            continue;
        if (std::ranges::any_of(alreadyIncluded, [&](const fs::path& p) {
                std::error_code e;
                return p == macroPath || fs::equivalent(p, macroPath, e);
            }))
            continue;

        // Prefer a path relative to the current file; fall back to the bare
        // filename when the relative path would climb out of the tree.
        auto rel = fs::relative(macroPath, thisDir, ec);
        std::string incPath;
        if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0)
            incPath = macroPath.filename().string();
        else
            incPath = rel.generic_string();

        std::unordered_map<std::string, std::vector<lsp::TextEdit>> changes;
        changes[ctx.params.textDocument.uri.str()].push_back(lsp::TextEdit{
            .range = {.start = insertAt, .end = insertAt},
            .newText = fmt::format("`include \"{}\"\n", incPath),
        });

        results.push_back(lsp::CodeAction{
            .title = fmt::format("Add `include \"{}\" for `{}", incPath, macroName),
            .kind = lsp::CodeActionKindOptions::from_name<"quickfix">().str(),
            .edit = lsp::WorkspaceEdit{.changes = std::move(changes)},
        });
    }
}

} // namespace server::codeactions
