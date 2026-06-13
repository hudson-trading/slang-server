

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

namespace {

struct PortInfo {
    std::string name;
    std::string direction;
    std::string type;
    bool isAnsi = true;
};

struct PortsInfo {
    std::vector<PortInfo> ports;
    bool hasAnsiPorts;
};

std::string portDirectionToString(ast::ArgumentDirection dir) {
    switch (dir) {
        case ast::ArgumentDirection::In:
            return "input";
        case ast::ArgumentDirection::Out:
            return "output";
        case ast::ArgumentDirection::InOut:
            return "inout";
        case ast::ArgumentDirection::Ref:
            return "ref";
        default:
            return "";
    };
};

PortsInfo getPorts(std::shared_ptr<ShallowAnalysis> analysis, std::string_view moduleName) {
    using namespace slang;
    using namespace slang::ast;

    PortsInfo result;

    const Symbol* sym = analysis->getDefinition(moduleName);
    if (!sym || sym->kind != SymbolKind::Definition)
        return result;

    const auto& def = sym->as<DefinitionSymbol>();
    auto& comp = *analysis->getCompilation();
    const auto& inst = InstanceSymbol::createDefault(comp, def);
    const auto& body = inst.body;

    result.hasAnsiPorts = !def.hasNonAnsiPorts;

    for (const Symbol* s : body.getPortList()) {
        switch (s->kind) {
            case SymbolKind::Port: {
                const auto& port = s->as<PortSymbol>();
                result.ports.push_back({std::string(port.name),
                                        std::string(portDirectionToString(port.direction)),
                                        std::string(port.getType().toString()), port.isAnsiPort});
                break;
            }

            case SymbolKind::MultiPort: {
                const auto& mp = s->as<MultiPortSymbol>();
                for (auto& port : mp.ports) {
                    result.ports.push_back({std::string(port->name),
                                            std::string(portDirectionToString(port->direction)),
                                            std::string(port->getType().toString()),
                                            port->isAnsiPort});
                }
                break;
            }

            case SymbolKind::InterfacePort: {
                const auto& ip = s->as<InterfacePortSymbol>();
                result.ports.push_back({std::string(ip.name), "interface", "", true});
                break;
            }

            default:
                break;
        }
    }

    return result;
}

std::string formatNonAnsiModulePorts(const PortsInfo& ports) {
    fmt::memory_buffer out;

    for (const auto& port : ports.ports) {
        if (port.direction == "interface") {
            fmt::format_to(fmt::appender(out), "   interface {};\n", port.name);
        }
        else if (!port.type.empty()) {
            fmt::format_to(fmt::appender(out), "   {} {} {};\n", port.direction, port.type,
                           port.name);
        }
        else {
            fmt::format_to(fmt::appender(out), "   {} {};\n", port.direction, port.name);
        }
    }

    return fmt::to_string(out);
}

} // namespace

lsp::MarkupContent getHover(const std::shared_ptr<ShallowAnalysis> sa, const SourceManager& sm,
                            const BufferID docBuffer, const DefinitionInfo& info,
                            const Config::HoverConfig& hovers) {
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

    const auto docCommentFormat = hovers.docCommentFormat.value();

    if (docCommentFormat == Config::HoverConfig::DocCommentFormat::raw) {
        // Print the node verbatim with its leading comments in a single code block
        doc.addParagraph().appendCodeBlock(formatCodeWithLeadingComments(display_node));
    }
    else {
        const std::string docComments = getDocCommentForHover(display_node, docCommentFormat);
        if (!docComments.empty())
            doc.addParagraph().appendText(docComments).newLine();
        std::string codeBlock = formatCode(display_node);

        if (display_node.kind == syntax::SyntaxKind::ModuleHeader && info.symbol &&
            info.symbol->kind == ast::SymbolKind::Definition) {
            const auto ports = getPorts(sa, info.symbol->name);

            if (!ports.hasAnsiPorts) {
                const auto portSummary = formatNonAnsiModulePorts(ports);
                if (!portSummary.empty()) {
                    codeBlock += "\n" + portSummary +
                                 "\n  endmodule : " + std::string(info.symbol->name);
                }
            }
        }

        doc.addParagraph().appendCodeBlock(codeBlock);
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
