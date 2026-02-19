// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "InlayHintScanner.h"

#include "ServerHarness.h"
#include "util/Converters.h"
#include <catch2/catch_test_macros.hpp>

void InlayHintScanner::scanDocument(DocumentHandle hdl) {
    auto doc = hdl.doc;
    if (!doc) {
        FAIL("Failed to get SlangDoc");
        return;
    }

    // Get inlay hints for the entire document
    // In the future we may want to measure the performance of querying more normal ranges

    std::string data = std::string{doc->getText()};
    auto start = hdl.getLocation(0);
    auto end = hdl.getLocation(data.size() - 1);
    auto range = slang::SourceRange{*start, *end};
    Config::InlayHints config{.portTypes = true};
    auto hints = doc->getAnalysis()->getInlayHints(server::toRange(range, doc->getSourceManager()),
                                                   config);

    // Insert hints into the document text
    size_t size_added = 0;
    for (auto& hint : hints) {
        std::string label = rfl::get<std::string>(hint.label);

        // Handle padding
        std::string prefix = hint.paddingLeft.value_or(false) ? " " : "";
        std::string suffix = hint.paddingRight.value_or(false) ? " " : "";

        auto toAdd = fmt::format("{}/*{}*/{}", prefix, label, suffix);
        auto insertPos = doc->getLocation(hint.position)->offset() + size_added;

        REQUIRE(insertPos <= data.size());

        data.insert(insertPos, toAdd);
        size_added += toAdd.size();
    }

    // Trim trailing whitespace (including null bytes) and ensure exactly one newline at end
    while (!data.empty() && (data.back() == ' ' || data.back() == '\t' || data.back() == '\n' ||
                             data.back() == '\r' || data.back() == '\0')) {
        data.pop_back();
    }
    test.record(data + "\n");
}
