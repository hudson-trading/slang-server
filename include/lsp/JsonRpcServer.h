//------------------------------------------------------------------------------
// JsonRpcServer.h
// Template-based JSON-RPC server implementation with type-safe method registration
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "JsonRpc.h"
#include "lsp/LspTypes.h"
#include "rfl/Generic.hpp"
#include "util/Timing.h"
#include <chrono>
#include <concepts>
#include <cstdint>
#include <fmt/format.h>
#include <functional>
#include <mutex>
#include <optional>
#include <rfl/json/write.hpp>
#include <rfl/visit.hpp>
#include <string>
#include <unordered_map>
#include <variant>

namespace lsp {

namespace {
/// Captured under the RPC mutex; formatted only after unlock so logging I/O is
/// not serialized onto the critical path.
struct HandlerTiming {
    enum class Kind : uint8_t {
        None,
        NotificationOk,
        NotificationError,
        RequestOk,
        RequestError,
        Ignored,
        NotFound,
    };

    Kind kind = Kind::None;
    std::string method;
    std::string id;
    std::string error;
    double ms = 0;
};

inline void logHandlerTiming(const HandlerTiming& timing) {
    switch (timing.kind) {
        case HandlerTiming::Kind::None:
            break;
        case HandlerTiming::Kind::NotificationOk:
            fmt::print(stderr, "<--- {}\n---- {} ({:.3f}ms)\n\n", timing.method, timing.method,
                       timing.ms);
            break;
        case HandlerTiming::Kind::NotificationError:
            fmt::print(stderr, "<--- {}\n-/-> {} Error: {} ({:.3f}ms)\n\n", timing.method,
                       timing.method, timing.error, timing.ms);
            break;
        case HandlerTiming::Kind::RequestOk:
            fmt::print(stderr, "<--- {} {}\n---> {} {} ({:.3f}ms)\n\n", timing.method, timing.id,
                       timing.method, timing.id, timing.ms);
            break;
        case HandlerTiming::Kind::RequestError:
            fmt::print(stderr, "<--- {} {}\n-/-> {} {} Error: {} ({:.3f}ms)\n\n", timing.method,
                       timing.id, timing.method, timing.id, timing.error, timing.ms);
            break;
        case HandlerTiming::Kind::Ignored:
            fmt::print(stderr, "<-/- {} (ignoring threaded req)\n\n", timing.method);
            break;
        case HandlerTiming::Kind::NotFound:
            fmt::print(stderr, "<-/- {} (not found)\n\n", timing.method);
            break;
    }
}
} // namespace

template<typename Impl>
class JsonRpcServer {
protected:
    /// method name -> request handler
    std::unordered_map<std::string, std::function<rfl::Generic(rfl::Generic)>> requests;

    /// method name -> notification handler
    std::unordered_map<std::string, std::function<void(rfl::Generic)>> notifications;

    /// Register an rpc method with the given Params, Return, and Method (name)
    template<typename P, typename R, auto Method>
    void registerMethod(const std::string& name) {
        if constexpr (std::is_same_v<R, std::monostate>) {
            requests[name] = [](std::optional<rfl::Generic>) -> rfl::Generic {
                return std::nullopt;
            };
        }
        else {
            requests[name] = [this](std::optional<rfl::Generic> paramsJson) -> rfl::Generic {
                // Deserialize params

                auto getResult = [&]() {
                    if constexpr (!std::is_same_v<P, std::nullopt_t>) {
                        rfl::Result<P> params = rfl::from_generic<P, rfl::UnderlyingEnums>(
                            paramsJson.value());
                        if (!params) {
                            throw std::runtime_error(params.error().what());
                        }
                        return (static_cast<Impl*>(this)->*Method)(params.value());
                    }
                    else {
                        return (static_cast<Impl*>(this)->*Method)(std::monostate{});
                    }
                };
                return rfl::to_generic<rfl::UnderlyingEnums>(getResult());
            };
        }
    }

    /// Register an rpc notification with the given Params and Method (name)
    template<typename P, auto Method>
    void registerNotification(const std::string& name) {
        notifications[name] = [this](std::optional<rfl::Generic> paramsJson) {
            // Call Notification
            if constexpr (!std::is_same_v<P, std::nullopt_t>) {
                // Deserialize params
                rfl::Result<P> params = rfl::from_generic<P, rfl::UnderlyingEnums>(
                    paramsJson.value());
                if (!params) {
                    throw std::runtime_error(params.error().what());
                }
                (static_cast<Impl*>(this)->*Method)(params.value());
            }
            else {
                (static_cast<Impl*>(this)->*Method)(std::nullopt);
            }
        };
    }

