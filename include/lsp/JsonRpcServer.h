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
#include <concepts>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <rfl/json/write.hpp>
#include <rfl/visit.hpp>
#include <string>
#include <unordered_map>
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
        if (!request.id) {
            // Notification
            auto it = notifications.find(request.method);
            if (it != notifications.end()) {
                std::cerr << "<--- " << request.method << std::endl;
                // std::cerr << rfl::json::write(request.params, rfl::json::pretty) <<
                // std::endl;
                try {
                    if (request.params.has_value()) {
                        it->second(request.params.value());
                    }
                    else {
                        it->second(std::nullopt);
                    }
                    std::cerr << "---- " << request.method << " (notification finished)"
                              << std::endl;
                }
                catch (const std::exception& e) {
                    std::cerr << "-/-> " << request.method << " Error: " << e.what() << '\n';
                }
            }
            else if (request.method.find("$/") == 0) {
                std::cerr << "<-/- " << request.method << " (ignoring threaded req)" << '\n';
            }
            else {
                std::cerr << "<-/- " << request.method << " (method not found)" << '\n';
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
            try {
                std::cerr << "<--- " << request.method << " " << id << '\n';
                rfl::Generic req_response;
                if (request.params.has_value()) {
                    req_response = it->second(request.params.value());
                }
                else {
                    req_response = it->second(rfl::Generic{});
                }
                std::cerr << "---> " << request.method << " " << id << '\n';
                return req_response;
            }
            catch (const std::exception& e) {
                std::cerr << "-/-> " << request.method << " " << id << " Error: " << e.what()
                          << "\n\n";
                return RpcError{.code = 1, .message = e.what()};
            }
        }
        else {
            std::cerr << "<-/- " << request.method << " (not found)" << '\n';
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
        std::cerr << '\n';
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
