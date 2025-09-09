// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

// #include "document/SlangDoc.h"
#include "Indexer.h"
#include "catch2/catch_test_macros.hpp"
#include "lsp/LspTypes.h"
#include <cstdio>
#include <filesystem>

#include "slang/driver/SourceLoader.h"
#include "slang/text/SourceManager.h"

class TestIndexer {
    struct FileHandle {
        explicit FileHandle() : fd(mkstemp(fileName)), path(std::filesystem::current_path()) {
            REQUIRE(fd >= 0);
        }
        ~FileHandle() {
            unlink(fileName);
            close(fd);
        }

        std::string name() const { return fileName; }

        void writeContent(std::string content) {
            REQUIRE(fd >= 0);
            write(fd, content.c_str(), content.size());
        }

        std::string getContent() const {
            REQUIRE(fd >= 0);
            char buffer[1024];
            lseek(fd, 0, SEEK_SET);
            auto bytesRead = read(fd, buffer, sizeof(buffer));
            return std::string(buffer, bytesRead) + '\0';
        }

        std::string fullPath() const { return path / fileName; }

    private:
        // template string is pretty specific, don't change it unless you consult mkstemp spec
        char fileName[12] = "_testXXXXXX";
        int fd;
        const std::filesystem::path path;
    };

public:
    FileHandle& addFile(const std::string& contents) {
        openFiles.push_back(std::make_unique<FileHandle>());

        openFiles.back()->writeContent(contents);
        return *openFiles.back();
    };

protected:
    slang::SourceManager sm;
    slang::driver::SourceLoader sl{sm};
    Indexer indexer;

    std::vector<std::unique_ptr<FileHandle>> openFiles;
};

TEST_CASE_METHOD(TestIndexer, "BasicIndexing") {
    const char* file1Content = R"(
module automatic m1 import p::*; #(int i = 1)
    (a, b, , .c({a, b[0]}));
    input a;
    output [1:0] b;
endmodule

(* attr = 3.14 *) bind m3.m m1 #(1) bound('x, , , );

interface Iface;
    extern function void foo(int i, real r);
    extern forkjoin task t3();

    modport m(export foo, function void bar(int, logic), task baz, export func);
    modport n(import function void func(int), import task t2);
    modport o(export t2);
endinterface

module n(Iface.m a);
    initial begin
        a.foo(42, 3.14);
        a.bar(1, 1);
        a.baz();
    end

    function void a.bar(int i, logic l); endfunction
    task a.baz; endtask
    function void a.func(int i); endfunction

    function void a.foo(int i, real r);
    endfunction
endmodule

module m4;
    Iface i1();
    n n1(i1);

    Iface i2();
    n n2(i2.m);

    localparam int baz = 3;
    task i1.t2;
        static int i = baz;
    endtask

    task i2.t2;
        static int i = baz;
    endtask
endmodule
)";

    const char* file2Content = R"(
module wire_module (input in, output out);
  Iface i2();
  n n2(i2.m);

  assign out = in;


  program driver;
  default clocking cb @(posedge clk);
  default input #1step output #1ns;
  endclocking

  initial begin
  @(rstGen.done);
  ##1;
  data_in <= 8'hAF;
  start <= '1;
  read_mode <= '0;
  $finish;
  end
  endprogram

endmodule

class C;
    int i;
    static int j;
    extern function int foo(int bar, int baz = 1);
endclass
)";

    const char* file3Content = R"(
`define REQUIRED                                                \
    input wire   cmc_clk_p,                                     \
    input wire   cmc_clk_n,
)";

    std::string f1Path = addFile(file1Content).fullPath();
    std::string f2Path = addFile(file2Content).fullPath();
    std::string f3Path = addFile(file3Content).fullPath();

    indexer.startIndexing({"_test*"}, {});

    using GoldenMap = Indexer::IndexMap;
    const auto checkIndexedMap = [](const Indexer::IndexMap& map, const GoldenMap& goldenMap) {
        CHECK(map.size() == goldenMap.size());
        auto expectedIt = goldenMap.begin();
        auto mapIt = map.begin();

        while (expectedIt != goldenMap.end() && mapIt != map.end()) {
            CHECK(expectedIt->first == mapIt->first);
            CHECK(expectedIt->second.toString(expectedIt->first) ==
                  mapIt->second.toString(mapIt->first));
            ++expectedIt;
            ++mapIt;
        }
    };

    SECTION("Macros") {
        GoldenMap expectedMap;
        expectedMap.emplace(
            "REQUIRED", std::move(Indexer::IndexMapEntry::fromMacroData(URI::fromFile(f3Path))));

        checkIndexedMap(indexer.macroMap().getAllEntries(), expectedMap);
    }

    SECTION("Symbols") {
        GoldenMap expectedMap;
        expectedMap.emplace("driver", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                          lsp::SymbolKind::Module, "wire_module",
                                          lsp::LocationUriOnly{URI::fromFile(f2Path)})));
        expectedMap.emplace("C", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                     lsp::SymbolKind::Class, "",
                                     lsp::LocationUriOnly{URI::fromFile(f2Path)})));
        expectedMap.emplace("Iface", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                         lsp::SymbolKind::Interface, "",
                                         lsp::LocationUriOnly{URI::fromFile(f1Path)})));
        expectedMap.emplace("m1", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                      lsp::SymbolKind::Module, "",
                                      lsp::LocationUriOnly{URI::fromFile(f1Path)})));
        expectedMap.emplace("m4", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                      lsp::SymbolKind::Module, "",
                                      lsp::LocationUriOnly{URI::fromFile(f1Path)})));
        expectedMap.emplace("n", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                     lsp::SymbolKind::Module, "",
                                     lsp::LocationUriOnly{URI::fromFile(f1Path)})));
        expectedMap.emplace("wire_module", std::move(Indexer::IndexMapEntry::fromSymbolData(
                                               lsp::SymbolKind::Module, "",
                                               lsp::LocationUriOnly{URI::fromFile(f2Path)})));
        checkIndexedMap(indexer.symbolMap().getAllEntries(), expectedMap);
    }
}
