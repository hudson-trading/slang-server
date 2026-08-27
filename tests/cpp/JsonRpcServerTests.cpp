#include "lsp/JsonRpcServer.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "slang/util/ScopeGuard.h"

namespace {

struct CancelParams {
    lsp::ID_t id;
};

class TestJsonRpcServer : public lsp::JsonRpcServer<TestJsonRpcServer> {
public:
    TestJsonRpcServer() {
        registerMethod<std::nullopt_t, int, &TestJsonRpcServer::succeedRequest>("success");
        registerMethod<std::nullopt_t, int, &TestJsonRpcServer::failRequest>("fail");
        registerMethod<std::nullopt_t, int, &TestJsonRpcServer::contextRequest>("context");
        registerMethod<std::nullopt_t, std::monostate, &TestJsonRpcServer::contextVoidRequest>(
            "context-void");
        registerNotification<std::nullopt_t, &TestJsonRpcServer::succeedNotification>(
            "notification");
        registerNotification<std::nullopt_t, &TestJsonRpcServer::failNotification>(
            "failed-notification");
        registerNotification<std::nullopt_t, &TestJsonRpcServer::contextNotification>(
            "context-notification");
        registerNotification<CancelParams, &TestJsonRpcServer::cancelRequestNotification>(
            "$/cancelRequest");
        registerNotification<lsp::DidChangeTextDocumentParams,
                             &TestJsonRpcServer::didChangeNotification>("textDocument/didChange");
    }

    int succeedRequest(std::monostate) { return 42; }

    int failRequest(std::monostate) { throw std::runtime_error("failed"); }

    int contextRequest(std::monostate, const lsp::RequestContext& ctx) {
        contextRequestCalls++;
        ctx.info("request checkpoint");
        return 42;
    }

    std::monostate contextVoidRequest(std::monostate, const lsp::RequestContext& ctx) {
        ctx.info("void request checkpoint");
        return {};
    }

    void succeedNotification(std::nullopt_t) {}

    void failNotification(std::nullopt_t) { throw std::runtime_error("notification failed"); }

    void contextNotification(std::nullopt_t, const lsp::RequestContext& ctx) {
        ctx.info("notification checkpoint");
    }

    void cancelRequestNotification(const CancelParams& params) { cancelRequest(params.id); }

    void didChangeNotification(const lsp::DidChangeTextDocumentParams&,
                               const lsp::RequestContext& ctx) {
        ctx.throwIfCancelled("before analysis");
    }

    lsp::RequestContext track(const lsp::RpcRequest& request) {
        auto ctx = createContext(request);
        registerContext(request, ctx);
        return ctx;
    }

    void untrack(const lsp::RpcRequest& request, const lsp::RequestContext& ctx) {
        unregisterContext(request, ctx);
    }

    void start(const lsp::RequestContext& ctx) { startRequest(ctx); }

    int contextRequestCalls = 0;

    using JsonRpcServer::processMessage;
};

class LogCapture {
public:
    LogCapture() {
        output = std::tmpfile();
        if (!output)
            throw std::runtime_error("Failed to create stderr capture file");
        original = server::logging::setOutput(output);
    }

    ~LogCapture() {
        server::logging::setOutput(original);
        std::fclose(output);
    }

