//------------------------------------------------------------------------------
// JsonRpcServer.h
// Template-based JSON-RPC server implementation with type-safe method registration
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include "JsonRpc.h"
#include "LspTypes.h"
#include "RequestContext.h"
#include "util/Log.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace lsp {

template<typename Impl>
class JsonRpcServer {
protected:
    std::unordered_map<std::string,
                       std::function<rfl::Generic(rfl::Generic, const RequestContext&)>>
        requests;
    std::unordered_map<std::string, std::function<void(rfl::Generic, const RequestContext&)>>
        notifications;

    template<typename P, typename R, auto Method>
    void registerMethod(const std::string& name) {
        constexpr bool acceptsContext =
            std::is_same_v<P, std::nullopt_t>
                ? std::is_invocable_v<decltype(Method), Impl*, std::monostate,
                                      const RequestContext&>
                : std::is_invocable_v<decltype(Method), Impl*, P&, const RequestContext&>;
        if constexpr (acceptsContext)
            cancellableMethods.emplace(name);

        requests[name] = [this](std::optional<rfl::Generic> paramsJson,
                                const RequestContext& ctx) -> rfl::Generic {
            auto getResult = [&]() -> R {
                if constexpr (!std::is_same_v<P, std::nullopt_t>) {
                    auto params = rfl::from_generic<P, rfl::UnderlyingEnums>(paramsJson.value());
                    if (!params)
                        throw std::runtime_error(params.error().what());

                    if constexpr (std::is_invocable_v<decltype(Method), Impl*, P&,
                                                      const RequestContext&>) {
                        return (static_cast<Impl*>(this)->*Method)(params.value(), ctx);
                    }
                    else {
                        return (static_cast<Impl*>(this)->*Method)(params.value());
                    }
                }
                else {
                    if constexpr (std::is_invocable_v<decltype(Method), Impl*, std::monostate,
                                                      const RequestContext&>) {
                        return (static_cast<Impl*>(this)->*Method)(std::monostate{}, ctx);
                    }
                    else {
                        return (static_cast<Impl*>(this)->*Method)(std::monostate{});
                    }
                }
            };

            if constexpr (std::is_same_v<R, std::monostate>) {
                getResult();
                return std::nullopt;
            }
            else {
                return rfl::to_generic<rfl::UnderlyingEnums>(getResult());
            }
        };
    }

    template<typename P, auto Method>
    void registerNotification(const std::string& name) {
        constexpr bool acceptsContext =
            std::is_same_v<P, std::nullopt_t>
                ? std::is_invocable_v<decltype(Method), Impl*, std::nullopt_t,
                                      const RequestContext&>
                : std::is_invocable_v<decltype(Method), Impl*, P&, const RequestContext&>;
        if constexpr (acceptsContext)
            cancellableMethods.emplace(name);

        notifications[name] = [this](std::optional<rfl::Generic> paramsJson,
                                     const RequestContext& ctx) {
            if constexpr (!std::is_same_v<P, std::nullopt_t>) {
                auto params = rfl::from_generic<P, rfl::UnderlyingEnums>(paramsJson.value());
                if (!params)
                    throw std::runtime_error(params.error().what());

                if constexpr (std::is_invocable_v<decltype(Method), Impl*, P&,
                                                  const RequestContext&>) {
                    (static_cast<Impl*>(this)->*Method)(params.value(), ctx);
                }
                else {
                    (static_cast<Impl*>(this)->*Method)(params.value());
                }
            }
            else {
                if constexpr (std::is_invocable_v<decltype(Method), Impl*, std::nullopt_t,
                                                  const RequestContext&>) {
                    (static_cast<Impl*>(this)->*Method)(std::nullopt, ctx);
                }
                else {
                    (static_cast<Impl*>(this)->*Method)(std::nullopt);
                }
            }
        };
    }

    RequestContext createContext(const RpcRequest& request) const {
        return RequestContext(request.method, request.id,
                              cancellableMethods.contains(request.method));
    }

    std::optional<std::string> getDidChangeKey(const RpcRequest& request) const {
        if (request.method != "textDocument/didChange" || !request.params)
            return std::nullopt;

        auto params = rfl::from_generic<DidChangeTextDocumentParams, rfl::UnderlyingEnums>(
            *request.params);
        if (!params)
            return std::nullopt;
        return params->textDocument.uri.str();
    }

    void registerContext(const RpcRequest& request, const RequestContext& ctx) {
        if (!request.id && !ctx.supportsCancellation())
            return;

        std::lock_guard lock(cancellationMutex);
        if (request.id)
            activeRequests.insert_or_assign(*request.id, ctx);

        if (ctx.supportsCancellation()) {
            auto key = getDidChangeKey(request);
            if (!key)
                return;

            auto it = pendingDocumentChanges.find(*key);
            if (it != pendingDocumentChanges.end() && it->second.id() != ctx.id())
                it->second.cancel();
            pendingDocumentChanges.insert_or_assign(std::move(*key), ctx);
        }
    }

