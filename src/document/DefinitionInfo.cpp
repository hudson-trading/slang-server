//------------------------------------------------------------------------------
// DefinitionInfo.cpp
// Hover rendering for resolved definitions.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "document/DefinitionInfo.h"

#include "SystemTaskDocs.h"
#include "document/ShallowAnalysis.h"
#include "lsp/URI.h"
#include "util/Converters.h"
#include "util/Formatting.h"
#include "util/Markdown.h"
#include "util/SlangExtensions.h"
#include <algorithm>
#include <filesystem>
#include <span>
#include <type_traits>

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/Expression.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/Type.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/OS.h"
#include "slang/util/SmallMap.h"
#include "slang/util/SmallVector.h"

namespace server {
using namespace slang;

namespace {

const ast::InstanceSymbol* getFirstInstanceElement(const ast::InstanceArraySymbol& array) {
    auto* current = &array;
    while (current && !current->elements.empty()) {
        auto* element = current->elements.front();
        if (auto* instance = element->as_if<ast::InstanceSymbol>())
            return instance;

        current = element->as_if<ast::InstanceArraySymbol>();
    }

    return nullptr;
}

std::string getInstanceArrayShape(const ast::InstanceArraySymbol& array) {
    std::string shape;
    auto* current = &array;
    while (current) {
        shape += fmt::format("[{}]", current->range.fullWidth());
        if (current->elements.empty())
            break;

        current = current->elements.front()->as_if<ast::InstanceArraySymbol>();
    }

    return shape;
}

std::string_view getPortDirectionHeader(ast::ArgumentDirection direction) {
    switch (direction) {
        case ast::ArgumentDirection::In:
            return "Input";
        case ast::ArgumentDirection::Out:
            return "Output";
        case ast::ArgumentDirection::InOut:
            return "InOut";
        case ast::ArgumentDirection::Ref:
            return "Ref";
        default:
            return {};
    }
}

void renderSymbolHeaderName(markup::Paragraph& infoPg, const ast::Symbol& symbol,
                            std::string_view kindOverride = {}) {
    if (!kindOverride.empty()) {
        infoPg.appendBold(kindOverride).appendCode(symbol.name);
        return;
    }

    if (auto* parameter = symbol.as_if<ast::ParameterSymbol>();
        parameter && parameter->isFromGenvar()) {
        infoPg.appendBold("Genvar").appendCode(symbol.name);
        return;
    }

    if (auto* definition = symbol.as_if<ast::DefinitionSymbol>()) {
        infoPg.appendBold(toString(definition->definitionKind)).appendCode(symbol.name);
        return;
    }

    if (auto* instance = symbol.as_if<ast::InstanceSymbol>()) {
        infoPg.appendCode(instance->getDefinition().name).appendText(" ").appendCode(symbol.name);
        return;
    }

    if (auto* array = symbol.as_if<ast::InstanceArraySymbol>()) {
        if (auto* instance = getFirstInstanceElement(*array)) {
            infoPg
                .appendCode(fmt::format("{}{}", instance->getDefinition().name,
                                        getInstanceArrayShape(*array)))
                .appendText(" ")
                .appendCode(symbol.name);
            return;
        }
    }

    // Better handling for ports to show direction
    if (auto* value = symbol.as_if<ast::ValueSymbol>()) {
        if (auto* port = value->getFirstPortBackref()) {
            infoPg
                .appendBold(fmt::format("{} {}", getPortDirectionHeader(port->port->direction),
                                        toString(symbol.kind)))
                .appendCode(symbol.name);
            return;
        }
    }

    infoPg.appendBold(toString(symbol.kind)).appendCode(symbol.name);
}

bool appendSourceLink(markup::Paragraph& paragraph, SourceLocation location,
                      const SourceManager& sourceManager, std::string_view label = {});

struct IncompleteSubtype {
    std::string type;
    SourceLocation location;
};

bool collectIncompleteSubtypes(const ast::Type& type, std::vector<IncompleteSubtype>& subtypes,
                               SmallSet<const ast::Type*, 8>& seenTypes,
                               std::vector<const ast::Type*>& activeTypes) {
    if (!type.isError())
        return false;

    const auto& recoveredType = unwrapErrorType(type);
    if ((!recoveredType.isStruct() && !recoveredType.isUnion()) ||
        std::ranges::find(activeTypes, &recoveredType) != activeTypes.end()) {
        return false;
    }

    bool found = false;
    activeTypes.push_back(&recoveredType);
    for (const auto& member : recoveredType.as<ast::Scope>().members()) {
        auto* field = member.as_if<ast::FieldSymbol>();
        if (!field || !field->getType().isError())
            continue;

        if (!collectIncompleteSubtypes(field->getType(), subtypes, seenTypes, activeTypes)) {
            std::string typeText = "Incomplete type";
            SourceLocation typeLocation = field->location;
            if (auto* fieldSyntax = field->getSyntax()) {
                if (auto* declarator = fieldSyntax->as_if<syntax::DeclaratorSyntax>()) {
                    if (auto* memberSyntax =
                            declarator->parent->as_if<syntax::StructUnionMemberSyntax>()) {
                        typeText = detailFormat(*memberSyntax->type);
                        typeLocation = memberSyntax->type->getFirstToken().location();
                    }
                }
            }
            if (field->getType().kind == ast::SymbolKind::TypeAlias)
                typeLocation = field->getType().location;
            if (seenTypes.insert(&field->getType()).second)
                subtypes.emplace_back(std::move(typeText), typeLocation);
        }
        found = true;
    }
    activeTypes.pop_back();
    return found;
}

void renderIncompleteSubtypes(markup::Paragraph& infoPg, const ast::Type& type,
                              const SourceManager& sourceManager) {
    if (!type.isError())
        return;

    std::vector<IncompleteSubtype> subtypes;
    SmallSet<const ast::Type*, 8> seenTypes;
    std::vector<const ast::Type*> activeTypes;
    collectIncompleteSubtypes(type, subtypes, seenTypes, activeTypes);
    if (subtypes.empty())
        return;

    infoPg.appendText("Incomplete subtypes: ");
    for (size_t i = 0; i < subtypes.size(); i++) {
        if (i > 0)
            infoPg.appendText(", ");
        appendSourceLink(infoPg, subtypes[i].location, sourceManager, subtypes[i].type);
    }
    infoPg.newLine();
}

void renderSymbolType(markup::Paragraph& infoPg, const ast::Symbol& symbol,
                      const SourceManager& sourceManager) {
    if (ast::ValueSymbol::isKind(symbol.kind) && symbol.kind != ast::SymbolKind::EnumValue) {
        const auto& valSym = symbol.as<ast::ValueSymbol>();
        const auto& type = valSym.getType();
        infoPg.appendText("Type: ");
        if (!appendSourceLink(infoPg, type.location, sourceManager,
                              getTypeString(type, TypeStringMode::Friendly)))
            infoPg.appendText(getTypeString(type, TypeStringMode::FriendlyMarkdownQuoted));
        infoPg.newLine();
        if (!ast::ParameterSymbol::isKind(symbol.kind) && !type.isError() &&
            type.getBitWidth() > 1) {
            infoPg.appendText("Width: ")
                .appendCode(fmt::format("{}", type.getBitWidth()))
                .newLine();
        }
    }
}

struct DriverGroup {
    const ast::Symbol* containingSymbol;
    std::vector<const analysis::ValueDriver*> drivers;
    SmallVector<DefinitionInfo::SyntaxTarget, 4> displayTargets;
};

bool isPortDriver(const analysis::ValueDriver& driver) {
    return driver.isUnidirectionalPort() ||
           driver.flags.has(analysis::DriverFlags::ViaIndirectPort);
}

std::string getDriverInstanceDescription(const DriverGroup& group) {
    const ast::InstanceSymbol* instance = nullptr;
    if (group.containingSymbol->kind == ast::SymbolKind::Instance) {
        instance = &group.containingSymbol->as<ast::InstanceSymbol>();
    }
    else if (group.containingSymbol->kind == ast::SymbolKind::InstanceBody) {
        instance = group.containingSymbol->as<ast::InstanceBodySymbol>().parentInstance;
    }

    if (!instance)
        return {};

    return fmt::format("{} {}", instance->getDefinition().name, instance->getArrayName());
}

std::string getDriverDescription(const analysis::ValueDriver& driver) {
    if (isPortDriver(driver))
        return "via port";

    if (driver.flags.has(analysis::DriverFlags::Initializer))
        return "by initializer";

    if (driver.source == analysis::DriverSource::Subroutine)
        return "by subroutine";

    if (driver.source != analysis::DriverSource::Other) {
        return fmt::format("by {}", ast::SemanticFacts::getProcedureKindStr(
                                        static_cast<ast::ProceduralBlockKind>(driver.source)));
    }

    if (driver.kind == analysis::DriverKind::Continuous)
        return "by continuous assignment";

    return "by procedural assignment";
}

std::string getDriverGroupHeader(const DriverGroup& group) {
    std::vector<std::string> descriptions;
    const auto instanceDescription = getDriverInstanceDescription(group);
    for (const auto* driver : group.drivers) {
        auto description = getDriverDescription(*driver);
        if (isPortDriver(*driver) && !instanceDescription.empty())
            description += fmt::format(" from `{}`", instanceDescription);
        if (std::ranges::find(descriptions, description) == descriptions.end())
            descriptions.push_back(std::move(description));
    }

    std::string result = "Driven ";
    for (size_t i = 0; i < descriptions.size(); i++) {
        if (i != 0)
            result += " and ";
        result += descriptions[i];
    }
    return result;
}

const syntax::SyntaxNode* findSyntaxAt(const syntax::SyntaxNode& root, SourceLocation location) {
    if (!root.sourceRange().contains(location))
        return nullptr;

    for (size_t i = 0; i < root.getChildCount(); i++) {
        if (const auto* child = root.childNode(i)) {
            if (const auto* result = findSyntaxAt(*child, location))
                return result;
        }
    }
    return &root;
}

const syntax::SyntaxNode* getDriverSourceNode(const analysis::ValueDriver& driver) {
    if (!driver.flags.has(analysis::DriverFlags::Initializer) &&
        driver.containingSymbol->kind == ast::SymbolKind::ProceduralBlock) {
        return driver.containingSymbol->getSyntax();
    }

    if (driver.flags.has(analysis::DriverFlags::Initializer)) {
        if (auto* syntax = driver.getSymbol().getSyntax())
            return syntax;
    }

    const auto* expressionSyntax = driver.path.fullExpr ? driver.path.fullExpr->syntax : nullptr;
    if (expressionSyntax)
        return expressionSyntax;

    const auto* containingSyntax = driver.containingSymbol->getSyntax();
    const auto range = driver.getSourceRange();
    if (containingSyntax && range != SourceRange::NoLocation) {
        if (const auto* syntax = findSyntaxAt(*containingSyntax, range.start()))
            return syntax;
    }
    return containingSyntax;
}

const syntax::SyntaxNode* getDriverDisplayNode(const analysis::ValueDriver& driver) {
    const auto* node = getDriverSourceNode(driver);
    if (!node)
        return nullptr;

    if (isPortDriver(driver)) {
        for (auto* current = node; current; current = current->parent) {
            switch (current->kind) {
                case syntax::SyntaxKind::ExplicitAnsiPort:
                case syntax::SyntaxKind::ImplicitAnsiPort:
                case syntax::SyntaxKind::NamedPortConnection:
                case syntax::SyntaxKind::OrderedPortConnection:
                case syntax::SyntaxKind::PortDeclaration:
                    return current;
                default:
                    break;
            }
        }
    }
    else if (driver.kind == analysis::DriverKind::Continuous) {
        for (auto* current = node; current; current = current->parent) {
            if (current->kind == syntax::SyntaxKind::ContinuousAssign)
                return current;
        }
    }

    for (auto* current = node; current; current = current->parent) {
        switch (current->kind) {
            case syntax::SyntaxKind::ExpressionStatement:
            case syntax::SyntaxKind::ProceduralAssignStatement:
            case syntax::SyntaxKind::ProceduralDeassignStatement:
            case syntax::SyntaxKind::ProceduralForceStatement:
            case syntax::SyntaxKind::ProceduralReleaseStatement:
                return current;
            default:
                break;
        }
    }

    return &selectDisplayNode(*node);
}

bool appendSourceLink(markup::Paragraph& paragraph, SourceLocation location,
                      const SourceManager& sourceManager, std::string_view label) {
    const auto originalLoc = sourceManager.getFullyOriginalLoc(location);
    if (!sourceManager.isFileLoc(originalLoc))
        return false;

    const auto& path = sourceManager.getFullPath(originalLoc.buffer());
    if (path.empty())
        return false;

    const auto line = sourceManager.getRawLineNumber(originalLoc);
    const auto column = sourceManager.getColumnNumber(originalLoc);
    const auto linkLabel = label.empty()
                               ? fmt::format("{}:{}:{}", path.filename().string(), line, column)
                               : fmt::format("`{}`", label);
    const auto target = fmt::format("{}#L{},{}", URI::fromFile(path), line, column);
    if (label.empty())
        paragraph.appendText(" at ");
    paragraph.appendText(fmt::format("[{}](<{}>)", linkLabel, target));
    return true;
}

void appendSyntaxTargets(markup::Document& doc,
                         std::span<const DefinitionInfo::SyntaxTarget> targets,
                         const SourceManager& sm,
                         Config::HoverConfig::DocCommentFormat docCommentFormat) {
    SmallVector<DefinitionInfo::SyntaxTarget, 4> renderedTargets;
    const bool rawDocComments = docCommentFormat == Config::HoverConfig::DocCommentFormat::raw;
    for (const auto& target : targets) {
        if (std::ranges::find(renderedTargets, target) != renderedTargets.end())
            continue;

        renderedTargets.push_back(target);
        const auto& displayNode = selectDisplayNode(*target.node);
        if (!rawDocComments) {
            const auto docComments = getDocCommentForHover(displayNode, docCommentFormat);
            if (!docComments.empty())
                doc.addParagraph().appendText(docComments).newLine();
        }
        target.renderCode(doc.addParagraph(), sm, rawDocComments);
    }
}

void renderDrivers(markup::Document& doc, const ast::Symbol& symbol, ShallowAnalysis& analysis,
                   const SourceManager& sourceManager, bool renderInputPortDriver,
                   bool followPortDriver,
                   SmallVector<const syntax::SyntaxNode*, 4>* renderedGroupSyntaxes = nullptr) {
    if (!ast::ValueSymbol::isKind(symbol.kind) || symbol.kind == ast::SymbolKind::EnumValue)
        return;

    SmallVector<const syntax::SyntaxNode*, 4> rootRenderedGroupSyntaxes;
    if (!renderedGroupSyntaxes)
        renderedGroupSyntaxes = &rootRenderedGroupSyntaxes;

    std::vector<DriverGroup> groups;
    SmallVector<const ast::Symbol*, 2> connectedPortSymbols;
    for (const auto* driver : analysis.getDrivers(symbol.as<ast::ValueSymbol>())) {
        if (!driver || (driver->isInputPort() && !renderInputPortDriver))
            continue;

        const ast::Symbol* containingSymbol = driver->containingSymbol;
        const auto* containingSyntax = containingSymbol->getSyntax();
        auto groupIt = std::ranges::find_if(groups, [&](const auto& group) {
            return group.containingSymbol == containingSymbol ||
                   (containingSyntax && group.containingSymbol->getSyntax() == containingSyntax);
        });
        if (groupIt == groups.end()) {
            groups.push_back({.containingSymbol = containingSymbol});
            groupIt = std::prev(groups.end());
        }

        auto& group = *groupIt;
        group.drivers.push_back(driver);
        if (const auto* node = getDriverDisplayNode(*driver)) {
            auto target = DefinitionInfo::SyntaxTarget::fromNode(node, node->getFirstToken(),
                                                                 sourceManager);
            if (std::ranges::find(group.displayTargets, target) == group.displayTargets.end())
                group.displayTargets.push_back(std::move(target));
        }

        if (!followPortDriver || !driver->flags.has(analysis::DriverFlags::OutputPort))
            continue;

        auto* instance = driver->containingSymbol->as_if<ast::InstanceSymbol>();
        if (!instance)
            continue;

        for (auto* connection : instance->getPortConnections()) {
            auto* expression = connection->getExpression();
            if (!expression)
                continue;
            if (auto* assignment = expression->as_if<ast::AssignmentExpression>())
                expression = &assignment->left();
            if (expression->sourceRange != driver->getSourceRange())
                continue;

            auto addInternalSymbol = [&](const ast::PortSymbol& port) {
                if (port.internalSymbol &&
                    std::ranges::find(connectedPortSymbols, port.internalSymbol) ==
                        connectedPortSymbols.end()) {
                    connectedPortSymbols.push_back(port.internalSymbol);
                }
            };
            if (auto* port = connection->port.as_if<ast::PortSymbol>()) {
                addInternalSymbol(*port);
            }
            else if (auto* multiPort = connection->port.as_if<ast::MultiPortSymbol>()) {
                for (auto* port : multiPort->ports)
                    addInternalSymbol(*port);
            }
        }
    }

    for (const auto& group : groups) {
        const auto* containingSyntax = group.containingSymbol->getSyntax();
        if (containingSyntax) {
            if (std::ranges::find(*renderedGroupSyntaxes, containingSyntax) !=
                renderedGroupSyntaxes->end()) {
                continue;
            }
            renderedGroupSyntaxes->push_back(containingSyntax);
        }

        markup::Paragraph paragraph;
        paragraph.appendText(getDriverGroupHeader(group));

        auto location = group.containingSymbol->location;
        if (!group.displayTargets.empty()) {
            const auto& target = group.displayTargets.front();
            location = target.macroUsageRange == SourceRange::NoLocation
                           ? target.nameToken.location()
                           : target.macroUsageRange.start();
        }
        appendSourceLink(paragraph, location, sourceManager);
        if (group.displayTargets.empty()) {
            doc.addParagraph(std::move(paragraph));
            continue;
        }

        for (const auto& target : group.displayTargets) {
            target.renderCode(paragraph, sourceManager, true);
            doc.addParagraph(std::move(paragraph));
            paragraph = {};
        }
    }

    for (const auto* connectedSymbol : connectedPortSymbols)
        renderDrivers(doc, *connectedSymbol, analysis, sourceManager, false, false,
                      renderedGroupSyntaxes);
}

void renderSymbolValue(markup::Paragraph& infoPg, const ast::Symbol& symbol,
                       const SourceManager& sourceManager) {
    auto appendTypeParameterValue = [&](const ast::Type& type) {
        if (!type.isError()) {
            infoPg.appendText("Value: ")
                .appendText(getTypeString(type, TypeStringMode::FriendlyMarkdownQuoted))
                .newLine();
        }
    };

    if (auto* typeParam = symbol.as_if<ast::TypeParameterSymbol>()) {
        appendTypeParameterValue(typeParam->targetType.getType());
    }
    else if (auto* typeAlias = symbol.as_if<ast::TypeAliasType>()) {
        auto* syntax = typeAlias->getSyntax();
        if (syntax && syntax->parent &&
            syntax->parent->kind == syntax::SyntaxKind::TypeParameterDeclaration) {
            appendTypeParameterValue(typeAlias->targetType.getType());
        }
    }

    // Values for elab-known values like parameters, type aliases, and enum values
    if (ast::ParameterSymbol::isKind(symbol.kind)) {
        auto& param = symbol.as<ast::ParameterSymbol>();
        const auto& value = param.getValue();
        if (!value.bad()) {
            infoPg.appendText("Value: ").appendCode(formatConstantValue(value)).newLine();
        }
    }
    else if (ast::Type::isKind(symbol.kind)) {
        auto& type = symbol.as<ast::Type>();
        if (!unwrapErrorType(type).isError()) {
            infoPg.appendText("Resolved Type: ");
            if (!appendSourceLink(infoPg, type.location, sourceManager,
                                  getTypeString(type, TypeStringMode::Friendly)))
                infoPg.appendText(getTypeString(type, TypeStringMode::FriendlyMarkdownQuoted));
            infoPg.newLine();
            if (type.isError()) {
                renderIncompleteSubtypes(infoPg, type, sourceManager);
            }
            else if (type.getBitWidth() > 0) {
                infoPg.appendText("Resolved Width: ")
                    .appendCode(fmt::format("{}", type.getBitWidth()))
                    .newLine();
            }
        }
    }
    else if (ast::EnumValueSymbol::isKind(symbol.kind)) {
        auto& enumVal = symbol.as<ast::EnumValueSymbol>();
        const auto& value = enumVal.getValue();
        if (!value.bad()) {
            infoPg.appendText("Value: ").appendCode(value.toString()).newLine();
        }
    }
}

void renderSymbolHeader(markup::Paragraph& infoPg, const ast::Symbol& symbol,
                        std::string_view kindOverride = {}) {
    // <Kind/Type> <Name> in <Scope>
    renderSymbolHeaderName(infoPg, symbol, kindOverride);

    auto& parentSym = symbol.getParentScope()->asSymbol();
    auto lexicalPath = parentSym.getLexicalPath();

    // The typedef name needs to be appended; it's not attached to the type
    auto parentSyntax = parentSym.getSyntax();
    if (parentSyntax && parentSyntax->parent &&
        parentSyntax->parent->kind == syntax::SyntaxKind::TypedefDeclaration &&
        parentSym.kind != ast::SymbolKind::EnumType) {
        lexicalPath += "::";
        lexicalPath +=
            parentSyntax->parent->as<syntax::TypedefDeclarationSyntax>().name.valueText();
    }

    if (!lexicalPath.empty())
        infoPg.appendText(" in ").appendCode(lexicalPath);
    infoPg.newLine();
}

const ast::Symbol& getDriverSymbol(const DefinitionInfo::SymbolTarget& target) {
    if (auto* modportPort = target.symbol->as_if<ast::ModportPortSymbol>()) {
        if (modportPort->internalSymbol)
            return *modportPort->internalSymbol;
    }
    return *target.symbol;
}

void renderSymbolSyntaxes(markup::Document& doc, const DefinitionInfo::SymbolTarget& target,
                          const SourceManager& sm, const Config::HoverConfig& hovers) {
    appendSyntaxTargets(doc, target.syntaxes, sm, hovers.docCommentFormat.value());
}

struct RenderedSymbolHover {
    markup::Paragraph header;
    markup::Paragraph type;
    markup::Paragraph generatedSignals;
    markup::Paragraph value;
    markup::Document syntaxes;
    markup::Document drivers;

