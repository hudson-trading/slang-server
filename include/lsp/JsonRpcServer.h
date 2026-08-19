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
#include "util/Log.h"
#include <chrono>
#include <concepts>
#include <functional>
#include <mutex>
#include <optional>
#include <rfl/json/write.hpp>
#include <rfl/visit.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace lsp {

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

    std::variant<rfl::Generic, RpcError, std::nullopt_t> processMessage(RpcRequest request) {
        struct MessageLog {
            MessageLog(std::string_view method, std::optional<std::string> id) :
                method(method), id(std::move(id)), start(std::chrono::steady_clock::now()) {
                if (this->id) {
                    server::logging::info("<--- {} {}", method, *this->id);
                }
                else {
                    server::logging::info("<--- {}", method);
                }
            }

            ~MessageLog() {
                const server::logging::Milliseconds latency(std::chrono::steady_clock::now() -
                                                            start);
                if (error) {
                    if (id) {
                        server::logging::error("-/-> {} {} ({}) Error: {}", method, *id, latency,
                                               *error);
                    }
                    else {
                        server::logging::error("-/-> {} ({}) Error: {}", method, latency, *error);
                    }
                }
                else if (id) {
                    server::logging::info("---> {} {} ({})", method, *id, latency);
                }
                else {
                    server::logging::info("---- {} (notification finished) ({})", method, latency);
                }
            }

            void setError(std::string message) { error = std::move(message); }

            std::string_view method;
            std::optional<std::string> id;
            std::optional<std::string> error;
            std::chrono::steady_clock::time_point start;
        };

        if (!request.id) {
            // Notification
            auto it = notifications.find(request.method);
            if (it != notifications.end()) {
                MessageLog messageLog(request.method, std::nullopt);
                try {
                    if (request.params.has_value()) {
                        it->second(request.params.value());
                    }
                    else {
                        it->second(std::nullopt);
                    }
                }
                catch (const std::exception& e) {
                    messageLog.setError(e.what());
                }
            }
            else if (request.method.find("$/") == 0) {
                server::logging::warn("<-/- {} (ignoring threaded req)", request.method);
            }
            else {
                server::logging::warn("<-/- {} (method not found)", request.method);
            }
            return std::nullopt;
        }

        // Request
        std::string id = rfl::visit(
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
            MessageLog messageLog(request.method, std::move(id));
            try {
                rfl::Generic req_response;
                if (request.params.has_value()) {
                    req_response = it->second(request.params.value());
                }
                else {
                    req_response = it->second(rfl::Generic{});
                }
                return req_response;
            }
            catch (const std::exception& e) {
                messageLog.setError(e.what());
                return RpcError{.code = 1, .message = e.what()};
            }
        }
        else {
            server::logging::warn("<-/- {} (not found)", request.method);
        }

        return std::nullopt;
    }

    void handleMessage(RpcRequest req) {
        std::lock_guard<std::mutex> lock(mutex);
        auto result = processMessage(req);
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
        server::logging::blankLine();
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