    std::string str() {
        std::fflush(output);
        const auto size = std::ftell(output);
        if (size < 0 || std::fseek(output, 0, SEEK_SET) != 0)
            throw std::runtime_error("Failed to read captured stderr");

        std::string result(static_cast<size_t>(size), '\0');
        if (!result.empty() && std::fread(result.data(), 1, result.size(), output) != result.size())
            throw std::runtime_error("Failed to read captured stderr");

        std::fseek(output, 0, SEEK_END);
        return result;
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

private:
    FILE* output = nullptr;
    FILE* original = nullptr;
};

} // namespace

TEST_CASE("JSON-RPC request IDs support integers and strings") {
    auto numeric = rfl::json::read<lsp::RpcRequest>(
        R"({"jsonrpc":"2.0","id":17,"method":"test","params":null})");
    REQUIRE(numeric);
    REQUIRE(numeric->id);
    CHECK(std::get<int>(*numeric->id) == 17);

    auto string = rfl::json::read<lsp::RpcRequest>(
        R"({"jsonrpc":"2.0","id":"request-id","method":"test","params":null})");
    REQUIRE(string);
    REQUIRE(string->id);
    CHECK(std::get<std::string>(*string->id) == "request-id");
}

TEST_CASE("JSON-RPC server separates activity batches and exits when input closes") {
    TestJsonRpcServer server;
    LogCapture capture;
    auto frame = [](std::string_view message) {
        return fmt::format("Content-Length: {}\r\n\r\n{}", message.size(), message);
    };
    const std::string initialize =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":null})";
    const std::string notification = R"({"jsonrpc":"2.0","method":"notification","params":null})";
    std::istringstream input(frame(initialize) + frame(notification) + frame(notification));
    const auto originalState = std::cin.rdstate();
    auto* originalBuffer = std::cin.rdbuf(input.rdbuf());
    slang::ScopeGuard restoreInput([&] {
        std::cin.rdbuf(originalBuffer);
        std::cin.clear(originalState);
    });

    server.run();
    const auto output = capture.str();
    const auto separator = output.find("\n\n");
    REQUIRE(separator != std::string::npos);

    const auto firstCompletion = output.find("(notification finished)");
    REQUIRE(firstCompletion != std::string::npos);
    CHECK(separator < firstCompletion);
    CHECK(output.find("(notification finished)", firstCompletion + 1) != std::string::npos);
}

TEST_CASE("JSON-RPC messages log their latency") {
    TestJsonRpcServer server;
    LogCapture capture;

    SECTION("successful request") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = 7,
            .method = "success",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<rfl::Generic>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#7 [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- success\n\[#7 {8}\+\.[0-9]{3}\] INFO: Started success\n\[#7 {8}\+\.[0-9]{3}\] INFO: ---> success\n)")));
    }

