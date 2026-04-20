

#include "Hovers.h"

#include "Config.h"
#include "document/ShallowAnalysis.h"
#include "util/Formatting.h"
#include "util/Markdown.h"

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

namespace server {

lsp::MarkupContent getHover(const SourceManager& sm, const BufferID docBuffer,
                            const DefinitionInfo& info, const Config::HoverConfig& hovers) {
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
            auto pathStr = path.filename().string();
            if (!pathStr.empty() && pathStr[0] != '<') {
                infoPg.appendText("From ").appendCode(pathStr).newLine();
            }
            else if (!info.defineSourceFile.empty()) {
                namespace fs = std::filesystem;
                auto srcPath = fs::path(info.defineSourceFile);
                auto rel = srcPath.lexically_relative(fs::current_path());
                auto display = (!rel.empty() && *rel.begin() != "..") ? rel.string()
                                                                      : srcPath.string();
                infoPg.appendText("From ").appendCode(display).newLine();
            }
            else {
                infoPg.appendText("Defined via command-line flags").newLine();
            }
        }
    }

    const syntax::SyntaxNode& display_node = selectDisplayNode(*info.node);

    const auto docCommentsMode = hovers.docComments.value();
    const std::string docComments = stripDocComment(display_node, docCommentsMode);

    if (!docComments.empty()) {
        if (docCommentsMode == Config::HoverConfig::DocComments::raw)
            doc.addParagraph().appendCodeBlock(docComments);
        else
            doc.addParagraph().appendText(docComments).newLine();
    }

    // Add the main code block with proper formatting
    doc.addParagraph().appendCodeBlock(formatCode(display_node));

    // Show what a macro expands to
    if (!info.macroExpansionText.empty()) {
        // Macro usage: show the expanded text at this call site
        doc.addParagraph()
            .appendText("Expands to ")
            .newLine()
            .appendText(svCodeBlockString(info.macroExpansionText));
    }

    // Show macro expansion if present (token is inside a macro expansion)
    if (info.macroUsageRange != SourceRange::NoLocation) {
        auto text = sm.getText(info.macroUsageRange);
        doc.addParagraph().appendText("Expanded from ").newLine().appendCodeBlock(text);
    }

    return doc.build();
}

} // namespace server
