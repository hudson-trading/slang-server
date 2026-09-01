//------------------------------------------------------------------------------
// ActiveDesignContext.cpp
// Projects selected full-design instances onto a shallow compilation
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/ActiveDesignContext.h"

#include "ast/ServerCompilationAnalysis.h"
#include <algorithm>
#include <string_view>

#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/SmallMap.h"

namespace server {

using namespace slang;

InterfaceConnection resolveInterfaceConnection(const ast::InterfacePortSymbol& port) {
    InterfaceConnection result;
    SmallSet<const ast::InterfacePortSymbol*, 8> visited;
    const ast::Symbol* navigableEndpoint = nullptr;
    auto addPathSymbol = [&](const ast::Symbol* symbol) {
        if (symbol && symbol != &port && symbol->getSyntax() &&
            std::ranges::find(result.sourcePath, symbol) == result.sourcePath.end()) {
            result.sourcePath.push_back(symbol);
        }
    };

    auto* currentPort = &port;
    while (visited.insert(currentPort).second) {
        auto [ifaceConnection, expression] = currentPort->getConnectionAndExpr();
        auto [connection, _modport] = ifaceConnection;
        if (connection && (!result.resolvedEndpoint ||
                           result.resolvedEndpoint->kind == ast::SymbolKind::InterfacePort)) {
            result.resolvedEndpoint = connection;
        }

        const ast::InterfacePortSymbol* nextPort = nullptr;
        if (auto* arbitrary = expression ? expression->as_if<ast::ArbitrarySymbolExpression>()
                                         : nullptr) {
            for (const auto& element : arbitrary->hierRef.path) {
                auto* forwardedPort = element.symbol->as_if<ast::InterfacePortSymbol>();
                if (forwardedPort) {
                    addPathSymbol(forwardedPort);
                    if (!visited.contains(forwardedPort))
                        nextPort = forwardedPort;
                }
            }
            if (!navigableEndpoint || navigableEndpoint->kind == ast::SymbolKind::InterfacePort)
                navigableEndpoint = arbitrary->symbol;
        }
        else if (!navigableEndpoint || navigableEndpoint->kind == ast::SymbolKind::InterfacePort) {
            navigableEndpoint = connection;
        }

        if (!nextPort && connection)
            nextPort = connection->as_if<ast::InterfacePortSymbol>();
        if (!nextPort || visited.contains(nextPort))
            break;
        currentPort = nextPort;
    }
    addPathSymbol(navigableEndpoint);
    return result;
}

namespace {

class InterfacePortSyntaxIndex {
public:
    template<typename TPredicate>
    InterfacePortSyntaxIndex(const ast::DefinitionSymbol& definition,
                             TPredicate&& isInterfacePort) {
        auto* portList = definition.portList;
        if (!portList)
            return;

        if (portList->kind == syntax::SyntaxKind::AnsiPortList) {
            for (auto* port : portList->as<syntax::AnsiPortListSyntax>().ports) {
                if (auto* implicit = port->as_if<syntax::ImplicitAnsiPortSyntax>()) {
                    auto& declarator = *implicit->declarator;
                    auto name = declarator.name.valueText();
                    if (isInterfacePort(name))
                        m_ports.emplace(name, &declarator);
                }
            }
            return;
        }

        if (portList->kind != syntax::SyntaxKind::NonAnsiPortList)
            return;

        std::unordered_map<std::string_view, const syntax::SyntaxNode*> declarations;
        auto* definitionSyntax = definition.getSyntax();
        if (!definitionSyntax || !syntax::ModuleDeclarationSyntax::isKind(definitionSyntax->kind)) {
            return;
        }

        for (auto* member : definitionSyntax->as<syntax::ModuleDeclarationSyntax>().members) {
            if (auto* declaration = member->as_if<syntax::PortDeclarationSyntax>()) {
                for (auto* declarator : declaration->declarators)
                    declarations.emplace(declarator->name.valueText(), declarator);
            }
            else if (auto* declaration = member->as_if<syntax::DataDeclarationSyntax>()) {
                for (auto* declarator : declaration->declarators)
                    declarations.emplace(declarator->name.valueText(), declarator);
            }
        }

        for (auto* port : portList->as<syntax::NonAnsiPortListSyntax>().ports) {
            const syntax::PortExpressionSyntax* expression = nullptr;
            std::string_view portName;
            if (auto* implicit = port->as_if<syntax::ImplicitNonAnsiPortSyntax>()) {
                expression = implicit->expr;
            }
            else if (auto* explicitPort = port->as_if<syntax::ExplicitNonAnsiPortSyntax>()) {
                portName = explicitPort->name.valueText();
                expression = explicitPort->expr;
            }

            auto* reference = expression ? expression->as_if<syntax::PortReferenceSyntax>()
                                         : nullptr;
            if (!reference)
                continue;
            if (portName.empty())
                portName = reference->name.valueText();
            if (!isInterfacePort(portName))
                continue;

            auto declaration = declarations.find(reference->name.valueText());
            if (declaration != declarations.end())
                m_ports.emplace(portName, declaration->second);
        }
    }