    void unregisterContext(const RpcRequest& request, const RequestContext& ctx) {
        if (!request.id && !ctx.supportsCancellation())
            return;

        std::lock_guard lock(cancellationMutex);
        if (request.id) {
            auto it = activeRequests.find(*request.id);
            if (it != activeRequests.end() && it->second.id() == ctx.id())
                activeRequests.erase(it);
        }

        if (ctx.supportsCancellation()) {
            auto key = getDidChangeKey(request);
            if (!key)
                return;

            auto it = pendingDocumentChanges.find(*key);
            if (it != pendingDocumentChanges.end() && it->second.id() == ctx.id())
                pendingDocumentChanges.erase(it);
        }
    }

    void cancelRequest(ID_t rpcId) {
        {
            std::lock_guard lock(cancellationMutex);
            if (auto it = activeRequests.find(rpcId); it != activeRequests.end()) {
                auto target = it->second;
                if (!target.hasStarted() || target.supportsCancellation()) {
                    target.cancel();
                    target.info("<--- $/cancelRequest - cancelling {}", target.method());
                }
                else {
                    target.info("<--- $/cancelRequest - {} does not support cancellation",
                                target.method());
                }
                return;
            }
        }

        RequestContext cancelCtx("$/cancelRequest", std::move(rpcId), false);
        cancelCtx.info("<--- $/cancelRequest - cancel requested but already returned");
    }

    void startRequest(const RequestContext& ctx) {
        std::lock_guard lock(cancellationMutex);
        ctx.throwIfCancelled("before handler");
        ctx.markStarted();
        ctx.info("Started {}", ctx.method());
    }

    std::variant<rfl::Generic, RpcError, std::nullopt_t> processMessage(RpcRequest request,
                                                                        RequestContext ctx = {},
                                                                        bool logStart = true) {
        struct MessageLog {
            explicit MessageLog(RequestContext ctx, bool logStart) :
                ctx(std::move(ctx)), enabled(this->ctx.method() != "$/cancelRequest") {
                if (enabled && logStart)
                    this->ctx.startInfo("<--- {}", this->ctx.method());
            }

            ~MessageLog() {
                if (!enabled)
                    return;

                if (cancellationPoint) {
                    if (ctx.rpcId()) {
                        ctx.info("-/-> {} (request cancelled {})", ctx.method(),
                                 *cancellationPoint);
                    }
                    else {
                        ctx.info("---- {} (notification superseded {})", ctx.method(),
                                 *cancellationPoint);
                    }
                }
                else if (error) {
                    ctx.error("-/-> {} Error: {}", ctx.method(), *error);
                }
                else if (ctx.rpcId()) {
                    ctx.info("---> {}", ctx.method());
                }
                else {
                    ctx.info("---- {} (notification finished)", ctx.method());
                }
            }

            void setError(std::string message) { error = std::move(message); }
            void setCancelled(std::string checkpoint) { cancellationPoint = std::move(checkpoint); }

            RequestContext ctx;
            bool enabled;
            std::optional<std::string> error;
            std::optional<std::string> cancellationPoint;
        };

        if (!ctx)
            ctx = createContext(request);

        if (!request.id) {
            auto it = notifications.find(request.method);
            if (it != notifications.end()) {
                MessageLog messageLog(ctx, logStart);
                try {
                    if (request.params)
                        it->second(*request.params, messageLog.ctx);
                    else
                        it->second(std::nullopt, messageLog.ctx);

                    messageLog.ctx.throwIfCancelled("before completion");
                }
                catch (const RequestCancelled& e) {
                    messageLog.setCancelled(e.what());
                }
                catch (const std::exception& e) {
                    messageLog.setError(e.what());
                }
            }
            else if (request.method.starts_with("$/")) {
                server::logging::warn("<-/- {} (ignoring threaded req)", request.method);
            }
            else {
                server::logging::warn("<-/- {} (method not found)", request.method);
            }
            return std::nullopt;
        }

        auto it = requests.find(request.method);
        if (it == requests.end()) {
            server::logging::warn("<-/- {} (not found)", request.method);
            return std::nullopt;
        }

        MessageLog messageLog(ctx, logStart);
        try {
            startRequest(messageLog.ctx);

            rfl::Generic response;
            if (request.params)
                response = it->second(*request.params, messageLog.ctx);
            else
                response = it->second(rfl::Generic{}, messageLog.ctx);

            messageLog.ctx.throwIfCancelled("before response");
            return response;
        }
        catch (const RequestCancelled& e) {
            messageLog.setCancelled(e.what());
            return RpcError{.code = static_cast<int>(LSPErrorCodes::RequestCancelled),
                            .message = "Request cancelled"};
        }
        catch (const std::exception& e) {
            messageLog.setError(e.what());
            return RpcError{.code = static_cast<int>(ErrorCodes::InternalError),
                            .message = e.what()};
        }
    }

    void handleMessage(RpcRequest request) {
        auto ctx = createContext(request);
        registerContext(request, ctx);
        handleMessage(std::move(request), std::move(ctx));
    }

