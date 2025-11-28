// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/InlayHintCollector.h"

#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Logging.h"

#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"

using namespace slang;
using namespace slang::syntax;

namespace server {
using namespace slang;

InlayHintCollector::InlayHintCollector(const ShallowAnalysis& analysis, lsp::Range range,
                                       const Config::InlayHints& config) :
    m_analysis(analysis), m_range(range), m_portTypes(config.portTypes.value()),
    m_orderedInstanceNames(config.orderedInstanceNames.value()),
    m_wildcardNames(config.wildcardNames.value()), m_funcArgNames(config.funcArgNames.value()),
    m_macroArgNames(config.macroArgNames.value()) {
}

void InlayHintCollector::handle(const HierarchyInstantiationSyntax& syntax) {
    auto module = m_analysis.getSymbolAtToken(&syntax.type);
    if (!module) {
        return;
    }

    if (module->kind != ast::SymbolKind::Definition) {
        // Primitive symbols- could maybe support this, but these are pretty rare nowadays
        return;
    }

    auto& def = module->as<ast::DefinitionSymbol>();

    // Do params
    if (m_orderedInstanceNames && syntax.parameters) {
        size_t paramIndex = 0;
        for (auto paramSyntax : syntax.parameters->parameters) {
            if (paramSyntax->kind == SyntaxKind::OrderedParamAssignment) {
                result.push_back(lsp::InlayHint{
                    .position = toPosition(paramSyntax->getFirstToken().location(),
                                           m_analysis.m_sourceManager),
                    .label = fmt::format("{}:", def.parameters[paramIndex++].name),
                    .kind = lsp::InlayHintKind::Parameter,
                    .paddingRight = true,
                });
            }
        }
    }

    // Do ports on each instance
    for (auto instanceSyntax : syntax.instances) {
        if (instanceSyntax->kind != SyntaxKind::HierarchicalInstance) {
            continue;
        }

        auto& hierInstSyntax = instanceSyntax->as<HierarchicalInstanceSyntax>();
        auto inst = m_analysis.getSymbolAtToken(&hierInstSyntax.decl->name);
        if (!inst) {
            continue;
        }
        if (inst->kind != ast::SymbolKind::Instance) {
            // TODO: handle instance arrays
            continue;
        }

        auto& body = inst->as<ast::InstanceSymbol>().body;

        // Collect named port hints for alignment
        std::vector<lsp::InlayHint> namedPortHints;
        size_t maxLabelLen = 0;
        size_t portIndex = 0;
        size_t lastPortLine = -1;
        auto ports = body.getPortList();
        for (auto portSyntax : hierInstSyntax.connections) {
            switch (portSyntax->kind) {
                case SyntaxKind::OrderedPortConnection: {
                    if (!m_orderedInstanceNames) {
                        continue;
                    }
                    result.push_back(lsp::InlayHint{
                        .position = toPosition(portSyntax->getFirstToken().location(),
                                               m_analysis.m_sourceManager),
                        .label = fmt::format("{}:", ports[portIndex++]->name),
                        .kind = lsp::InlayHintKind::Parameter,
                        .paddingRight = true,
                    });
                } break;
                case SyntaxKind::NamedPortConnection: {
                    if (!m_portTypes) {
                        continue;
                    }
                    auto& connection = portSyntax->as<NamedPortConnectionSyntax>();
                    auto port = body.findPort(connection.name.valueText());
                    if (!port) {
                        continue;
                    }

                    std::string label;
                    if (port->kind == ast::SymbolKind::Port) {
                        auto& portSym = port->as<ast::PortSymbol>();
                        label = fmt::format("{} {}", portString(portSym.direction),
                                            portSym.getType().toString());
                    }
                    else if (port->kind == ast::SymbolKind::InterfacePort) {
                        auto& portSym = port->as<ast::InterfacePortSymbol>();
                        if (!portSym.interfaceDef) {
                            WARN("invalid modport: {}", portSym.name);
                            continue;
                        }
                        label = fmt::format("{}.{}", portSym.interfaceDef->name, portSym.modport);
                    }
                    else {
                        WARN("Unknown port sym: {}", toString(port->kind));
                        continue;
                    }

                    // Track max lengths to preserve alignment
                    maxLabelLen = std::max(maxLabelLen, label.size());
                    auto pos = toPosition(connection.name.location() +
                                              connection.name.rawText().size(),
                                          m_analysis.m_sourceManager);
                    namedPortHints.push_back(lsp::InlayHint{
                        .position = pos,
                        .label = label,
                        .kind = lsp::InlayHintKind::Type,
                        .paddingLeft = true,
                        .paddingRight = true,
                    });

                    // Don't show ports if types if theyr'e all on the same line
                    if (pos.line == lastPortLine) {
                        return;
                    }
                    lastPortLine = pos.line;
                } break;
                case SyntaxKind::WildcardPortConnection: {
                    if (!m_wildcardNames) {
                        continue;
                    }
                    std::string label;
                    std::string replaceText = "";
                    size_t indent = m_analysis.m_sourceManager.getColumnNumber(
                                        syntax.type.location()) +
                                    FORMATTING_INDENT - 1;

                    auto getLine = [&](const auto& loc) {
                        return m_analysis.m_sourceManager.getLineNumber(loc);
                    };

                    if (getLine(instanceSyntax->openParen.location()) ==
                        getLine(portSyntax->sourceRange().start())) {
                        // The lsp indent params are only specified in individual requests- so we
                        // just have to guess for now. This config will go in formatting.json later.
                        replaceText += "\n" + std::string(indent, ' ');
                    }

                    for (auto portSym : ports) {
                        label += portSym->name;
                        replaceText += fmt::format(".{}", portSym->name);

                        if (portSym != ports.back()) {
                            label += ", ";
                            replaceText += ",\n" + std::string(indent, ' ');
                        }
                    }
                    if (getLine(instanceSyntax->closeParen.location()) ==
                        getLine(portSyntax->sourceRange().end())) {
                        replaceText += "\n" + std::string(indent - FORMATTING_INDENT, ' ');
                    }
                    SLANG_ASSERT(body.getSyntax());
                    auto portSyntaxList =
                        body.getSyntax()->as<ModuleDeclarationSyntax>().header->ports;

                    result.push_back(lsp::InlayHint{
                        .position = toPosition(portSyntax->sourceRange().end(),
                                               m_analysis.m_sourceManager),
                        .label = label,
                        .kind = lsp::InlayHintKind::Type,
                        .textEdits =
                            std::vector<lsp::TextEdit>{
                                lsp::TextEdit{
                                    .range = toRange(portSyntax->sourceRange(),
                                                     m_analysis.m_sourceManager),
                                    .newText = replaceText,
                                },
                            },
                        .tooltip = portSyntaxList ? std::optional<lsp::MarkupContent>(
                                                        svCodeBlock(*portSyntaxList))
                                                  : std::nullopt,
                        .paddingLeft = true,
                        .paddingRight = true,
                    });

                } break;

                default:
                    WARN("Inlay Hints: Unknown port symbol kind: {}", toString(portSyntax->kind));
                    break;
            }
        }

        // align named port hints
        for (auto& hint : namedPortHints) {
            auto labelStr = rfl::get<std::string>(hint.label);
            hint.label = labelStr + std::string(maxLabelLen - labelStr.size(), ' ');

            result.push_back(std::move(hint));
        }
    }
}

void InlayHintCollector::handle(const MacroUsageSyntax& syntax) {
    if (m_macroArgNames <= 0 || !syntax.args) {
        return;
    }

    // TODO: maybe we should also use the index for these?
    auto defInfo = m_analysis.macros.find(syntax.directive.valueText().substr(1));
    if (defInfo == m_analysis.macros.end()) {
        return;
    }
    if (!defInfo->second->formalArguments) {
        return;
    }
    // Check if we should show hints based on arg count
    if (syntax.args->args.size() < static_cast<size_t>(m_macroArgNames)) {
        return;
    }

    size_t argIndex = 0;
    for (auto param : syntax.args->args) {
        result.push_back(lsp::InlayHint{
            .position = toPosition(param->getFirstToken().location(), m_analysis.m_sourceManager),
            .label = fmt::format(
                "{}:", defInfo->second->formalArguments->args[argIndex++]->name.rawText()),
            .kind = lsp::InlayHintKind::Parameter,
            .paddingRight = true,
        });
    }
}

void InlayHintCollector::handle(const InvocationExpressionSyntax& syntax) {
    if (m_funcArgNames <= 0 || !syntax.arguments)
        return;
    auto maybeSub = m_analysis.getSymbolAtToken(syntax.left->getLastTokenPtr());
    if (!maybeSub || maybeSub->kind != ast::SymbolKind::Subroutine) {
        // TODO: Implement for system names
        return;
    }

    // Check if we should show hints based on arg count
    if (syntax.arguments->parameters.size() < static_cast<size_t>(m_funcArgNames)) {
        return;
    }

    auto argNames = maybeSub->as<ast::SubroutineSymbol>().getArguments();
    size_t argIndex = 0;
    for (auto arg : syntax.arguments->parameters) {
        if (arg->kind != slang::syntax::SyntaxKind::OrderedArgument) {
            break;
        }
        auto name = argNames[argIndex++]->name;
        if (name.empty()) {
            continue;
        }
        result.push_back(lsp::InlayHint{
            .position = toPosition(arg->getFirstToken().location(), m_analysis.m_sourceManager),
            .label = fmt::format("{}:", name),
            .kind = lsp::InlayHintKind::Parameter,
            .paddingRight = true,
        });
    }
}

void InlayHintCollector::collectHints() {

    auto slangStart = m_analysis.m_sourceManager.getSourceLocation(m_analysis.m_buffer,
                                                                   m_range.start.line,
                                                                   m_range.start.character);

    auto slangEnd = m_analysis.m_sourceManager.getSourceLocation(m_analysis.m_buffer,
                                                                 m_range.end.line,
                                                                 m_range.end.character);
    if (!slangStart || !slangEnd) {
        ERROR("Invalid range for inlay hints");
        return;
    }
    auto start = m_analysis.syntaxes.collectedHints.lower_bound(slangStart->offset());
    auto end = m_analysis.syntaxes.collectedHints.upper_bound(slangEnd->offset());

    // Expand start backward to include nodes that begin before the range but extend into it
    while (start != m_analysis.syntaxes.collectedHints.begin()) {
        auto prev = std::prev(start);
        if (prev->second->sourceRange().end().offset() <= slangStart->offset()) {
            break;
        }
        start = prev;
    }

    for (auto it = start; it != end; ++it) {
        switch (it->second->kind) {
            case syntax::SyntaxKind::HierarchyInstantiation:
                handle(it->second->as<HierarchyInstantiationSyntax>());
                break;
            case syntax::SyntaxKind::MacroUsage:
                handle(it->second->as<MacroUsageSyntax>());
                break;
            case syntax::SyntaxKind::InvocationExpression:
                handle(it->second->as<InvocationExpressionSyntax>());
                break;
            default:
                break;
        }
    }
}

} // namespace server
