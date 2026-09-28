//------------------------------------------------------------------------------
// NamedPortCompletions.cpp
// Named port and parameter connection completions inside a module instance.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "completions/NamedPortCompletions.h"

#include "document/ShallowAnalysis.h"
#include "document/SlangDoc.h"
#include "lsp/SnippetString.h"
#include <unordered_set>

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/syntax/AllSyntax.h"

namespace server::completions {
using namespace slang;

namespace {

/// Walk outwards from the cursor to the instance whose connection list it sits in.
const syntax::HierarchyInstantiationSyntax* findInstantiation(const CompletionContext& context,
                                                              SourceLocation cursor,
                                                              bool& inParameters) {
    auto* node = context.analysis->syntaxes.getSyntaxAt(cursor);
    if (!node) {
        auto* previous = context.analysis->syntaxes.getTokenBefore(cursor);
        node = context.analysis->syntaxes.getTokenParent(previous);
        if (!node)
            return nullptr;
    }

    bool sawParameterList = false;
    for (; node; node = node->parent) {
        if (syntax::ParameterValueAssignmentSyntax::isKind(node->kind))
            sawParameterList = true;
        if (auto* hierarchy = node->as_if<syntax::HierarchyInstantiationSyntax>()) {
            inParameters = sawParameterList;
            return hierarchy;
        }
    }
    return nullptr;
}

/// Names already connected in this instance, so they are not offered twice. The
/// partially typed connection under the cursor is skipped.
void collectConnected(const syntax::HierarchyInstantiationSyntax& syntax, SourceLocation cursor,
                      bool parameters, std::unordered_set<std::string_view>& taken) {
    auto record = [&](const parsing::Token& name, SourceRange range) {
        if (name.isMissing() || (range.start() <= cursor && cursor <= range.end()))
            return;
        taken.emplace(name.valueText());
    };

    if (parameters) {
        if (!syntax.parameters)
            return;
        for (auto* param : syntax.parameters->parameters) {
            if (auto* named = param->as_if<syntax::NamedParamAssignmentSyntax>())
                record(named->name, named->sourceRange());
        }
        return;
    }

    for (auto* instance : syntax.instances) {
        for (auto* connection : instance->connections) {
            if (auto* named = connection->as_if<syntax::NamedPortConnectionSyntax>())
                record(named->name, named->sourceRange());
        }
    }
}

class NamedPortCompletionQueryImpl final : public NamedPortCompletionQuery {
public:
    NamedPortCompletionQueryImpl(lsp::Range replacementRange, SourceLocation cursor,
                                 bool parameters, bool followedByCall) :
        NamedPortCompletionQuery(replacementRange, followedByCall), cursor(cursor),
        parameters(parameters) {}

    CompletionQueryKind kind() const final { return CompletionQueryKind::NamedPort; }

    void getCompletions(std::vector<lsp::CompletionItem>& results, CompletionDispatch&,
                        const std::shared_ptr<SlangDoc>&,
                        const CompletionContext& context) const final {
        bool inParameters = false;
        auto* syntax = findInstantiation(context, cursor, inParameters);
        if (!syntax)
            return;

        auto* module = context.analysis->getSymbolAtToken(&syntax->type);
        if (!module || module->kind != ast::SymbolKind::Definition)
            return;

        auto& def = module->as<ast::DefinitionSymbol>();

        std::unordered_set<std::string_view> taken;
        collectConnected(*syntax, cursor, parameters, taken);

        auto emit = [&](std::string_view name, lsp::CompletionItemKind itemKind,
                        std::string_view detail) {
            if (name.empty() || taken.contains(name))
                return;
            lsp::CompletionItem item{.label = std::string(name), .kind = itemKind};
            if (!detail.empty())
                item.detail = std::string(detail);
            if (!followedByCall) {
                SnippetString snippet;
                snippet.appendText(name).appendText("(").appendTabstop().appendText(")");
                item.insertText = snippet.getValue();
                item.insertTextFormat = lsp::InsertTextFormat::Snippet;
            }
            setCompletionEdit(item);
            results.push_back(std::move(item));
        };

        if (parameters) {
            for (auto& param : def.parameters)
                emit(param.name, lsp::CompletionItemKind::TypeParameter, "parameter");
            return;
        }

        // An instance that does not elaborate still has a definition to read
        // ports from, which is the usual state while the connection list is
        // being typed.
        const ast::InstanceSymbol* inst = nullptr;
        for (auto* instanceSyntax : syntax->instances) {
            if (!instanceSyntax->decl)
                continue;
            auto* sym = context.analysis->getSymbolAtToken(&instanceSyntax->decl->name);
            if (sym && sym->kind == ast::SymbolKind::Instance) {
                inst = &sym->as<ast::InstanceSymbol>();
                break;
            }
        }

        if (!inst)
            return;

        for (auto* port : inst->body.getPortList()) {
            if (port)
                emit(port->name, lsp::CompletionItemKind::Property, "port");
        }
    }

private:
    SourceLocation cursor;
    bool parameters;
};

} // namespace

std::unique_ptr<CompletionQuery> NamedPortCompletionQuery::create(lsp::Range replacementRange,
                                                                  SourceLocation cursor,
                                                                  bool parameters,
                                                                  bool followedByCall) {
    return std::make_unique<NamedPortCompletionQueryImpl>(std::move(replacementRange), cursor,
                                                          parameters, followedByCall);
}

} // namespace server::completions
