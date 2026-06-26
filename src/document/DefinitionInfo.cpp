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
#include <filesystem>

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
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

const syntax::SyntaxNode* renderSymbolHeader(markup::Paragraph& infoPg, const ast::Symbol& symbol,
                                             const std::shared_ptr<ShallowAnalysis>& analysis) {
    // <Kind/Type> <Name> in <Scope>
    infoPg.appendBold(toString(symbol.kind)).appendCode(symbol.name);

    auto symbolScope = symbol.getParentScope();
    auto& parentSym = symbolScope->asSymbol();
    auto hierPath = parentSym.getLexicalPath();

    const syntax::SyntaxNode* extraDisplayNode = nullptr;

    // The typedef name needs to be appended; it's not attached to the type
    if (parentSym.kind == ast::SymbolKind::PackedStructType ||
        parentSym.kind == ast::SymbolKind::UnpackedStructType) {
        auto syntax = parentSym.getSyntax();
        if (syntax && syntax->parent &&
            syntax->parent->kind == syntax::SyntaxKind::TypedefDeclaration) {
            hierPath += "::";
            hierPath += syntax->parent->as<syntax::TypedefDeclarationSyntax>().name.valueText();
        }
    }

    if (!hierPath.empty()) {
        infoPg.appendText(" in ").appendCode(hierPath);
    }
    infoPg.newLine();

    // Type info for value symbols and instance symbols
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

        const auto drivers = analysis->getDrivers(valSym);
        if (!drivers.empty()) {
            const slang::analysis::ValueDriver* uniqueDriver = nullptr;

            for (const auto* driver : drivers) {
                if (!driver) {
                    continue;
                }

                if (!uniqueDriver) {
                    uniqueDriver = driver;
                }

                else if (driver->kind != uniqueDriver->kind ||
                         driver->source != uniqueDriver->source) {
                    uniqueDriver = nullptr;
                    break;
                }
            }

            if (uniqueDriver) {
                const auto kind = uniqueDriver->kind;
                const auto source = uniqueDriver->source;

                const auto driverStr =
                    (source == slang::analysis::DriverSource::Other ||
                     source == slang::analysis::DriverSource::Subroutine)
                        ? std::string(toString(kind))
                        : fmt::format("{} ({})", toString(kind),
                                      ast::SemanticFacts::getProcedureKindStr(
                                          static_cast<slang::ast::ProceduralBlockKind>(source)));

                infoPg.appendText("Driver: ").appendCode(driverStr).newLine();

                if (uniqueDriver->kind != slang::analysis::DriverKind::Continuous) {
                    return nullptr;
                }

                // Currently it only collects continuous assignment drivers (ie: `assign`) since
                // they are guaranteed to only have a single driver node (except for `tri` and maybe
                // others).
                if (drivers.size() > 1) {
                    return nullptr;
                }

                const auto range = uniqueDriver->getSourceRange();
                if (range == SourceRange::NoLocation) {
                    return nullptr;
                }

                const auto loc = analysis->getSourceManager().getFullyOriginalLoc(range.start());
                const auto node = analysis->syntaxes.getSyntaxAt(loc);

                for (auto cur = node; cur; cur = cur->parent) {
                    if (cur->kind == syntax::SyntaxKind::ContinuousAssign) {
                        extraDisplayNode = &selectDisplayNode(*cur);
                    }
                }
            }
        }
    }

    else if (ast::InstanceSymbol::isKind(symbol.kind)) {
        auto& instSym = symbol.as<ast::InstanceSymbol>();
        auto typeStr = instSym.getDefinition().name;
        infoPg.appendText("Type: ").appendText(typeStr).newLine();
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
            if (!type.isError() && type.getBitWidth() > 0) {
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
    return extraDisplayNode;
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

void DefinitionInfo::SyntaxTarget::renderCode(markup::Document& doc,
                                              const Config::HoverConfig& hovers,
                                              const syntax::SyntaxNode* extraDisplayNode) const {

    const syntax::SyntaxNode& displayNode = selectDisplayNode(*node);
    const auto docCommentFormat = hovers.docCommentFormat.value();

    auto appendExtraDisplayNode = [&](std::string& code) {
        if (extraDisplayNode) {
            code += "\n";

            const auto formattedCode = formatCode(*extraDisplayNode);

            // Catches when people have long if/else (?/:) chains
            // 300 is completely arbitrary and could probably be made into a config option or
            // a compile time constant
            if (formattedCode.size() <= 300)
                code += formattedCode;
        }
    };

    if (docCommentFormat == Config::HoverConfig::DocCommentFormat::raw) {
        // Print the node verbatim with its leading comments in a single code block.
        std::string code = formatCodeWithLeadingComments(displayNode);
        appendExtraDisplayNode(code);
        doc.addParagraph().appendCodeBlock(code);
        return;
    }

    const std::string docComments = getDocCommentForHover(displayNode, docCommentFormat);
    if (!docComments.empty()) {
        doc.addParagraph().appendText(docComments).newLine();
    }

    std::string code = formatCode(displayNode);
    appendExtraDisplayNode(code);
    doc.addParagraph().appendCodeBlock(code);
}

void DefinitionInfo::SyntaxTarget::renderMacroExpansion(markup::Document& doc,
                                                        const SourceManager& sm) const {
    if (macroUsageRange == SourceRange::NoLocation)
        return;
    auto text = sm.getText(macroUsageRange);
    doc.addParagraph().appendText("Expanded from ").newLine().appendCodeBlock(text);
}

lsp::MarkupContent DefinitionInfo::SymbolTarget::getHover(const SourceManager& sm,
                                                          BufferID /*docBuffer*/,
                                                          const Config::HoverConfig& hovers) const {
    markup::Document doc;
    const syntax::SyntaxNode* extraDisplayNode = renderSymbolHeader(doc.addParagraph(), *symbol,
                                                                    analysis);
    syntax.renderCode(doc, hovers, extraDisplayNode);
    syntax.renderMacroExpansion(doc, sm);
    return doc.build();
}

lsp::MarkupContent DefinitionInfo::MacroTarget::getHover(const SourceManager& sm,
                                                         BufferID docBuffer,
                                                         const Config::HoverConfig& hovers) const {
    markup::Document doc;
    renderMacroHeader(doc.addParagraph(), *this, sm, docBuffer);

    const auto* syntax = syntaxTarget();
    if (syntax)
        syntax->renderCode(doc, hovers);

    if (!macroExpansionText.empty()) {
        // Macro usage: show the expanded text at this call site
        doc.addParagraph()
            .appendText("Expands to ")
            .newLine()
            .appendText(svCodeBlockString(macroExpansionText));
    }

    if (syntax)
        syntax->renderMacroExpansion(doc, sm);

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
