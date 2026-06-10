

#include "Hovers.h"

#include "Config.h"
#include "document/ShallowAnalysis.h"
#include "util/Formatting.h"
#include "util/Markdown.h"

#include "slang/analysis/ValueDriver.h"
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
std::string_view driverSourceToString(const slang::analysis::DriverSource source) {
    using slang::analysis::DriverSource;

    switch (source) {
        case DriverSource::Initial:
            return "initial";
        case DriverSource::Final:
            return "final";
        case DriverSource::Always:
            return "always";
        case DriverSource::AlwaysComb:
            return "always_comb";
        case DriverSource::AlwaysLatch:
            return "always_latch";
        case DriverSource::AlwaysFF:
            return "always_ff";
        case DriverSource::Subroutine:
            return "subroutine";
        case DriverSource::Other:
            return "other";
    }

    return "unknown";
}

std::string_view driverKindToString(const slang::analysis::DriverKind kind) {
    using slang::analysis::DriverKind;

    switch (kind) {
        case DriverKind::Procedural:
            return "procedural";
        case DriverKind::Continuous:
            return "continuous";
    }

    return "unknown";
}

const syntax::SyntaxNode* getDriverDisplayNode(const ShallowAnalysis& analysis,
                                               const slang::analysis::ValueDriver& driver) {
    if (driver.kind != slang::analysis::DriverKind::Continuous) {
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
                /// We only show driver info for values that have a single unique driver source.
                /// `structs` and variables that use `always` statements can have multiple drivers.
                std::vector<const slang::analysis::ValueDriver*> uniqueDrivers;

                for (const auto* driver : drivers) {
                    if (!driver) {
                        continue;
                    }

                    const auto sameKindAndSource =
                        [driver](const slang::analysis::ValueDriver* existing) {
                            return existing && existing->kind == driver->kind &&
                                   existing->source == driver->source;
                        };

                    if (std::ranges::find_if(uniqueDrivers, sameKindAndSource) ==
                        uniqueDrivers.end()) {
                        uniqueDrivers.push_back(driver);
                    }
                }

                if (uniqueDrivers.size() == 1) {
                    const auto* driver = uniqueDrivers.front();

                    const auto kind = driver->kind;
                    const auto source = driver->source;

                    const auto driverStr = source == slang::analysis::DriverSource::Other
                                               ? std::string(driverKindToString(kind))
                                               : fmt::format("{} ({})", driverKindToString(kind),
                                                             driverSourceToString(source));

                    infoPg.appendText("Driver: ").appendCode(driverStr).newLine();

                    extraDisplayNode = getDriverDisplayNode(*analysis, *driver);
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
