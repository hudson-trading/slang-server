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
#include <algorithm>
#include <filesystem>

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

void renderSymbolHeaderName(markup::Paragraph& infoPg, const ast::Symbol& symbol) {
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

void renderSymbolType(markup::Paragraph& infoPg, const ast::Symbol& symbol) {
    if (ast::ValueSymbol::isKind(symbol.kind) && symbol.kind != ast::SymbolKind::EnumValue) {
        const auto& valSym = symbol.as<ast::ValueSymbol>();
        const auto& type = valSym.getType();
        const auto typeStr = getHoverTypeString(type);
        infoPg.appendText("Type: ").appendText(typeStr).newLine();
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
    SmallVector<const syntax::SyntaxNode*, 4> displayNodes;
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

void appendSourceLink(markup::Paragraph& paragraph, SourceLocation location,
                      const SourceManager& sourceManager) {
    const auto originalLoc = sourceManager.getFullyOriginalLoc(location);
    if (!sourceManager.isFileLoc(originalLoc))
        return;

    const auto& path = sourceManager.getFullPath(originalLoc.buffer());
    if (path.empty())
        return;

    const auto line = sourceManager.getLineNumber(originalLoc);
    const auto column = sourceManager.getColumnNumber(originalLoc);
    const auto label = fmt::format("{}:{}:{}", path.filename().string(), line, column);
    const auto target = fmt::format("{}#L{},{}", URI::fromFile(path), line, column);
    paragraph.appendText(" at ").appendText(fmt::format("[{}](<{}>)", label, target));
}

void renderDrivers(markup::Document& doc, const ast::Symbol& symbol, ShallowAnalysis& analysis,
                   const SourceManager& sourceManager) {
    if (!ast::ValueSymbol::isKind(symbol.kind) || symbol.kind == ast::SymbolKind::EnumValue)
        return;

    std::vector<DriverGroup> groups;
    for (const auto* driver : analysis.getDrivers(symbol.as<ast::ValueSymbol>())) {
        if (!driver || driver->isInputPort())
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
        if (const auto* node = getDriverDisplayNode(*driver);
            node && std::ranges::find(group.displayNodes, node) == group.displayNodes.end()) {
            group.displayNodes.push_back(node);
        }
    }

    for (const auto& group : groups) {
        auto& paragraph = doc.addParagraph();
        paragraph.appendText(getDriverGroupHeader(group));

        auto location = group.containingSymbol->location;
        if (!group.displayNodes.empty())
            location = group.displayNodes.front()->getFirstToken().location();
        appendSourceLink(paragraph, location, sourceManager);

        std::string code;
        for (const auto* node : group.displayNodes) {
            if (!code.empty())
                code += "\n";
            code += formatCode(*node);
        }
        if (!code.empty())
            paragraph.newLine().appendCodeBlock(code);
    }
}

void renderSymbolValue(markup::Paragraph& infoPg, const ast::Symbol& symbol) {
    auto appendTypeParameterValue = [&](const ast::Type& type) {
        if (!type.isError()) {
            infoPg.appendText("Value: ").appendText(getHoverTypeString(type)).newLine();
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
        if (!type.isError()) {
            auto typeString = getHoverTypeString(type);
            infoPg.appendText("Resolved Type: ").appendText(typeString).newLine();
            if (type.getBitWidth() > 0) {
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

void renderSymbolHeader(markup::Paragraph& infoPg, const ast::Symbol& symbol) {
    // <Kind/Type> <Name> in <Scope>
    renderSymbolHeaderName(infoPg, symbol);

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

    renderSymbolType(infoPg, symbol);
    renderSymbolValue(infoPg, symbol);
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

void DefinitionInfo::SyntaxTarget::renderCode(markup::Document& doc, const SourceManager& sm,
                                              const Config::HoverConfig& hovers) const {
    const syntax::SyntaxNode& displayNode = selectDisplayNode(*node);
    const auto docCommentFormat = hovers.docCommentFormat.value();

    if (docCommentFormat == Config::HoverConfig::DocCommentFormat::raw) {
        // Print the node verbatim with its leading comments in a single code block
        doc.addParagraph().appendCodeBlock(formatCodeWithLeadingComments(displayNode));
    }
    else {
        const std::string docComments = getDocCommentForHover(displayNode, docCommentFormat);
        if (!docComments.empty()) {
            doc.addParagraph().appendText(docComments).newLine();
        }

        doc.addParagraph().appendCodeBlock(formatCode(displayNode));
    }

    if (macroUsageRange != SourceRange::NoLocation) {
        auto text = sm.getSourceText(macroUsageRange);
        doc.addParagraph().appendText("Expanded from ").newLine().appendCodeBlock(text);
    }
}

lsp::MarkupContent DefinitionInfo::SymbolTarget::getHover(const SourceManager& sm,
                                                          BufferID /*docBuffer*/,
                                                          const Config::HoverConfig& hovers) const {
    markup::Document doc;
    renderSymbolHeader(doc.addParagraph(), *symbol);
    syntax.renderCode(doc, sm, hovers);
    renderDrivers(doc, *symbol, *analysis, sm);
    return doc.build();
}

lsp::MarkupContent DefinitionInfo::MacroTarget::getHover(const SourceManager& sm,
                                                         BufferID docBuffer,
                                                         const Config::HoverConfig& hovers) const {
    markup::Document doc;
    renderMacroHeader(doc.addParagraph(), *this, sm, docBuffer);

    const auto* syntax = syntaxTarget();
    if (syntax)
        syntax->renderCode(doc, sm, hovers);

    if (!macroExpansionText.empty()) {
        // Macro usage: show the expanded text at this call site
        doc.addParagraph()
            .appendText("Expands to ")
            .newLine()
            .appendText(svCodeBlockString(macroExpansionText));
    }

    return doc.build();
}

lsp::MarkupContent DefinitionInfo::SystemSubroutineTarget::getHover(
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
    return md.build();
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
    return syntax.getDefinition(sm);
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

lsp::MarkupContent DefinitionInfo::getHover(const SourceManager& sm, BufferID docBuffer,
                                            const Config::HoverConfig& hovers) const {
    return std::visit([&](const auto& t) { return t.getHover(sm, docBuffer, hovers); }, target);
}

std::vector<lsp::LocationLink> DefinitionInfo::getDefinition(const SourceManager& sm) const {
    return std::visit([&](const auto& t) { return t.getDefinition(sm); }, target);
}

} // namespace server
