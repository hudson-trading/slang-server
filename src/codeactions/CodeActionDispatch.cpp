//------------------------------------------------------------------------------
// CodeActionDispatch.cpp
// Dispatches code action requests to individual action providers
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "codeactions/CodeActionDispatch.h"

#include "ServerDriver.h"
#include "codeactions/AddDefine.h"
#include "codeactions/ExpandMacro.h"
#include <rfl/Variant.hpp>

namespace server {
using namespace slang;

CodeActionDispatch::CodeActionDispatch(ServerDriver& driver, SourceManager& sm) :
    m_driver(driver), m_sourceManager(sm) {
}

std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>> CodeActionDispatch::getCodeActions(
    const lsp::CodeActionParams& params) {
    auto doc = m_driver.getDocument(params.textDocument.uri);
    if (!doc)
        return {};

    auto analysis = doc->getAnalysis();

    // Find the token and syntax at the cursor position
    const parsing::Token* token = nullptr;
    const syntax::SyntaxNode* syntax = nullptr;

    auto loc = m_sourceManager.getSourceLocation(doc->getBuffer(), params.range.start.line,
                                                 params.range.start.character);
    if (loc) {
        token = analysis->syntaxes.getWordTokenAt(*loc);
        if (token)
            syntax = analysis->syntaxes.getTokenParent(token);
    }

    CodeActionContext ctx{
        .params = params,
        .doc = *doc,
        .analysis = *analysis,
        .sourceManager = m_sourceManager,
        .token = token,
        .syntax = syntax,
        .diagnostics = params.context.diagnostics,
    };

    std::vector<rfl::Variant<lsp::Command, lsp::CodeAction>> results;

    // Syntax-based actions
    if (syntax) {
        switch (syntax->kind) {
            case syntax::SyntaxKind::MacroUsage:
                codeactions::addExpandMacroAction(results, ctx);
                break;
            case syntax::SyntaxKind::NamedConditionalDirectiveExpression:
                codeactions::addAddDefineAction(results, ctx);
                break;
            default:
                break;
        }
    }

    // Diagnostic-based actions
    // TODO: add quick fixes based on diagnostic code
    // for (const auto& diag : ctx.diagnostics) {
    //     if (!diag.code)
    //         continue;
    //     auto& code = rfl::get<std::string>(*diag.code);
    //     (void)code;
    //     // e.g. if (code == "some-warning") addSomeFixAction(results, ctx, diag);
    // }

    return results;
}

} // namespace server