    void handleMessage(RpcRequest request, RequestContext ctx, bool logStart = true) {
        std::lock_guard<std::mutex> lock(serverStateMutex);
        auto result = processMessage(request, ctx, logStart);
        unregisterContext(request, ctx);
        std::visit(
            [request](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, rfl::Generic>) {
                    sendMessage(RpcResponse{
                        .jsonrpc = "2.0",
                        .id = request.id,
                        .result = value,
                    });
                }
                else if constexpr (std::is_same_v<T, RpcError>) {
                    sendMessage(RpcErrorResponse{
                        .jsonrpc = "2.0",
                        .id = request.id,
                        .error = value,
                    });
                }
            },
            result);
    }

    // Protects server state by serializing LSP and WCP handler execution.
    std::mutex serverStateMutex;

    std::unordered_set<std::string> cancellableMethods;

    // Protects activeRequests and pendingDocumentChanges.
    std::mutex cancellationMutex;

    // Tracks every active request so cancellation can distinguish unsupported routes.
    std::unordered_map<ID_t, RequestContext> activeRequests;

    // Successive /didChange requests should cancel earlier ones doing analysis
    std::unordered_map<std::string, RequestContext> pendingDocumentChanges;

public:
    void run() {
        std::string inputLine;
        std::string inputContent;

        // Init loop
        while (true) {
            auto request = readJson<RpcRequest>(inputLine, inputContent);
            if (!request)
                return;

            if (request->method != "initialize") {
                sendMessage(RpcErrorResponse{
                    .jsonrpc = "2.0",
                    .id = request->id,
                    .error =
                        RpcError{
                            .code = static_cast<int>(ErrorCodes::ServerNotInitialized),
                            .message = "Server not initialized",
                        },
                });
                continue;
            }
            handleMessage(*request);
            server::logging::blankLine();
            break;
        }

        struct QueuedMessage {
            RpcRequest request;
            RequestContext ctx;
        };
        std::deque<QueuedMessage> queue;

        // Protects the queue, worker state, and deferred log separator.
        std::mutex queueMutex;
        std::condition_variable queueCondition;
        bool inputFinished = false;
        bool workerBusy = false;
        bool separatorPending = false;

        // worker- processes messages in order
        auto* logOutput = server::logging::getOutput();
        std::thread worker([&, logOutput] {
            server::logging::setOutput(logOutput);
            while (true) {
                QueuedMessage message;
                {
                    std::unique_lock lock(queueMutex);
                    queueCondition.wait(lock, [&] { return inputFinished || !queue.empty(); });
                    if (queue.empty())
                        return;
                    message = std::move(queue.front());
                    queue.pop_front();
                    workerBusy = true;
                }
                handleMessage(std::move(message.request), std::move(message.ctx), false);
                {
                    std::lock_guard lock(queueMutex);
                    workerBusy = false;
                    if (queue.empty())
                        separatorPending = true;
                }
            }
        });

        auto printPendingSeparatorLocked = [&] {
            if (separatorPending) {
                server::logging::blankLine();
                separatorPending = false;
            }
        };

        auto enqueue = [&](RpcRequest queuedRequest) {
            auto ctx = createContext(queuedRequest);
            registerContext(queuedRequest, ctx);
            {
                std::lock_guard lock(queueMutex);
                printPendingSeparatorLocked();
                ctx.startInfo("<--- {}", ctx.method());
                queue.push_back({std::move(queuedRequest), std::move(ctx)});
            }
            queueCondition.notify_one();
        };

        auto handleCancelRequest = [&](RpcRequest cancellationRequest) {
            {
                std::lock_guard lock(queueMutex);
                printPendingSeparatorLocked();
            }
            processMessage(std::move(cancellationRequest));
            {
                std::lock_guard lock(queueMutex);
                if (!workerBusy && queue.empty())
                    separatorPending = true;
            }
        };

        // Main loop - reads stdin
        bool shutdown = false;
        while (auto request = readJson<RpcRequest>(inputLine, inputContent)) {
            shutdown = request->method == "shutdown";
            if (request->method == "$/cancelRequest")
                handleCancelRequest(std::move(*request));
            else
                enqueue(std::move(*request));
            if (shutdown)
                break;
        }

        // Shutdown loop
        if (shutdown) {
            while (auto request = readJson<RpcRequest>(inputLine, inputContent)) {
                if (request->method == "exit")
                    break;

                if (request->method == "$/cancelRequest") {
                    handleCancelRequest(std::move(*request));
                }
                else {
                    sendMessage(RpcErrorResponse{
                        .jsonrpc = "2.0",
                        .id = request->id,
                        .error =
                            RpcError{
                                .code = static_cast<int>(ErrorCodes::InvalidRequest),
                                .message = "Invalid Request",
                            },
                    });
                }
            }
        }

        {
            std::lock_guard lock(queueMutex);
            inputFinished = true;
        }
        queueCondition.notify_one();
        worker.join();
    }
};
} // namespace lsp
