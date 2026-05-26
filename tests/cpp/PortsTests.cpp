#include "Hovers.h"
#include "document/SyntaxIndexer.h"
#include "util/Converters.h"
#include "utils/ServerHarness.h"
#include <catch2/catch_test_macros.hpp>

#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

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

PortsInfo getPorts(std::shared_ptr<hier::ShallowAnalysis> analysis, std::string_view moduleName) {
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

TEST_CASE("GetPortsNonAnsi") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module mymod(clk, rst, io, done);
    input logic clk;
    input logic rst;
    inout logic io;
    output logic done;
endmodule
)");

    auto analysis = doc.doc->getAnalysis();
    auto ports = getPorts(analysis, "mymod");

    JsonGoldenTest golden;
    golden.record(ports);
}

TEST_CASE("GetPortsAnsi") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module mymod(input logic clk, output logic done);
endmodule
)");

    auto analysis = doc.doc->getAnalysis();
    auto ports = getPorts(analysis, "mymod");

    JsonGoldenTest golden;
    golden.record(ports);
}

TEST_CASE("GetPortsNonAnsi_Hovers") {
    ServerHarness server;

    auto doc = server.openFile("test.sv", R"(
module mymod(clk, rst, io, done);
    input logic clk;
    input logic rst;
    inout logic io;
    output logic done;
endmodule
)");

    auto analysis = doc.doc->getAnalysis();
    auto ports = getPorts(analysis, "mymod");

    auto cursor = doc.before("mymod");
    auto s = doc.getHoverAt(cursor.m_offset);

    JsonGoldenTest golden;
    golden.record(s->contents);
}