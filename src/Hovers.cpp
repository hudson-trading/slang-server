#include "Hovers.h"

#include "Config.h"
#include "document/ShallowAnalysis.h"
#include "util/Formatting.h"
#include "util/Markdown.h"

#include "slang/analysis/ValueDriver.h"
#include "slang/ast/SemanticFacts.h"
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

namespace {
/// Obtains the driver display node if available. Currently it only collects
/// continuous assignment drivers (ie: `assign`) since they are guaranteed to only have a single
/// driver node (except for `tri` and maybe others).
const syntax::SyntaxNode* getDriverDisplayNode(const ShallowAnalysis& analysis,
                                               const slang::analysis::ValueDriver& driver,
                                               const std::size_t driversCount) {
    if (driver.kind != slang::analysis::DriverKind::Continuous) {
        return nullptr;
    }

    if (driversCount > 1) {
        // If there's more than one continuous assign driver, then there's an error
        // in the code and we early exit
        return nullptr;
    }

    const auto range = driver.getSourceRange();
    if (range == SourceRange::NoLocation) {
        return nullptr;
    }

    const auto loc = analysis.getSourceManager().getFullyOriginalLoc(range.start());
    auto node = analysis.syntaxes.getSyntaxAt(loc);

    for (auto cur = node; cur; cur = cur->parent) {
        switch (cur->kind) {
            case syntax::SyntaxKind::ContinuousAssign:
                return &selectDisplayNode(*cur);

            default:
                break;
        }
    }

    return nullptr;
}
} // namespace

lsp::MarkupContent getHover(const SourceManager& sm,
                            const std::shared_ptr<ShallowAnalysis> analysis,
                            const BufferID docBuffer, const DefinitionInfo& info,
                            const Config::HoverConfig& hovers) {
    markup::Document doc;

    auto& infoPg = doc.addParagraph();

    const syntax::SyntaxNode* extraDisplayNode = nullptr;

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
            const auto& valSym = info.symbol->as<ast::ValueSymbol>();
            const auto& type = valSym.getType();
            const auto typeStr = getHoverTypeString(type);
            infoPg.appendText("Type: ").appendText(typeStr).newLine();
            if (!ast::ParameterSymbol::isKind(info.symbol->kind) && !type.isError() &&
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
                            : fmt::format(
                                  "{} ({})", toString(kind),
                                  ast::SemanticFacts::getProcedureKindStr(
                                      static_cast<slang::ast::ProceduralBlockKind>(source)));

                    infoPg.appendText("Driver: ").appendCode(driverStr).newLine();

                    extraDisplayNode = getDriverDisplayNode(*analysis, *uniqueDriver,
                                                            drivers.size());
                }
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

    const syntax::SyntaxNode& displayNode = selectDisplayNode(*info.node);
    const auto docCommentFormat = hovers.docCommentFormat.value();

    if (docCommentFormat == Config::HoverConfig::DocCommentFormat::raw) {
        // Print the node verbatim with its leading comments in a single code block

        std::string code = formatCodeWithLeadingComments(displayNode);

        if (extraDisplayNode) {
            code += "\n";
            code += formatCode(*extraDisplayNode);
        }

        doc.addParagraph().appendCodeBlock(code);
    }
    else {
        const std::string docComments = getDocCommentForHover(displayNode, docCommentFormat);
        if (!docComments.empty())
            doc.addParagraph().appendText(docComments).newLine();

        std::string code = formatCode(displayNode);

        if (extraDisplayNode) {
            code += "\n";
            code += formatCode(*extraDisplayNode);
        }

        doc.addParagraph().appendCodeBlock(code);
    }

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
