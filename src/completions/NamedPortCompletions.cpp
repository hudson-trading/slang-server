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
#include "slang/ast/Scope.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/syntax/AllSyntax.h"

namespace server::completions {
using namespace slang;

namespace {

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
        const syntax::HierarchicalInstanceSyntax* instanceSyntax = nullptr;
        auto* syntax = findInstantiation(context, instanceSyntax);
        if (!syntax)
            return;

        auto* module = context.analysis->getSymbolAtToken(&syntax->type);
        if (!module || module->kind != ast::SymbolKind::Definition)
            return;

        auto& def = module->as<ast::DefinitionSymbol>();

        std::unordered_set<std::string_view> taken;
        collectConnected(*syntax, instanceSyntax, taken);

        auto visibleInScope = [&](std::string_view name) {
            return context.scope && context.scope->lookupName(name) != nullptr;
        };

        auto emit = [&](std::string_view name, lsp::CompletionItemKind itemKind, std::string detail,
                        bool allowImplicit = false) {
            if (name.empty() || taken.contains(name))
                return;
            lsp::CompletionItem item{.label = std::string(name), .kind = itemKind};
            if (!detail.empty())
                item.detail = std::move(detail);
            const bool implicit = allowImplicit && visibleInScope(name);
            if (implicit) {
                item.insertText = std::string(name);
                item.insertTextFormat = lsp::InsertTextFormat::PlainText;
            }
            else if (!followedByCall) {
                SnippetString snippet;
                snippet.appendText(name).appendText("(").appendTabstop().appendText(")");
                item.insertText = snippet.getValue();
                item.insertTextFormat = lsp::InsertTextFormat::Snippet;
            }
            setCompletionEdit(item);
            results.push_back(std::move(item));
        };

        auto* inst = findInstanceSymbol(context, *syntax, instanceSyntax);

        if (parameters) {
            if (inst) {
                for (auto* param : inst->body.getParameters()) {
                    if (!param || param->isLocalParam())
                        continue;
                    auto& sym = param->symbol;
                    if (auto* value = sym.as_if<ast::ParameterSymbol>())
                        emit(value->name, lsp::CompletionItemKind::TypeParameter,
                             value->getType().toString());
                    else if (auto* typeParam = sym.as_if<ast::TypeParameterSymbol>())
                        emit(typeParam->name, lsp::CompletionItemKind::TypeParameter, "type");
                }
                return;
            }
            for (auto& param : def.parameters) {
                if (param.isLocalParam)
                    continue;
                emit(param.name, lsp::CompletionItemKind::TypeParameter,
                     param.isTypeParam ? "type" : std::string());
            }
            return;
        }

        if (!inst)
            return;

        for (auto* port : inst->body.getPortList()) {
            if (!port)
                continue;
            if (auto* value = port->as_if<ast::PortSymbol>())
                emit(value->name, lsp::CompletionItemKind::Property, value->getType().toString(),
                     true);
            else if (auto* iface = port->as_if<ast::InterfacePortSymbol>())
                emit(iface->name, lsp::CompletionItemKind::Property,
                     iface->interfaceDef ? std::string(iface->interfaceDef->name) : std::string(),
                     true);
            else
                emit(port->name, lsp::CompletionItemKind::Property, std::string(), true);
        }
    }

private:
    const syntax::HierarchyInstantiationSyntax* findInstantiation(
        const CompletionContext& context,
        const syntax::HierarchicalInstanceSyntax*& instanceSyntax) const {
        auto* node = context.analysis->syntaxes.getSyntaxAt(cursor);
        if (!node) {
            auto* previous = context.analysis->syntaxes.getTokenBefore(cursor);
            node = context.analysis->syntaxes.getTokenParent(previous);
            if (!node)
                return nullptr;
        }

        for (; node; node = node->parent) {
            if (auto* instance = node->as_if<syntax::HierarchicalInstanceSyntax>())
                instanceSyntax = instance;
            if (auto* hierarchy = node->as_if<syntax::HierarchyInstantiationSyntax>())
                return hierarchy;
        }
        return nullptr;
    }

    void collectConnected(const syntax::HierarchyInstantiationSyntax& syntax,
                          const syntax::HierarchicalInstanceSyntax* instanceSyntax,
                          std::unordered_set<std::string_view>& taken) const {
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

        if (!instanceSyntax)
            return;

        for (auto* connection : instanceSyntax->connections) {
            if (auto* named = connection->as_if<syntax::NamedPortConnectionSyntax>())
                record(named->name, named->sourceRange());
        }
    }

    const ast::InstanceSymbol* findInstanceSymbol(
        const CompletionContext& context, const syntax::HierarchyInstantiationSyntax& syntax,
        const syntax::HierarchicalInstanceSyntax* instanceSyntax) const {
        auto resolve = [&](const syntax::HierarchicalInstanceSyntax* candidate) {
            if (!candidate || !candidate->decl)
                return static_cast<const ast::InstanceSymbol*>(nullptr);
            auto* sym = context.analysis->getSymbolAtToken(&candidate->decl->name);
            if (sym && sym->kind == ast::SymbolKind::Instance)
                return &sym->as<ast::InstanceSymbol>();
            return static_cast<const ast::InstanceSymbol*>(nullptr);
        };

        if (auto* own = resolve(instanceSyntax))
            return own;

        for (auto* candidate : syntax.instances) {
            if (auto* any = resolve(candidate))
                return any;
        }
        return nullptr;
    }

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
