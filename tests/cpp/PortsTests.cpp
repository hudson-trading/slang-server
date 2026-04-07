#include "document/SyntaxIndexer.h"
#include "util/Converters.h"
#include "utils/ServerHarness.h"
#include <catch2/catch_test_macros.hpp>

#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

struct PortInfo {
    std::string name;
    std::string direction;
};

std::vector<PortInfo> getPorts(std::shared_ptr<hier::ShallowAnalysis> analysis,
                               std::string_view moduleName) {
    using namespace slang;
    using namespace slang::ast;

    std::vector<PortInfo> result;

    const Symbol* sym = analysis->getDefinition(moduleName);
    if (!sym || sym->kind != SymbolKind::Definition)
        return result;

    const auto& def = sym->as<DefinitionSymbol>();
    auto& comp = *analysis->getCompilation();
    const auto& inst = InstanceSymbol::createDefault(comp, def);
    const auto& body = inst.body;

    for (const Symbol* s : body.getPortList()) {
        switch (s->kind) {
            case SymbolKind::Port: {
                const auto& port = s->as<PortSymbol>();
                result.push_back({std::string(port.name), std::string(toString(port.direction))});
                break;
            }

            case SymbolKind::MultiPort: {
                const auto& mp = s->as<MultiPortSymbol>();
                for (auto& port : mp.ports) {
                    result.push_back(
                        {std::string(port->name), std::string(toString(port->direction))});
                }
                break;
            }

            case SymbolKind::InterfacePort: {
                const auto& ip = s->as<InterfacePortSymbol>();
                result.push_back({std::string(ip.name), "interface"});
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
    input clk;
    input rst;
    inout io;
    output done;
endmodule
)");

    auto analysis = doc.doc->getAnalysis();
    auto ports = getPorts(analysis, "mymod");

    JsonGoldenTest golden;
    golden.record(ports);
}