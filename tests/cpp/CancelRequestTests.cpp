// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "lsp/JsonRpc.h"
#include "lsp/LspTypes.h"
#include "util/RequestCancel.h"
#include "utils/ServerHarness.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace lsp;

namespace {

using ServerRpcResult = server::SlangServer::RpcResult;

RpcRequest makeRequest(std::string method, int id, rfl::Generic params) {
    return RpcRequest{
        .jsonrpc = "2.0",
        .id = id,
        .method = std::move(method),
        .params = std::move(params),
    };
}

RpcRequest makeCancel(int id) {
    CancelParams params{.id = id};
    return RpcRequest{
        .jsonrpc = "2.0",
        .id = std::nullopt,
        .method = "$/cancelRequest",
        .params = rfl::to_generic<rfl::UnderlyingEnums>(params),
    };
}

RpcRequest makeDidChange(const URI& uri) {
    DidChangeTextDocumentParams params{
        .textDocument = VersionedTextDocumentIdentifier{.version = 2, .uri = uri},
        .contentChanges = {TextDocumentContentChangeEvent{
            TextDocumentContentChangeWholeDocument{.text = "module m; endmodule\n"}}},
    };
    return RpcRequest{
        .jsonrpc = "2.0",
        .id = std::nullopt,
        .method = "textDocument/didChange",
        .params = rfl::to_generic<rfl::UnderlyingEnums>(params),
    };
}

bool isCancelledError(const ServerRpcResult& result) {
    return std::holds_alternative<RpcError>(result) &&
           std::get<RpcError>(result).code == static_cast<int>(LSPErrorCodes::RequestCancelled);
}

} // namespace

TEST_CASE("RequestCancelState_Basic", "[cancel]") {
    RequestCancelState state;
    state.beginRequest("1");
    REQUIRE_NOTHROW(state.throwIfCancelled());

    state.cancel("1");
    REQUIRE_THROWS_AS(state.throwIfCancelled(), RequestCancelledException);

    state.endRequest();
    state.beginRequest("2");
    REQUIRE_NOTHROW(state.throwIfCancelled());
    state.endRequest();
}

TEST_CASE("RequestCancelState_CancelCurrent", "[cancel]") {
    RequestCancelState state;
    state.beginRequest("7");
    state.cancelCurrent();
    REQUIRE_THROWS_AS(state.throwIfCancelled(), RequestCancelledException);
    state.endRequest();
}

TEST_CASE("RequestCancelState_IgnoresUnknownId", "[cancel]") {
    RequestCancelState state;

    // Late / unknown cancel must not poison a future request that reuses the id.
    state.cancel("1");
    state.beginRequest("1");
    REQUIRE_NOTHROW(state.throwIfCancelled());
    state.endRequest();
}

TEST_CASE("RequestCancelState_PendingIdCanBeCancelled", "[cancel]") {
    RequestCancelState state;
    state.registerPending("9");
    state.cancel("9");
    state.beginRequest("9");
    REQUIRE_THROWS_AS(state.throwIfCancelled(), RequestCancelledException);
    state.endRequest();
}

TEST_CASE("CancelRequest_UnknownIdDoesNotPoisonReuse", "[cancel]") {
    ServerHarness server;
    auto doc = server.openFile("cancel_ws.sv", "module top; endmodule\n");
    doc.save();

    // Cancel id 10 before any such request exists — must be ignored.
    server.handleMessageForTest(makeCancel(10));

    WorkspaceSymbolParams params{.query = ""};
    auto result = server.handleMessageForTest(
        makeRequest("workspace/symbol", 10, rfl::to_generic<rfl::UnderlyingEnums>(params)));

    // Must not return -32800; a late/unknown cancel must not poison id reuse.
    REQUIRE(std::holds_alternative<rfl::Generic>(result));
}

TEST_CASE("CancelRequest_PendingRequest_ReturnsCancelled", "[cancel]") {
    ServerHarness server;
    auto doc = server.openFile("pend.sv", "module top; endmodule\n");
    doc.save();

    WorkspaceSymbolParams params{.query = ""};
    auto target = makeRequest("workspace/symbol", 10,
                              rfl::to_generic<rfl::UnderlyingEnums>(params));

    // Pause after dequeue so the request stays pending (registered) until we cancel.
    server.pauseWorkerForTest();
    auto future = server.enqueueForTest(std::move(target));
    server.handleMessageForTest(makeCancel(10));
    server.resumeWorkerForTest();

    REQUIRE(isCancelledError(future.get()));
}