    /// Run the handler with no logging. Caller must hold @c mutex for stdout
    /// sends; format @p timing only after releasing it.
    std::variant<rfl::Generic, RpcError, std::nullopt_t> processMessage(RpcRequest request,
                                                                        HandlerTiming& timing) {
        timing.method = request.method;

        if (!request.id) {
            // Notification
            auto it = notifications.find(request.method);
            if (it != notifications.end()) {
                const auto start = std::chrono::steady_clock::now();
                try {
                    if (request.params.has_value()) {
                        it->second(request.params.value());
                    }
                    else {
                        it->second(std::nullopt);
                    }
                    timing.ms = elapsedMs(start);
                    timing.kind = HandlerTiming::Kind::NotificationOk;
                }
                catch (const std::exception& e) {
                    timing.ms = elapsedMs(start);
                    timing.error = e.what();
                    timing.kind = HandlerTiming::Kind::NotificationError;
                }
            }
            else if (request.method.find("$/") == 0) {
                timing.kind = HandlerTiming::Kind::Ignored;
            }
            else {
                timing.kind = HandlerTiming::Kind::NotFound;
            }
            return std::nullopt;
        }

        // Request
        timing.id = rfl::visit(
            [&](auto&& id_) -> std::string {
                using T = typename std::decay_t<decltype(id_)>;
                if constexpr (std::is_same_v<T, int>) {
                    return std::to_string(id_);
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    return id_;
                }
                else {
                    static_assert(rfl::always_false_v<T>, "Not all cases were covered.");
                }
            },
            request.id.value());

        auto it = requests.find(request.method);

        if (it != requests.end()) {
            const auto start = std::chrono::steady_clock::now();
            try {
                rfl::Generic req_response;
                if (request.params.has_value()) {
                    req_response = it->second(request.params.value());
                }
                else {
                    req_response = it->second(rfl::Generic{});
                }
                timing.ms = elapsedMs(start);
                timing.kind = HandlerTiming::Kind::RequestOk;
                return req_response;
            }
            catch (const std::exception& e) {
                timing.ms = elapsedMs(start);
                timing.error = e.what();
                timing.kind = HandlerTiming::Kind::RequestError;
                return RpcError{.code = 1, .message = e.what()};
            }
        }

        timing.kind = HandlerTiming::Kind::NotFound;
        return std::nullopt;
    }

    void handleMessage(RpcRequest req) {
        HandlerTiming timing;
        {
            // Mutex serializes stdout (also taken by WCP). Keep only handler +
            // send under the lock; defer stderr formatting until after unlock.
            std::lock_guard<std::mutex> lock(mutex);
            auto result = processMessage(req, timing);
            std::visit(
                [req](auto&& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, rfl::Generic>) {
                        sendMessage(RpcResponse{
                            .jsonrpc = "2.0",
                            .id = req.id,
                            .result = value,
                        });
                    }
                    else if constexpr (std::is_same_v<T, RpcError>) {
                        sendMessage(RpcErrorResponse{
                            .jsonrpc = "2.0",
                            .id = req.id,
                            .error = value,
                        });
                    }
                },
                result);
        }
        logHandlerTiming(timing);
    }

    std::string line;
    std::string content;
    std::mutex mutex;

public:
    void run() {
        // Handle initialize first
        RpcRequest req;
        while (true) {
            req = readJson<RpcRequest>(line, content);
            if (req.method.compare("initialize") != 0) {
                sendMessage(RpcErrorResponse{.jsonrpc = "2.0",
                                             .id = req.id,
                                             .error = lsp::RpcError{
                                                 .code = -32002,
                                                 .message = "Server not initialized",
                                             }});
                continue;
            }
            handleMessage(req);
            break;
        }

        // Run until shutdown
        do {
            req = readJson<RpcRequest>(line, content);
            handleMessage(req);
        } while (req.method.compare("shutdown") != 0);

        while (true) {
            req = readJson<RpcRequest>(line, content);
            if (req.method.compare("exit") == 0) {
                break;
            }
            sendMessage(RpcErrorResponse{.jsonrpc = "2.0",
                                         .id = req.id,
                                         .error = lsp::RpcError{
                                             .code = -32600,
                                             .message = "Invalid Request",
                                         }});
        }
    }
};

} // namespace lsp
