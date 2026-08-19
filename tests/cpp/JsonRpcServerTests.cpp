#include "lsp/JsonRpcServer.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <regex>
#include <stdexcept>

namespace {

class TestJsonRpcServer : public lsp::JsonRpcServer<TestJsonRpcServer> {
public:
    TestJsonRpcServer() {
        registerMethod<std::nullopt_t, int, &TestJsonRpcServer::succeedRequest>("success");
        registerMethod<std::nullopt_t, int, &TestJsonRpcServer::failRequest>("fail");
        registerNotification<std::nullopt_t, &TestJsonRpcServer::succeedNotification>(
            "notification");
        registerNotification<std::nullopt_t, &TestJsonRpcServer::failNotification>(
            "failed-notification");
    }

    int succeedRequest(std::monostate) { return 42; }

    int failRequest(std::monostate) { throw std::runtime_error("failed"); }

    void succeedNotification(std::nullopt_t) {}

    void failNotification(std::nullopt_t) { throw std::runtime_error("notification failed"); }

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
            std::regex(R"(INFO: <--- success 7\nINFO: ---> success 7 \([0-9]+ ms\)\n)")));
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
                R"(INFO: <--- fail request-id\nERROR: -/-> fail request-id \([0-9]+ ms\) Error: failed\n)")));
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
                R"(INFO: <--- notification\nINFO: ---- notification \(notification finished\) \([0-9]+ ms\)\n)")));
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
                R"(INFO: <--- failed-notification\nERROR: -/-> failed-notification \([0-9]+ ms\) Error: notification failed\n)")));
    }
}
