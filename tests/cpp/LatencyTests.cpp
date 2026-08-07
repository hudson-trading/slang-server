// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Timing.h"
#include "utils/ServerHarness.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace {

struct TimedResult {
    std::string name;
    double ms = 0;
    size_t amount = 0; // optional count (tokens, symbols, hints, ...)
};

std::string makeLargeSv(int moduleCount) {
    std::string text;
    text.reserve(static_cast<size_t>(moduleCount) * 280);
    text += "// Generated latency fixture\n";
    for (int i = 0; i < moduleCount; i++) {
        text += fmt::format(R"(
module m{0} #(
    parameter int WIDTH_{0} = 8,
    parameter type T_{0} = logic
);
    typedef logic [WIDTH_{0}-1:0] word_{0}_t;
    word_{0}_t data_{0};
    word_{0}_t addr_{0};

    function automatic word_{0}_t f_{0}(input word_{0}_t x);
        return x + WIDTH_{0};
    endfunction

    initial begin
        word_{0}_t y = f_{0}(data_{0});
        $display("%0d", y);
    end
endmodule
)",
                            i);
    }
    return text;
}

template<typename Fn>
TimedResult timeIt(std::string name, Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    size_t amount = static_cast<size_t>(fn());
    return TimedResult{std::move(name), elapsedMs(start), amount};
}

// Print on success (Catch INFO only shows on failure unless -s).
void report(const TimedResult& r) {
    if (r.amount > 0) {
        fmt::print(stderr, "LATENCY {}: {:.3f}ms (n={})\n", r.name, r.ms, r.amount);
    }
    else {
        fmt::print(stderr, "LATENCY {}: {:.3f}ms\n", r.name, r.ms);
    }
}

double medianMs(std::vector<double> samples) {
    REQUIRE_FALSE(samples.empty());
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

} // namespace

TEST_CASE("RequestLatency_LargeDocument", "[latency]") {
    // ~100 modules → a few thousand lines; large enough to expose whole-document cost,
    // small enough for CI. Soft upper bounds catch catastrophic regressions only.
    constexpr int kModules = 100;
    constexpr int kWarmIters = 5;

    auto text = makeLargeSv(kModules);
    ServerHarness server;

    // didOpen → updateDoc → issueDiagnosticsTo → getAnalysis. That is the true first-touch
    // cost; a later getAnalysis() would only measure a warm cache hit.
    const auto openStart = std::chrono::steady_clock::now();
    auto hdl = server.openFile("latency_large.sv", std::move(text));
    const auto openMs = elapsedMs(openStart);
    REQUIRE(hdl.doc);
    REQUIRE(hdl.doc->hasAnalysis());
    auto open = TimedResult{"open_and_analyze", openMs,
                            hdl.doc->getAnalysis()->syntaxes.collected.size()};
    report(open);
    CHECK(open.ms < 5000.0);

    // Invalidate analysis the way an edit does, then rebuild.
    hdl.append("\n");
    hdl.publishChanges();
    auto rebuild = timeIt("shallow_analysis_rebuild", [&] {
        auto analysis = hdl.doc->getAnalysis();
        REQUIRE(analysis);
        return analysis->syntaxes.collected.size();
    });
    report(rebuild);
    CHECK(rebuild.ms < 5000.0);

    auto analysis = hdl.doc->getAnalysis();
    REQUIRE(analysis);
    const size_t tokenCount = analysis->syntaxes.collected.size();
    fmt::print(stderr, "LATENCY document tokens: {}\n", tokenCount);
    CHECK(tokenCount > 1000);

    // Floor for a full-document token pass (semanticTokens/full will sit above this).
    auto tokenWalk = timeIt("token_walk", [&] {
        size_t touched = 0;
        for (const auto* token : analysis->syntaxes.collected) {
            if (token) {
                touched += token->rawText().size();
            }
        }
        return touched;
    });
    report(tokenWalk);
    CHECK(tokenWalk.ms < 1000.0);

    std::vector<double> symbolSamples;
    symbolSamples.reserve(kWarmIters);
    size_t symbolCount = 0;
    for (int i = 0; i < kWarmIters; i++) {
        auto sample = timeIt("documentSymbol", [&] {
            auto symbols = hdl.getSymbolTree();
            return symbols.size();
        });
        symbolCount = sample.amount;
        symbolSamples.push_back(sample.ms);
    }
    auto symbolsMedian = TimedResult{"documentSymbol_median", medianMs(symbolSamples), symbolCount};
    report(symbolsMedian);
    CHECK(symbolsMedian.ms < 2000.0);

    auto hoverPos = hdl.m_text.find("WIDTH_0");
    REQUIRE(hoverPos != std::string::npos);
    std::vector<double> hoverSamples;
    hoverSamples.reserve(kWarmIters);
    for (int i = 0; i < kWarmIters; i++) {
        auto sample = timeIt("hover", [&] {
            auto hover = hdl.getHoverAt(static_cast<lsp::uint>(hoverPos));
            return hover ? size_t{1} : size_t{0};
        });
        hoverSamples.push_back(sample.ms);
    }
    auto hoverMedian = TimedResult{"hover_median", medianMs(hoverSamples), 0};
    report(hoverMedian);
    CHECK(hoverMedian.ms < 1000.0);

    std::vector<double> hintSamples;
    hintSamples.reserve(kWarmIters);
    size_t hintCount = 0;
    for (int i = 0; i < kWarmIters; i++) {
        auto sample = timeIt("inlayHint", [&] {
            auto hints = hdl.getAllInlayHints();
            return hints.size();
        });
        hintCount = sample.amount;
        hintSamples.push_back(sample.ms);
    }
    auto hintsMedian = TimedResult{"inlayHint_median", medianMs(hintSamples), hintCount};
    report(hintsMedian);
    CHECK(hintsMedian.ms < 2000.0);

    fmt::print(stderr,
               "LATENCY summary: open={:.3f}ms rebuild={:.3f}ms token_walk={:.3f}ms "
               "documentSymbol={:.3f}ms hover={:.3f}ms inlayHint={:.3f}ms tokens={}\n",
               open.ms, rebuild.ms, tokenWalk.ms, symbolsMedian.ms, hoverMedian.ms, hintsMedian.ms,
               tokenCount);
}