    SECTION("failed request") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = std::string("request-id"),
            .method = "fail",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<lsp::RpcError>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#request-id [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- fail\n\[#request-id {8}\+\.[0-9]{3}\] INFO: Started fail\n\[#request-id {8}\+\.[0-9]{3}\] ERROR: -/-> fail Error: failed\n)")));
    }

    SECTION("successful notification") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "notification",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<std::nullopt_t>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[n([0-9]+) [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- notification\n\[n\1 {8}\+\.[0-9]{3}\] INFO: ---- notification \(notification finished\)\n)")));
    }

    SECTION("failed notification") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "failed-notification",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<std::nullopt_t>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[n([0-9]+) [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- failed-notification\n\[n\1 {8}\+\.[0-9]{3}\] ERROR: -/-> failed-notification Error: notification failed\n)")));
    }

    SECTION("handler receives request context") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = 9,
            .method = "context",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<rfl::Generic>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#9 [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- context\n\[#9 {8}\+\.[0-9]{3}\] INFO: Started context\n\[#9 {8}\+\.[0-9]{3}\] INFO: request checkpoint\n\[#9 {8}\+\.[0-9]{3}\] INFO: ---> context\n)")));
    }

    SECTION("notification handler receives request context") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "context-notification",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<std::nullopt_t>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[n([0-9]+) [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- context-notification\n\[n\1 {8}\+\.[0-9]{3}\] INFO: notification checkpoint\n\[n\1 {8}\+\.[0-9]{3}\] INFO: ---- context-notification \(notification finished\)\n)")));
    }

    SECTION("void request handler receives request context") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = 10,
            .method = "context-void",
            .params = std::nullopt,
        });

        CHECK(std::holds_alternative<rfl::Generic>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#10 [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- context-void\n\[#10 {8}\+\.[0-9]{3}\] INFO: Started context-void\n\[#10 {8}\+\.[0-9]{3}\] INFO: void request checkpoint\n\[#10 {8}\+\.[0-9]{3}\] INFO: ---> context-void\n)")));
    }

    SECTION("cancel request marks matching context") {
        lsp::RpcRequest request{
            .jsonrpc = "2.0",
            .id = 11,
            .method = "context",
            .params = std::nullopt,
        };
        auto ctx = server.track(request);

        auto cancelResult = server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "$/cancelRequest",
            .params = rfl::to_generic<rfl::UnderlyingEnums>(CancelParams{.id = 11}),
        });
        CHECK(std::holds_alternative<std::nullopt_t>(cancelResult));
        CHECK(ctx.isCancelled());
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#11 {8}\+\.[0-9]{3}\] INFO: <--- \$/cancelRequest - cancelling context\n)")));

        auto result = server.processMessage(request, ctx);
        REQUIRE(std::holds_alternative<lsp::RpcError>(result));
        CHECK(std::get<lsp::RpcError>(result).code ==
              static_cast<int>(lsp::LSPErrorCodes::RequestCancelled));
        CHECK(server.contextRequestCalls == 0);
        server.untrack(request, ctx);
    }

    SECTION("queued request without context support can be cancelled") {
        lsp::RpcRequest request{
            .jsonrpc = "2.0",
            .id = 14,
            .method = "success",
            .params = std::nullopt,
        };
        auto ctx = server.track(request);

        server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "$/cancelRequest",
            .params = rfl::to_generic<rfl::UnderlyingEnums>(CancelParams{.id = 14}),
        });
        CHECK(ctx.isCancelled());
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#14 {8}\+\.[0-9]{3}\] INFO: <--- \$/cancelRequest - cancelling success\n)")));

        auto result = server.processMessage(request, ctx);
        REQUIRE(std::holds_alternative<lsp::RpcError>(result));
        CHECK(std::get<lsp::RpcError>(result).code ==
              static_cast<int>(lsp::LSPErrorCodes::RequestCancelled));
        server.untrack(request, ctx);
    }

    SECTION("running request without context support ignores cancellation") {
        lsp::RpcRequest request{
            .jsonrpc = "2.0",
            .id = 15,
            .method = "success",
            .params = std::nullopt,
        };
        auto ctx = server.track(request);
        server.start(ctx);

        server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "$/cancelRequest",
            .params = rfl::to_generic<rfl::UnderlyingEnums>(CancelParams{.id = 15}),
        });
        CHECK_FALSE(ctx.isCancelled());
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#15 {8}\+\.[0-9]{3}\] INFO: Started success\n\[#15 {8}\+\.[0-9]{3}\] INFO: <--- \$/cancelRequest - success does not support cancellation\n)")));
        server.untrack(request, ctx);
    }

    SECTION("late cancellation is reported") {
        auto result = server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "$/cancelRequest",
            .params = rfl::to_generic<rfl::UnderlyingEnums>(CancelParams{.id = 404}),
        });

        CHECK(std::holds_alternative<std::nullopt_t>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[#404 {8}\+\.[0-9]{3}\] INFO: <--- \$/cancelRequest - cancel requested but already returned\n)")));
    }

    SECTION("numeric and string request IDs are distinct") {
        lsp::RpcRequest numericRequest{
            .jsonrpc = "2.0",
            .id = 12,
            .method = "context",
            .params = std::nullopt,
        };
        lsp::RpcRequest stringRequest{
            .jsonrpc = "2.0",
            .id = std::string("12"),
            .method = "context",
            .params = std::nullopt,
        };
        auto numericContext = server.track(numericRequest);
        auto stringContext = server.track(stringRequest);

        server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "$/cancelRequest",
            .params = rfl::to_generic<rfl::UnderlyingEnums>(CancelParams{.id = 12}),
        });
        CHECK(numericContext.isCancelled());
        CHECK_FALSE(stringContext.isCancelled());

        server.processMessage({
            .jsonrpc = "2.0",
            .id = std::nullopt,
            .method = "$/cancelRequest",
            .params = rfl::to_generic<rfl::UnderlyingEnums>(CancelParams{.id = std::string("12")}),
        });
        CHECK(stringContext.isCancelled());

        server.untrack(numericRequest, numericContext);
        server.untrack(stringRequest, stringContext);
    }

    SECTION("new didChange cancels pending work for the same document") {
        auto makeChange = [](std::string uri, int version) {
            auto params = lsp::DidChangeTextDocumentParams{
                .textDocument = lsp::VersionedTextDocumentIdentifier{.version = version,
                                                                     .uri = URI(std::move(uri))},
                .contentChanges = {lsp::TextDocumentContentChangeWholeDocument{
                    .text = fmt::format("version {}", version)}},
            };
            return lsp::RpcRequest{
                .jsonrpc = "2.0",
                .id = std::nullopt,
                .method = "textDocument/didChange",
                .params = rfl::to_generic<rfl::UnderlyingEnums>(params),
            };
        };

        auto first = makeChange("file:///same.sv", 1);
        auto second = makeChange("file:///same.sv", 2);
        auto other = makeChange("file:///other.sv", 1);
        auto firstContext = server.track(first);
        auto secondContext = server.track(second);
        auto otherContext = server.track(other);

        CHECK(firstContext.isCancelled());
        CHECK_FALSE(secondContext.isCancelled());
        CHECK_FALSE(otherContext.isCancelled());

        auto result = server.processMessage(first, firstContext);
        CHECK(std::holds_alternative<std::nullopt_t>(result));
        CHECK(std::regex_match(
            capture.str(),
            std::regex(
                R"(\[n([0-9]+) [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO: <--- textDocument/didChange\n\[n\1 {8}\+\.[0-9]{3}\] INFO: ---- textDocument/didChange \(notification superseded before analysis\)\n)")));

        server.untrack(first, firstContext);
        server.untrack(second, secondContext);
        server.untrack(other, otherContext);
    }
}
