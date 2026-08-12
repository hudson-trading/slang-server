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
#include <concepts>
#include <cstdint>
#include <fmt/format.h>
#include <functional>
#include <mutex>
#include <optional>
#include <rfl/json/write.hpp>
#include <rfl/visit.hpp>
#include <string>
#include <string_view>
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
    /// Views into the request still alive in @c handleMessage.
    std::string_view method;
    std::string_view id;
    /// Owned: @c exception::what() does not outlive the catch block.
    std::string error;
    /// Backing store when the request id is an int.
    std::string idStorage;
    double ms = 0;
};

inline void logHandlerTiming(const HandlerTiming& timing) {
    if (timing.kind == HandlerTiming::Kind::None)
        return;

    std::string body;
    bool timed = false;
    switch (timing.kind) {
        case HandlerTiming::Kind::None:
            return;
        case HandlerTiming::Kind::NotificationOk:
            body = fmt::format("<--- {}\n---- {}", timing.method, timing.method);
            timed = true;
            break;
        case HandlerTiming::Kind::NotificationError:
            body = fmt::format("<--- {}\n-/-> {} Error: {}", timing.method, timing.method,
                               timing.error);
            timed = true;
            break;
        case HandlerTiming::Kind::RequestOk:
            body = fmt::format("<--- {} {}\n---> {} {}", timing.method, timing.id, timing.method,
                               timing.id);
            timed = true;
            break;
        case HandlerTiming::Kind::RequestError:
            body = fmt::format("<--- {} {}\n-/-> {} {} Error: {}", timing.method, timing.id,
                               timing.method, timing.id, timing.error);
            timed = true;
            break;
        case HandlerTiming::Kind::Ignored:
            body = fmt::format("<-/- {} (ignoring threaded req)", timing.method);
            break;
        case HandlerTiming::Kind::NotFound:
            body = fmt::format("<-/- {} (not found)", timing.method);
            break;
    }

    if (timed)
        fmt::print(stderr, "{} ({:.3f}ms)\n\n", body, timing.ms);
    else
        fmt::print(stderr, "{}\n\n", body);
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
    /// @p request must outlive @p timing.method (and logging after unlock).
    std::variant<rfl::Generic, RpcError, std::nullopt_t> processMessage(const RpcRequest& request,
                                                                        HandlerTiming& timing) {
        timing.method = request.method;

        if (!request.id) {
            // Notification
            auto it = notifications.find(request.method);
            if (it != notifications.end()) {
                ScopedElapsedMs elapsed(timing.ms);
                try {
                    if (request.params.has_value()) {
                        it->second(request.params.value());
                    }
                    else {
                        it->second(std::nullopt);
                    }
                    timing.kind = HandlerTiming::Kind::NotificationOk;
                }
                catch (const std::exception& e) {
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
        rfl::visit(
            [&](auto&& id_) {
                using T = typename std::decay_t<decltype(id_)>;
                if constexpr (std::is_same_v<T, int>) {
                    timing.idStorage = std::to_string(id_);
                    timing.id = timing.idStorage;
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    timing.id = id_;
                }
                else {
                    static_assert(rfl::always_false_v<T>, "Not all cases were covered.");
                }
            },
            request.id.value());

        auto it = requests.find(request.method);

        if (it != requests.end()) {
            ScopedElapsedMs elapsed(timing.ms);
            try {
                rfl::Generic req_response;
                if (request.params.has_value()) {
                    req_response = it->second(request.params.value());
                }
                else {
                    req_response = it->second(rfl::Generic{});
                }
                timing.kind = HandlerTiming::Kind::RequestOk;
                return req_response;
            }
            catch (const std::exception& e) {
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