    const syntax::SyntaxNode* get(std::string_view name) const {
        auto it = m_ports.find(name);
        return it == m_ports.end() ? nullptr : it->second;
    }

private:
    std::unordered_map<std::string_view, const syntax::SyntaxNode*> m_ports;
};

const ast::InstanceBodySymbol* getContainingInstanceBody(const ast::Symbol& symbol) {
    if (auto* body = symbol.as_if<ast::InstanceBodySymbol>())
        return body;

    for (auto* scope = symbol.getParentScope(); scope; scope = scope->asSymbol().getParentScope()) {
        if (auto* body = scope->asSymbol().as_if<ast::InstanceBodySymbol>())
            return body;
    }
    return nullptr;
}

const ast::InstanceSymbol* getFirstInterfaceInstance(const ast::Symbol* symbol) {
    if (auto* instance = symbol ? symbol->as_if<ast::InstanceSymbol>() : nullptr)
        return instance;
    if (auto* array = symbol ? symbol->as_if<ast::InstanceArraySymbol>() : nullptr) {
        for (auto* element : array->elements) {
            if (auto* instance = getFirstInterfaceInstance(element))
                return instance;
        }
    }
    return nullptr;
}

void copyParameterOverrides(ast::HierarchyOverrideNode& shallowOverride,
                            const ast::InstanceBodySymbol& designBody,
                            const ast::DefinitionSymbol& shallowDefinition) {
    for (auto* parameterBase : designBody.getParameters()) {
        if (parameterBase->isLocalParam())
            continue;

        const auto& symbol = parameterBase->symbol;
        auto declaration = std::ranges::find(shallowDefinition.parameters, symbol.name,
                                             &ast::DefinitionSymbol::ParameterDecl::name);
        if (declaration == shallowDefinition.parameters.end() || !declaration->hasSyntax)
            continue;

        const syntax::SyntaxNode* syntax =
            declaration->isTypeParam ? static_cast<const syntax::SyntaxNode*>(declaration->typeDecl)
                                     : declaration->valueDecl;
        if (!syntax)
            continue;

        if (auto* parameter = symbol.as_if<ast::ParameterSymbol>()) {
            const auto& value = parameter->getValue();
            if (!value.bad()) {
                shallowOverride.paramOverrides.insert_or_assign(
                    syntax, ast::HierarchyOverrideNode::ValueParamOverride{value});
            }
        }
        else if (auto* parameter = symbol.as_if<ast::TypeParameterSymbol>()) {
            const auto& type = parameter->targetType.getType();
            if (!type.isError()) {
                shallowOverride.paramOverrides.insert_or_assign(
                    syntax, ast::HierarchyOverrideNode::TypeParamOverride{
                                &type, parameter->targetType.getResolvedTypeSyntax()});
            }
        }
    }
}

} // namespace

void ActiveDesignContext::bindInstance(const ast::InstanceSymbol& instance) {
    DefinitionBinding binding{.instance = &instance};
    for (auto* symbol : instance.body.getPortList()) {
        if (auto* port = symbol->as_if<ast::InterfacePortSymbol>()) {
            binding.interfaceConnections.emplace(std::string(port->name),
                                                 resolveInterfaceConnection(*port));
        }
    }
    m_definitions.insert_or_assign(std::string(instance.getDefinition().name), std::move(binding));
}

void ActiveDesignContext::applyOverrides(ast::Compilation& shallowCompilation) const {
    std::unordered_map<std::string_view, const ast::DefinitionSymbol*> shallowDefinitions;
    for (auto* symbol : shallowCompilation.getDefinitions()) {
        if (auto* definition = symbol->as_if<ast::DefinitionSymbol>())
            shallowDefinitions.emplace(definition->name, definition);
    }

    for (const auto& [name, binding] : m_definitions) {
        auto shallowDefinitionIt = shallowDefinitions.find(name);
        if (shallowDefinitionIt == shallowDefinitions.end())
            continue;

        auto* shallowDefinition = shallowDefinitionIt->second;
        auto* shallowDefinitionSyntax = shallowDefinition->getSyntax();
        if (!shallowDefinitionSyntax)
            continue;

        InterfacePortSyntaxIndex shallowPorts(*shallowDefinition, [&](std::string_view portName) {
            return std::ranges::any_of(binding.interfaceConnections,
                                       [&](const auto& entry) { return entry.first == portName; });
        });
        auto& shallowOverride = shallowCompilation.getOrAddTopLevelHierarchyOverride(
            *shallowDefinitionSyntax);
        copyParameterOverrides(shallowOverride, binding.instance->body, *shallowDefinition);

        for (const auto& [portName, connection] : binding.interfaceConnections) {
            auto* shallowPortSyntax = shallowPorts.get(portName);
            if (!shallowPortSyntax)
                continue;

            auto* designInterfaceInstance = getFirstInterfaceInstance(connection.resolvedEndpoint);
            if (!designInterfaceInstance)
                continue;

            auto shallowInterfaceIt = shallowDefinitions.find(
                designInterfaceInstance->getDefinition().name);
            if (shallowInterfaceIt == shallowDefinitions.end())
                continue;

            copyParameterOverrides(shallowOverride.childNodes[*shallowPortSyntax],
                                   designInterfaceInstance->body, *shallowInterfaceIt->second);
        }
    }
}

const ast::Symbol* ActiveDesignContext::getDesignSymbol(const ast::Symbol& shallowSymbol) const {
    if (auto* definition = shallowSymbol.as_if<ast::DefinitionSymbol>()) {
        auto it = m_definitions.find(std::string(definition->name));
        return it == m_definitions.end() ? nullptr : &it->second.instance->getDefinition();
    }

    auto* body = getContainingInstanceBody(shallowSymbol);
    if (!body || !body->parentInstance)
        return nullptr;

    auto binding = m_definitions.find(std::string(body->getDefinition().name));
    if (binding == m_definitions.end())
        return nullptr;
    if (&shallowSymbol == body)
        return &binding->second.instance->body;

    auto shallowInstancePath = body->parentInstance->getHierarchicalPath();
    auto symbolPath = shallowSymbol.getHierarchicalPath();
    if (!std::string_view(symbolPath).starts_with(shallowInstancePath))
        return nullptr;

    auto designPath = binding->second.instance->getHierarchicalPath();
    designPath.append(std::string_view(symbolPath).substr(shallowInstancePath.size()));

    auto& compilation = m_analysis->compilation;
    ast::LookupResult result;
    ast::ASTContext context(compilation.getRoot(), ast::LookupLocation::max);
    ast::Lookup::name(compilation.parseName(designPath), context,
                      ast::LookupFlags::AllowUnnamedGenerate, result);
    return result.found;
}

const InterfaceConnection* ActiveDesignContext::getInterfaceConnection(
    const ast::InterfacePortSymbol& port) const {
    auto* body = getContainingInstanceBody(port);
    if (!body)
        return nullptr;

    auto binding = m_definitions.find(std::string(body->getDefinition().name));
    if (binding == m_definitions.end())
        return nullptr;

    auto connection = binding->second.interfaceConnections.find(std::string(port.name));
    return connection == binding->second.interfaceConnections.end() ? nullptr : &connection->second;
}

} // namespace server
