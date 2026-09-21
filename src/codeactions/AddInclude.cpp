//------------------------------------------------------------------------------
// AddInclude.cpp
// Include quick fixes for unresolved macros and header declarations
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "codeactions/AddInclude.h"

#include "Indexer.h"
#include "util/Converters.h"
#include <algorithm>
#include <cctype>

#include "slang/diagnostics/PreprocessorDiags.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/AllSyntax.h"

namespace server::codeactions {
using namespace slang;

void addIncludeActions(std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>>& results,
                       const CodeActionContext& ctx) {
    if (ctx.params.context.only &&
        std::ranges::none_of(*ctx.params.context.only,
                             [](const auto& kind) { return kind.empty() || kind == "quickfix"; }))
        return;

    auto tree = ctx.doc.getSyntaxTree();
    auto cursor = toSourceLocation(ctx.doc.getBuffer(), ctx.params.range.start, ctx.sourceManager);
    if (!cursor)
        return;

    auto text = ctx.doc.getText();
    std::string_view name;
    bool macro = false;
    // Unknown object-like macros may be discarded by preprocessing, leaving no syntax token.
    for (const auto& diag : tree->diagnostics()) {
        if (diag.code != diag::UnknownDirective || diag.location.buffer() != ctx.doc.getBuffer())
            continue;
        auto start = diag.location.offset();
        auto end = start + 1;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) ||
                                     text[end] == '_' || text[end] == '$'))
            ++end;
        if (cursor->offset() >= start && cursor->offset() <= end) {
            name = text.substr(start + 1, end - start - 1);
            macro = true;
            break;
        }
    }

    if (name.empty() && ctx.token && ctx.syntax) {
        if (ctx.syntax->kind == syntax::SyntaxKind::NamedConditionalDirectiveExpression) {
            name = ctx.token->valueText();
            macro = true;
            if (ctx.analysis.macros.contains(name))
                return;
        }
        else if ((ctx.syntax->kind == syntax::SyntaxKind::IdentifierName ||
                  ctx.syntax->kind == syntax::SyntaxKind::ClassName) &&
                 !ctx.analysis.getSymbolAtToken(ctx.token)) {
            name = ctx.token->valueText();
        }
    }
    if (name.empty())
        return;

    auto paths = macro ? ctx.indexer.getFilesForMacro(name) : ctx.indexer.getHeadersForSymbol(name);
    std::ranges::sort(paths);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    // Preserve the file's leading comments and blank lines, placing the include before code.
    size_t offset = 0;
    while (offset < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[offset]))) {
            ++offset;
        }
        else if (text.substr(offset).starts_with("//")) {
            auto end = text.find('\n', offset);
            if (end == std::string_view::npos)
                break;
            offset = end + 1;
        }
        else if (text.substr(offset).starts_with("/*")) {
            auto end = text.find("*/", offset + 2);
            if (end == std::string_view::npos)
                break;
            offset = end + 2;
        }
        else {
            break;
        }
    }
    if (!macro) {
        // Package members cannot use $unit declarations. Place the header before the enclosing
        // member, keeping earlier imports and declarations available to the included text.
        auto* member = ctx.syntax;
        while (const syntax::SyntaxNode* parent = member->parent) {
            if (parent->kind == syntax::SyntaxKind::CompilationUnit ||
                parent->getFirstToken().location().buffer() != ctx.doc.getBuffer())
                break;
            if (auto* declaration = parent->as_if<syntax::ModuleDeclarationSyntax>();
                declaration && member != declaration->header)
                break;
            member = parent;
        }
        offset = member->getFirstToken().location().offset();
    }
    else {
        // Keep new macro definitions from affecting headers already included before this use.
        for (const auto& include : tree->getIncludeDirectives()) {
            auto range = include.syntax->sourceRange();
            if (range.start().buffer() != ctx.doc.getBuffer() || range.end() > *cursor)
                continue;
            auto end = range.end().offset();
            auto newline = text.find('\n', end);
            if (newline != std::string_view::npos && newline < cursor->offset())
                end = newline + 1;
            offset = std::max(offset, size_t(end));
        }
    }
    auto lineStart = offset ? text.rfind('\n', offset - 1) : std::string_view::npos;
    lineStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    std::string_view indentation;
    if (text.substr(lineStart, offset - lineStart).find_first_not_of(" \t") ==
        std::string_view::npos) {
        indentation = text.substr(lineStart, offset - lineStart);
        offset = lineStart;
    }
    auto position = toPosition(SourceLocation(ctx.doc.getBuffer(), offset), ctx.sourceManager);
    const auto newline = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    auto currentPath = std::filesystem::path(ctx.doc.getPath()).lexically_normal();
    const auto configuredPaths =
        ctx.configuredOptions.getOrDefault<parsing::PreprocessorOptions>().additionalIncludePaths;
    auto& sourceManager = ctx.doc.getSourceManager();
    auto includeLoc = SourceLocation(ctx.doc.getBuffer(), offset);
    auto library = sourceManager.getLibraryFor(ctx.doc.getBuffer());

    for (const auto& path : paths) {
        auto fullPath = std::filesystem::absolute(path).lexically_normal();
        if (fullPath == currentPath)
            continue;
        if (std::ranges::any_of(tree->getSourceBufferIds(), [&](auto buffer) {
                return ctx.sourceManager.getFullPath(buffer) == fullPath;
            }))
            continue;

        std::filesystem::path spelling;
        auto suffix = fullPath.filename();
        auto directory = fullPath.parent_path();
        while (!directory.empty()) {
            auto configured = sourceManager.readHeader(suffix.generic_string(), includeLoc, library,
                                                       false, configuredPaths);
            if (configured && sourceManager.getFullPath(configured->id) == fullPath) {
                spelling = suffix;
                break;
            }
            if (directory == directory.root_path())
                break;
            suffix = directory.filename() / suffix;
            directory = directory.parent_path();
        }
        if (spelling.empty()) {
            spelling = fullPath.lexically_relative(currentPath.parent_path());
            if (spelling.empty())
                continue;
            auto relative = sourceManager.readHeader(spelling.generic_string(), includeLoc, library,
                                                     false, configuredPaths);
            if (!relative || sourceManager.getFullPath(relative->id) != fullPath)
                continue;
        }
        auto includePath = spelling.generic_string();
        if (includePath.empty() || includePath.find_first_of("\"\r\n") != std::string::npos)
            continue;
        if (std::ranges::any_of(tree->getIncludeDirectives(), [&](const auto& include) {
                return std::filesystem::path(include.path).lexically_normal() == spelling;
            }))
            continue;

        auto insertion = fmt::format("{}`include \"{}\"{}", indentation, includePath, newline);
        if (offset && text[offset - 1] != '\n')
            insertion.insert(0, newline);
        std::unordered_map<std::string, std::vector<lsp::TextEdit>> changes;
        changes[ctx.doc.getURI().str()].push_back(
            {.range = {position, position}, .newText = std::move(insertion)});
        results.push_back(lsp::CodeAction{
            .title = fmt::format("Add `include \"{}\"", includePath),
            .kind = lsp::CodeActionKindOptions::from_name<"quickfix">().str(),
            .edit = lsp::WorkspaceEdit{.changes = std::move(changes)},
        });
    }
}

} // namespace server::codeactions