TEST_CASE("RequestCancelState_CancelAllPendingAndCurrent", "[cancel]") {
    RequestCancelState state;
    state.registerPending("1");
    state.registerPending("2");
    state.beginRequest("1");
    state.registerPending("3");

    state.cancelAllPendingAndCurrent();

    REQUIRE_THROWS_AS(state.throwIfCancelled(), RequestCancelledException);
    state.endRequest();

    state.beginRequest("2");
    REQUIRE_THROWS_AS(state.throwIfCancelled(), RequestCancelledException);
    state.endRequest();

    state.beginRequest("3");
    REQUIRE_THROWS_AS(state.throwIfCancelled(), RequestCancelledException);
    state.endRequest();
}

TEST_CASE("CancelRequest_DidChangePreemptsQueuedRequest", "[cancel]") {
    ServerHarness server;
    auto doc = server.openFile("preempt.sv", "module top; logic a; endmodule\n");

    DocumentSymbolParams symParams{.textDocument = TextDocumentIdentifier{.uri = doc.m_uri}};

    // Pause so documentSymbol stays pending; didChange must cancel it without an
    // explicit $/cancelRequest.
    server.pauseWorkerForTest();
    auto future = server.enqueueForTest(makeRequest(
        "textDocument/documentSymbol", 20, rfl::to_generic<rfl::UnderlyingEnums>(symParams)));
    // Enqueue mutation while worker is gated; this marks pending ids cancelled.
    auto didChangeFut = server.enqueueForTest(makeDidChange(doc.m_uri));
    server.resumeWorkerForTest();

    auto symResult = future.get();
    auto changeResult = didChangeFut.get();
    server.waitForIdleForTest();

    REQUIRE(isCancelledError(symResult));
    // Notification result is nullopt / empty — must not be an RPC error.
    REQUIRE_FALSE(std::holds_alternative<RpcError>(changeResult));
}

TEST_CASE("CancelRequest_HandleMessageDoesNotBlockOnCancel", "[cancel]") {
    ServerHarness server;
    auto doc = server.openFile("noblock.sv", "module top; endmodule\n");
    doc.save();

    // Inflate the index so workspace/symbol has something to walk.
    for (int i = 0; i < 40; ++i) {
        auto d = server.openFile(fmt::format("mod_{}.sv", i),
                                 fmt::format("module m_{0}; logic x_{0}; endmodule\n", i));
        d.save();
    }

    WorkspaceSymbolParams params{.query = ""};
    auto heavy = makeRequest("workspace/symbol", 30, rfl::to_generic<rfl::UnderlyingEnums>(params));

    std::atomic<bool> done{false};
    std::thread runner([&] {
        (void)server.handleMessageForTest(std::move(heavy));
        done.store(true);
    });

    // Wait until the heavy request is likely running (or at least queued).
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const auto t0 = std::chrono::steady_clock::now();
    server.handleMessageForTest(makeCancel(30));
    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    runner.join();

    // Cancel must be applied on the calling thread, not stuck behind workspace/symbol.
    REQUIRE(ms < 50.0);
    REQUIRE(done.load());
}

TEST_CASE("CancelRequest_WorkspaceSymbolRespectsCancelFlag", "[cancel]") {
    ServerHarness server;
    for (int i = 0; i < 20; ++i) {
        auto d = server.openFile(fmt::format("sym_{}.sv", i),
                                 fmt::format("module s_{0}; endmodule\n", i));
        d.save();
    }

    // Pause after dequeue so beginRequest has not run yet; cancel while pending,
    // then resume — processMessage must observe the flag and return -32800.
    server.pauseWorkerForTest();
    auto future = server.enqueueForTest(
        makeRequest("workspace/symbol", 42,
                    rfl::to_generic<rfl::UnderlyingEnums>(WorkspaceSymbolParams{.query = ""})));
    server.handleMessageForTest(makeCancel(42));
    server.resumeWorkerForTest();
    REQUIRE(isCancelledError(future.get()));
}
