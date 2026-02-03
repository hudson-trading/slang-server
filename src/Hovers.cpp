

#include "Hovers.h"

#include "document/ShallowAnalysis.h"
#include "util/Formatting.h"
#include "util/Logging.h"
#include "util/Markdown.h"

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {

lsp::MarkupContent getHover(const SourceManager& sm, const BufferID docBuffer,
                            const DefinitionInfo& info) {
    markup::Document doc;

    auto& infoPg = doc.addParagraph();

    if (info.symbol) {
        // <Kind/Type> <Name> in <Scope>

        infoPg.appendBold(toString(info.symbol->kind)).appendCode(info.symbol->name);

        auto symbolScope = info.symbol->getParentScope();
        auto& parentSym = symbolScope->asSymbol();
        auto hierPath = parentSym.getLexicalPath();

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
        if (ast::ValueSymbol::isKind(info.symbol->kind) &&
            info.symbol->kind != ast::SymbolKind::EnumValue) {
            auto& valSym = info.symbol->as<ast::ValueSymbol>();
            auto& type = valSym.getType();
            auto typeStr = getHoverTypeString(type);
            infoPg.appendText("Type: ").appendText(typeStr).newLine();
            if (!ast::ParameterSymbol::isKind(info.symbol->kind) && !type.isError() &&
                type.getBitWidth() > 1) {
                infoPg.appendText("Width: ")
                    .appendCode(fmt::format("{}", type.getBitWidth()))
                    .newLine();
            }
        }
        else if (ast::InstanceSymbol::isKind(info.symbol->kind)) {
            auto& instSym = info.symbol->as<ast::InstanceSymbol>();
            auto typeStr = instSym.getDefinition().name;
            infoPg.appendText("Type: ").appendText(typeStr).newLine();
        }

        // Values for elab-known values like parameters, type aliases, and enum values
        if (ast::ParameterSymbol::isKind(info.symbol->kind)) {
            auto& param = info.symbol->as<ast::ParameterSymbol>();
            auto value = param.getValue();
            if (!value.bad()) {
                infoPg.appendText("Value: ").appendCode(formatConstantValue(value)).newLine();
            }
        }
        else if (ast::Type::isKind(info.symbol->kind)) {
            auto& type = info.symbol->as<ast::Type>();
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
        else if (ast::EnumValueSymbol::isKind(info.symbol->kind)) {
            auto& enumVal = info.symbol->as<ast::EnumValueSymbol>();
            auto value = enumVal.getValue();
            if (!value.bad()) {
                infoPg.appendText("Value: ").appendCode(value.toString()).newLine();
            }
        }
    }
    else {
        // <Kind> <Name>
        // From <File>
        auto macroBuf = info.nameToken.location().buffer();
        infoPg.appendText(toString(info.node->kind));
        infoPg.appendText(" ").appendText(info.nameToken.valueText()).newLine();
        if (macroBuf != docBuffer && sm.isLatestData(macroBuf)) {
            const auto& path = sm.getFullPath(macroBuf);
            if (!path.empty()) {
                infoPg.appendText("From ").appendCode(path.filename().string()).newLine();
            }
        }
    }

    // Add the main code block with proper formatting
    doc.addParagraph().appendCodeBlock(formatSyntaxNode(*info.node));

    // Show macro expansion if present
    if (info.macroUsageRange != SourceRange::NoLocation) {
        auto text = sm.getText(info.macroUsageRange);
        doc.addParagraph().appendText("Expanded from ").newLine().appendCodeBlock(text);
    }

    return doc.build();
}

} // namespace server
