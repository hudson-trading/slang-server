// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"
#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <memory>

#include "slang/parsing/Token.h"
#include "slang/text/SourceLocation.h"

using namespace slang;

template<typename ElementT>
class DocumentScanner {
public:
    void scanDocument(const std::string& fileName) {
        ServerHarness server("");

        std::string docPath = (findSlangRoot() / "tests/data" / fileName).string();
        auto hdl = server.openFile(docPath);
        auto doc = hdl.doc;
        if (!doc) {
            FAIL("Failed to get SlangDoc");
            return;
        }
        auto& sm = server.sourceManager();

        // Record the first line
        test.record(hdl.getLine(0));

        SourceLocation prevLoc;

        uint colNum = 0;

        auto data = doc->getText();

        for (uint offset = 0; offset < data.size() - 1; offset++) {
            auto locOpt = hdl.getLocation(offset);
            auto loc = *locOpt;
            auto line = sm.getLineNumber(loc);

            bool newLine = line != sm.getLineNumber(prevLoc);

            // Get current element
            auto currentElement = getElementAt(&hdl, offset);
            bool newElement = currentElement != prevElement;

            // Process element transition
            if (prevElement.has_value() && (newLine || newElement)) {
                processElementTransition(&hdl, sm, offset - 1);
            }

            // Handle new line
            if (offset == 0 || newLine) {
                test.record("\n");
                test.record(hdl.getLine(line));
                colNum = 0;
            }

            // Record marker if needed
            if (currentElement.has_value()) {
                if (newElement) {
                    test.record(std::string(colNum, ' '));
                }
                test.record("^");
            }

            // Update for next iteration
            colNum++;
            prevLoc = loc;
            prevElement = currentElement;
        }
    }

protected:
    GoldenTest test;
    std::optional<ElementT> prevElement = std::nullopt;

    // These methods should be overridden by derived classes
    virtual std::optional<ElementT> getElementAt(DocumentHandle* hdl, uint offset) = 0;
    virtual void processElementTransition(DocumentHandle* hdl, SourceManager& sm, uint offset) = 0;
};

class SyntaxScanner : public DocumentScanner<parsing::Token> {
public:
    SyntaxScanner() : DocumentScanner<parsing::Token>() {}

protected:
    std::optional<parsing::Token> getElementAt(DocumentHandle* hdl, uint offset) override {
        auto doc = hdl->doc;
        auto tok = doc->getTokenAt(slang::SourceLocation(doc->getBuffer(), offset));
        if (!tok) {
            return std::nullopt;
        }
        return *tok;
    }

    void processElementTransition(DocumentHandle* _hdl, SourceManager&, uint) override {
        test.record(fmt::format(" {}\n", toString(prevElement->kind)));
    }
};

class SymbolRefScanner : public DocumentScanner<server::DefinitionInfo> {
public:
    SymbolRefScanner() : DocumentScanner<server::DefinitionInfo>() {}

protected:
    std::optional<server::DefinitionInfo> getElementAt(DocumentHandle* hdl, uint offset) override {
        return hdl->getDefinitionInfoAt(offset);
    }

    void processElementTransition(DocumentHandle* hdl, SourceManager& sm, uint offset) override {
        // Get the current syntax node at the symbol's location
        auto doc = hdl->doc;
        auto tok = doc->getWordTokenAt(slang::SourceLocation(doc->getBuffer(), offset));

        if (tok && prevElement->nameToken.location() == tok->location()) {
            test.record(fmt::format(" Sym {} : {}\n", prevElement->nameToken.valueText(),
                                    toString(prevElement->node->kind)));
        }
        else {
            test.record(fmt::format(" Ref -> "));
            // Print hover, but turn newlines into \n
            auto maybeHover = doc->getAnalysis().getDocHover(hdl->getPosition(offset), true);
            if (!maybeHover) {
                test.record(" No Hover\n");
                return;
            }
            auto hover = rfl::get<lsp::MarkupContent>(maybeHover->contents);
            auto hoverText = hover.value;
            // Make the code blocks more readable
            auto replace = [&](std::string& str, const std::string& from, const std::string& to) {
                size_t start_pos = 0;
                while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                    str.replace(start_pos, from.length(), to);
                    start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
                }
            };
            replace(hoverText, "````systemverilog\n", "`");
            replace(hoverText, "\n````", "`");
            std::string singleLine;
            for (char c : hoverText) {
                if (c == '\n' || c == '\r') {
                    singleLine += "\\n\\";
                }
                else {
                    singleLine += c;
                }
            }
            test.record(singleLine + "\n");
        }
    }
};

TEST_CASE("FindSyntax") {
    /// Find the syntax at each location in the file

    SyntaxScanner scanner;
    scanner.scanDocument("all.sv");
}

TEST_CASE("FindSymbolRef") {
    /// Find the referenced symbol at each location in the file, if any.
    SymbolRefScanner scanner;
    scanner.scanDocument("all.sv");
}

TEST_CASE("FindSymbolRefHdl") {
    /// Find the referenced symbol at each location in the comms test file.

    SymbolRefScanner scanner;
    scanner.scanDocument("hdl_test.sv");
}

TEST_CASE("FindSymbolRefMacro") {
    /// Find the referenced symbol at each location in the macro test file.

    SymbolRefScanner scanner;
    scanner.scanDocument("macro_test.sv");
}