    void appendTo(markup::Document& doc) {
        header.append(std::move(type));
        header.append(std::move(generatedSignals));
        header.append(std::move(value));
        doc.addParagraph(std::move(header));
        doc.append(std::move(syntaxes));
        doc.append(std::move(drivers));
    }
};

RenderedSymbolHover renderSymbolHover(const DefinitionInfo::SymbolTarget& target,
                                      std::string_view label, const SourceManager& sm,
                                      const Config::HoverConfig& hovers,
                                      bool followPortDriver = true) {
    RenderedSymbolHover result;
    if (!label.empty())
        result.header.appendBold(label);
    renderSymbolHeader(result.header, *target.symbol);
    renderSymbolType(result.type, *target.symbol, sm);
    if (target.generatedSignalCount > 1) {
        result.generatedSignals.appendText("Generated signals: ")
            .appendCode(fmt::format("{}", target.generatedSignalCount))
            .newLine();
    }
    renderSymbolValue(result.value, *target.symbol, sm);
    renderSymbolSyntaxes(result.syntaxes, target, sm, hovers);
    renderDrivers(result.drivers, getDriverSymbol(target), *target.analysis, sm,
                  target.renderInputPortDriver, followPortDriver);
    return result;
}

void deduplicatePortHoverParts(const RenderedSymbolHover& outer, RenderedSymbolHover& inner) {
    if (inner.type == outer.type)
        inner.type = {};
    if (inner.value == outer.value)
        inner.value = {};
    if (inner.drivers == outer.drivers)
        inner.drivers = {};
}

std::optional<lsp::MarkupContent> renderElaboratedParameterSummary(
    const std::vector<DefinitionInfo::Target>& targets, const SourceManager& sm,
    const Config::HoverConfig& hovers) {
    if (targets.size() < 2)
        return {};

    std::vector<const DefinitionInfo::SymbolTarget*> parameters;
    std::vector<const ConstantValue*> values;
    const ConstantValue* minValue = nullptr;
    const ConstantValue* maxValue = nullptr;
    const DefinitionInfo::SymbolTarget* genvar = nullptr;
    bool isGenvar = false;
    SourceLocation declarationLocation;
    const ast::Type* parameterType = nullptr;
    for (const auto& target : targets) {
        auto* symbolTarget = std::get_if<DefinitionInfo::SymbolTarget>(&target);
        if (!symbolTarget)
            return {};

        if (symbolTarget->symbol->kind == ast::SymbolKind::Genvar) {
            if (genvar || !parameters.empty())
                return {};
            genvar = symbolTarget;
            isGenvar = true;
            continue;
        }

        auto* parameter = symbolTarget->symbol->as_if<ast::ParameterSymbol>();
        if (!parameter)
            return {};

        auto originalLocation = sm.getFullyOriginalLoc(parameter->location);
        if (parameters.empty()) {
            declarationLocation = originalLocation;
            parameterType = &parameter->getType();
            isGenvar = isGenvar || parameter->isFromGenvar();
        }
        else if (originalLocation != declarationLocation || parameter->isFromGenvar() != isGenvar ||
                 !parameter->getType().isMatching(*parameterType)) {
            return {};
        }

        const auto& value = parameter->getValue();
        if (!value.isInteger() || value.hasUnknown())
            return {};

        if (!minValue || value < *minValue)
            minValue = &value;
        if (!maxValue || value > *maxValue)
            maxValue = &value;
        if (std::ranges::none_of(values, [&](const auto* existing) { return *existing == value; }))
            values.push_back(&value);
        parameters.push_back(symbolTarget);
    }

    if (parameters.empty() || (values.size() < 2 && !genvar))
        return {};

    auto sortedValues = values;
    std::ranges::sort(sortedValues,
                      [](const auto* left, const auto* right) { return *left < *right; });
    bool isContiguous = true;
    for (size_t i = 1; i < sortedValues.size(); i++) {
        auto expected = sortedValues[i - 1]->integer();
        if (++expected != sortedValues[i]->integer()) {
            isContiguous = false;
            break;
        }
    }

    markup::Document doc;
    auto& header = doc.addParagraph();
    // TODO: when we have focused instances, gen loops will also have focused scopes
    // in which we will both render the genvar (showing all values) and the internal parameter
    // symbol
    auto kindOverride = !genvar && isGenvar ? "Genvar" : "";
    renderSymbolHeader(header, genvar ? *genvar->symbol : *parameters.front()->symbol,
                       kindOverride);
    renderSymbolType(header, *parameters.front()->symbol, sm);
    if (values.size() == 1) {
        header.appendText("Value: ").appendCode(formatConstantValue(*values.front()));
    }
    else if (isContiguous) {
        header.appendText("Value range: ")
            .appendCode(formatConstantValue(*minValue))
            .appendText(" through ")
            .appendCode(formatConstantValue(*maxValue));
    }
    else {
        header.appendText("Values: ");
        for (size_t i = 0; i < values.size(); i++) {
            if (i)
                header.appendText(", ");
            header.appendCode(formatConstantValue(*values[i]));
        }
    }
    header.newLine();

    std::vector<DefinitionInfo::SyntaxTarget> syntaxes;
    for (const auto* target : parameters) {
        syntaxes.insert(syntaxes.end(), target->syntaxes.begin(), target->syntaxes.end());
    }
    appendSyntaxTargets(doc, syntaxes, sm, hovers.docCommentFormat.value());
    return doc.build();
}

std::optional<lsp::MarkupContent> renderElaboratedParameterValues(
    const std::vector<DefinitionInfo::Target>& targets, const SourceManager& sm,
    const Config::HoverConfig& hovers) {
    if (targets.size() < 2)
        return {};

    std::vector<const DefinitionInfo::SymbolTarget*> parameterTargets;
    for (const auto& target : targets) {
        auto* symbolTarget = std::get_if<DefinitionInfo::SymbolTarget>(&target);
        if (!symbolTarget || !symbolTarget->symbol->as_if<ast::ParameterSymbol>())
            return {};
        if (!parameterTargets.empty() &&
            symbolTarget->syntaxes != parameterTargets.front()->syntaxes) {
            return {};
        }
        parameterTargets.push_back(symbolTarget);
    }

    markup::Document result;
    markup::Paragraph sharedType;
    renderSymbolType(sharedType, *parameterTargets.front()->symbol, sm);
    std::vector<const ConstantValue*> renderedValues;
    for (auto* target : parameterTargets) {
        const auto& value = target->symbol->as<ast::ParameterSymbol>().getValue();
        if (value.bad())
            return {};
        if (std::ranges::any_of(renderedValues,
                                [&](auto* rendered) { return *rendered == value; })) {
            continue;
        }

        auto hover = renderSymbolHover(*target, {}, sm, hovers);
        if (!renderedValues.empty() && hover.type == sharedType)
            hover.type = {};
        hover.appendTo(result);
        renderedValues.push_back(&value);
    }
    return result.build();
}

void renderMacroHeader(markup::Paragraph& infoPg, const DefinitionInfo::MacroTarget& macro,
                       const SourceManager& sm, BufferID docBuffer) {
    // <Kind> <Name>
    // From <File>
    if (auto* macroSyntax = macro.syntaxTarget()) {
        auto macroBuf = macroSyntax->nameToken.location().buffer();
        infoPg.appendText(toString(macroSyntax->node->kind));
        infoPg.appendText(" ").appendText(macroSyntax->nameToken.valueText()).newLine();
        if (macroBuf != docBuffer && sm.isLatestData(macroBuf)) {
            const auto& path = sm.getFullPath(macroBuf);
            auto pathStr = path.filename().string();
            if (!pathStr.empty() && pathStr[0] != '<')
                infoPg.appendText("From ").appendCode(pathStr).newLine();
        }
    }
    else if (auto* define = macro.commandLineDefine()) {
        infoPg.appendText("DefineDirective ");
        infoPg.appendText(define->nameToken.valueText()).newLine();
        if (!define->defineSourceFile.empty()) {
            namespace fs = std::filesystem;
            auto srcPath = fs::path(define->defineSourceFile);
            auto rel = srcPath.lexically_relative(fs::current_path());
            auto display = (!rel.empty() && *rel.begin() != "..") ? rel.string() : srcPath.string();
            infoPg.appendText("From ").appendCode(display).newLine();
        }
        else {
            infoPg.appendText("Defined via command-line flags").newLine();
        }
    }
}

} // namespace

DefinitionInfo::SyntaxTarget DefinitionInfo::SyntaxTarget::fromNode(
    const syntax::SyntaxNode* node, parsing::Token nameToken, const SourceManager& sourceManager) {
    auto macroUsageRange = SourceRange::NoLocation;
    if (sourceManager.isMacroLoc(nameToken.location())) {
        auto tokenRange = SourceRange(nameToken.location(),
                                      nameToken.location() + nameToken.rawText().length());
        auto expansionRange = sourceManager.getFullyExpandedRange(tokenRange);
        if (!sourceManager.getSourceText(expansionRange).empty())
            macroUsageRange = expansionRange;
    }
    return SyntaxTarget{node, std::move(nameToken), macroUsageRange};
}

void DefinitionInfo::SyntaxTarget::renderCode(markup::Paragraph& paragraph, const SourceManager& sm,
                                              bool rawDocComments) const {
    if (!paragraph.isEmpty())
        paragraph.newLine();
    const auto& displayNode = selectDisplayNode(*node);
    paragraph.appendCodeBlock(rawDocComments ? formatCodeWithLeadingComments(displayNode)
                                             : formatCode(displayNode));
    if (macroUsageRange != SourceRange::NoLocation) {
        paragraph.newLine()
            .appendText("Expanded from ")
            .newLine()
            .appendCodeBlock(sm.getSourceText(macroUsageRange));
    }
}

DefinitionInfo::MacroTarget::MacroTarget(Definition definition,
                                         const syntax::SyntaxNode& referenceSyntax,
                                         const ShallowAnalysis& analysis) :
    definition(std::move(definition)) {
    if (referenceSyntax.kind != syntax::SyntaxKind::MacroUsage)
        return;

    auto it = analysis.syntaxes.macroExpansions.find(&referenceSyntax);
    if (it != analysis.syntaxes.macroExpansions.end())
        macroExpansionText = it->second.getText();
}

markup::Document DefinitionInfo::SymbolTarget::getHover(const SourceManager& sm,
                                                        BufferID /*docBuffer*/,
                                                        const Config::HoverConfig& hovers) const {
    markup::Document doc;
    renderSymbolHover(*this, {}, sm, hovers).appendTo(doc);
    return doc;
}

markup::Document DefinitionInfo::PortConnectionTarget::getHover(
    const SourceManager& sm, BufferID /*docBuffer*/, const Config::HoverConfig& hovers) const {
    auto outerHover = renderSymbolHover(outer, "Outer", sm, hovers, false);
    auto innerHover = renderSymbolHover(inner, "Inner", sm, hovers);
    deduplicatePortHoverParts(outerHover, innerHover);

    markup::Document result;
    outerHover.appendTo(result);
    innerHover.appendTo(result);
    return result;
}

markup::Document DefinitionInfo::MacroTarget::getHover(const SourceManager& sm, BufferID docBuffer,
                                                       const Config::HoverConfig& hovers) const {
    markup::Document doc;
    renderMacroHeader(doc.addParagraph(), *this, sm, docBuffer);

    const auto* syntax = syntaxTarget();
    if (syntax)
        appendSyntaxTargets(doc, std::span<const SyntaxTarget>(syntax, 1), sm,
                            hovers.docCommentFormat.value());

    if (!macroExpansionText.empty()) {
        // Macro usage: show the expanded text at this call site
        doc.addParagraph()
            .appendText("Expands to ")
            .newLine()
            .appendText(svCodeBlockString(macroExpansionText));
    }

    return doc;
}

markup::Document DefinitionInfo::SystemSubroutineTarget::getHover(
    const SourceManager& /*sm*/, BufferID /*docBuffer*/,
    const Config::HoverConfig& /*hovers*/) const {
    markup::Document md;

    auto& head = md.addParagraph();
    head.appendBold(isTask ? "System task" : "System function")
        .appendText(" ")
        .appendCode(token.valueText());
    if (!doc->ieeeSection.empty()) {
        head.appendText(" (IEEE 1800 §").appendText(doc->ieeeSection).appendText(")");
    }
    md.addParagraph().appendCodeBlock(doc->signature);
    if (!doc->description.empty()) {
        md.addParagraph().appendText(doc->description);
    }
    return md;
}

std::vector<lsp::LocationLink> DefinitionInfo::SyntaxTarget::getDefinition(
    const SourceManager& sm) const {
    auto targetRange = (macroUsageRange != SourceRange::NoLocation) ? macroUsageRange
                                                                    : nameToken.range();
    auto path = sm.getFullPath(targetRange.start().buffer());
    auto lspRange = toRange(targetRange, sm);

    return {lsp::LocationLink{
        .targetUri = URI::fromFile(path),
        // This is supposed to be the full source range- however the hover view already provides
        // that, leading to a worse UI
        .targetRange = lspRange,
        .targetSelectionRange = lspRange,
    }};
}

std::vector<lsp::LocationLink> DefinitionInfo::SymbolTarget::getDefinition(
    const SourceManager& sm) const {
    std::vector<lsp::LocationLink> result;
    for (const auto& syntax : syntaxes) {
        for (auto& link : syntax.getDefinition(sm)) {
            auto duplicate = std::ranges::any_of(result, [&](const auto& existing) {
                return existing.targetUri == link.targetUri &&
                       existing.targetSelectionRange == link.targetSelectionRange;
            });
            if (!duplicate)
                result.push_back(std::move(link));
        }
    }
    return result;
}

std::vector<lsp::LocationLink> DefinitionInfo::PortConnectionTarget::getDefinition(
    const SourceManager& sm) const {
    auto result = outer.getDefinition(sm);
    for (auto& link : inner.getDefinition(sm)) {
        auto duplicate = std::ranges::any_of(result, [&](const auto& existing) {
            return existing.targetUri == link.targetUri &&
                   existing.targetSelectionRange == link.targetSelectionRange;
        });
        if (!duplicate)
            result.push_back(std::move(link));
    }
    return result;
}

std::vector<lsp::LocationLink> DefinitionInfo::MacroTarget::getDefinition(
    const SourceManager& sm) const {
    if (auto* define = commandLineDefine()) {
        if (define->defineSourceFile.empty())
            return {};

        auto macroName = std::string(define->nameToken.valueText());
        auto srcPath = std::filesystem::path(define->defineSourceFile);

        // Find the -D flag in the source file for precise line/column
        lsp::Range defRange = {};
        SmallVector<char> buf;
        if (!OS::readFile(srcPath, buf)) {
            std::string_view content(buf.data(), buf.size() - 1);
            std::string patterns[] = {"-D" + macroName, "-D " + macroName,
                                      "--define-macro=" + macroName, "--define-macro " + macroName,
                                      "+define+" + macroName};
            for (auto& pat : patterns) {
                auto pos = content.find(pat);
                if (pos != std::string::npos) {
                    lsp::uint line = 0, col = 0;
                    for (size_t i = 0; i < pos; i++) {
                        if (content[i] == '\n') {
                            line++;
                            col = 0;
                        }
                        else {
                            col++;
                        }
                    }
                    defRange = {.start = {line, col}, .end = {line, col}};
                    break;
                }
            }
        }

        return {lsp::LocationLink{
            .targetUri = URI::fromFile(srcPath),
            .targetRange = defRange,
            .targetSelectionRange = defRange,
        }};
    }

    if (auto* syntax = syntaxTarget())
        return syntax->getDefinition(sm);

    return {};
}

std::vector<lsp::LocationLink> DefinitionInfo::SystemSubroutineTarget::getDefinition(
    const SourceManager& /*sm*/) const {
    return {};
}

lsp::MarkupContent DefinitionInfo::getHover(BufferID docBuffer,
                                            const Config::HoverConfig& hovers) const {
    const auto& sm = sourceManager.get();
    if (auto parameterSummary = renderElaboratedParameterSummary(targets, sm, hovers))
        return std::move(*parameterSummary);
    if (auto parameterValues = renderElaboratedParameterValues(targets, sm, hovers))
        return std::move(*parameterValues);

    std::vector<std::pair<SourceLocation, markup::Document>> renderedTargets;
    for (const auto& target : targets) {
        auto rendered = std::visit(
            [&](const auto& concreteTarget) {
                return std::pair{concreteTarget.nameToken().location(),
                                 concreteTarget.getHover(sm, docBuffer, hovers)};
            },
            target);
        auto duplicate = std::ranges::any_of(renderedTargets, [&](const auto& existing) {
            return existing.first == rendered.first && existing.second == rendered.second;
        });
        if (!duplicate)
            renderedTargets.push_back(std::move(rendered));
    }

    markup::Document result;
    for (auto& rendered : renderedTargets)
        result.append(std::move(rendered.second));
    return result.build();
}

std::vector<lsp::LocationLink> DefinitionInfo::getDefinitionLspLinks() const {
    const auto& sm = sourceManager.get();
    std::vector<lsp::LocationLink> result;
    for (const auto& target : targets) {
        auto links = std::visit([&](const auto& t) { return t.getDefinition(sm); }, target);
        for (auto& link : links) {
            auto duplicate = std::ranges::any_of(result, [&](const auto& existing) {
                return existing.targetUri == link.targetUri &&
                       existing.targetSelectionRange == link.targetSelectionRange;
            });
            if (!duplicate)
                result.push_back(std::move(link));
        }
    }
    return result;
}

std::vector<lsp::Location> DefinitionInfo::getDefinitionLspLocs() const {
    auto links = getDefinitionLspLinks();
    std::vector<lsp::Location> result;
    result.reserve(links.size());
    for (auto& link : links)
        result.emplace_back(std::move(link.targetUri), link.targetRange);
    return result;
}

} // namespace server
