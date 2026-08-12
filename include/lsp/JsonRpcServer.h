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
#include "util/RequestCancel.h"
#include "util/Timing.h"
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <fmt/format.h>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <rfl/json/write.hpp>
#include <rfl/visit.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
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
        Cancelled,
    };

    Kind kind = Kind::None;
    /// Views into the request still alive in @c handleMessage / worker job.
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
        case HandlerTiming::Kind::Cancelled:
            body = fmt::format("<--- {} {}\n-/-> {} {} (cancelled)", timing.method, timing.id,
                               timing.method, timing.id);
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

inline std::string requestIdToString(const rfl::Variant<int, std::string>& id) {
    return rfl::visit(
        [](auto&& id_) -> std::string {
            using T = std::decay_t<decltype(id_)>;
            if constexpr (std::is_same_v<T, int>)
                return std::to_string(id_);
            else if constexpr (std::is_same_v<T, std::string>)
                return id_;
            else
                static_assert(rfl::always_false_v<T>, "Not all cases were covered.");
        },
        id);
}

inline bool isDocMutationMethod(std::string_view method) {
    return method == "textDocument/didChange" || method == "textDocument/didOpen" ||
           method == "textDocument/didClose" || method == "textDocument/didSave" ||
           method == "workspace/didChangeWatchedFiles";
}
} // namespace

template<typename Impl>
class JsonRpcServer {
public:
    using RpcResult = std::variant<rfl::Generic, RpcError, std::nullopt_t>;

protected:
    /// method name -> request handler
    std::unordered_map<std::string, std::function<rfl::Generic(rfl::Generic)>> requests;

    /// method name -> notification handler
    std::unordered_map<std::string, std::function<void(rfl::Generic)>> notifications;

    /// Cooperative cancel state; safe to touch from the stdin thread and worker.
    RequestCancelState cancelState;

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

    void applyCancelRequest(const RpcRequest& request) {
        if (!request.params.has_value())
            return;
        auto params = rfl::from_generic<CancelParams, rfl::UnderlyingEnums>(request.params.value());
        if (!params)
            return;
        cancelState.cancel(requestIdToString(params->id));
    }

    /// Run the handler with no logging. Caller must hold @c mutex for stdout
    /// sends; format @p timing only after releasing it.
    /// @p request must outlive @p timing.method (and logging after unlock).
    std::variant<rfl::Generic, RpcError, std::nullopt_t> processMessage(const RpcRequest& request,
                                                                        HandlerTiming& timing) {
        timing.method = request.method;

        if (!request.id) {
            // Notification
            if (request.method == "$/cancelRequest") {
                applyCancelRequest(request);
                timing.kind = HandlerTiming::Kind::NotificationOk;
                return std::nullopt;
            }

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
                catch (const RequestCancelledException& e) {
                    timing.error = e.what();
                    timing.kind = HandlerTiming::Kind::NotificationError;
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

        cancelState.beginRequest(std::string(timing.id));
        if (cancelState.isCancelled(std::string(timing.id))) {
            timing.kind = HandlerTiming::Kind::Cancelled;
            cancelState.endRequest();
            return RpcError{.code = static_cast<int>(LSPErrorCodes::RequestCancelled),
                            .message = "Request cancelled"};
        }

        auto it = requests.find(request.method);

        if (it != requests.end()) {
            ScopedElapsedMs elapsed(timing.ms);
            try {
                cancelState.throwIfCancelled();
                rfl::Generic req_response;
                if (request.params.has_value()) {
                    req_response = it->second(request.params.value());
                }
                else {
                    req_response = it->second(rfl::Generic{});
                }
                cancelState.throwIfCancelled();
                timing.kind = HandlerTiming::Kind::RequestOk;
                cancelState.endRequest();
                return req_response;
            }
            catch (const RequestCancelledException& e) {
                timing.error = e.what();
                timing.kind = HandlerTiming::Kind::Cancelled;
                cancelState.endRequest();
                return RpcError{.code = static_cast<int>(LSPErrorCodes::RequestCancelled),
                                .message = e.what()};
            }
            catch (const std::exception& e) {
                timing.error = e.what();
                timing.kind = HandlerTiming::Kind::RequestError;
                cancelState.endRequest();
                return RpcError{.code = 1, .message = e.what()};
            }
        }

        timing.kind = HandlerTiming::Kind::NotFound;
        cancelState.endRequest();
        return std::nullopt;
    }

    struct QueuedMessage {
        RpcRequest req;
        bool sendResponse = true;
        std::shared_ptr<std::promise<RpcResult>> resultPromise;
        std::shared_ptr<std::promise<void>> donePromise;
    };

    void ensureWorker() {
        std::lock_guard lock(m_queueMutex);
        if (m_worker.joinable())
            return;
        m_stop.store(false, std::memory_order_release);
        m_worker = std::thread([this] { workerLoop(); });
    }

    void stopWorker() {
        {
            std::lock_guard lock(m_queueMutex);
            if (!m_worker.joinable())
                return;
            m_stop.store(true, std::memory_order_release);
        }
        // Ensure a paused test gate cannot deadlock join.
        setWorkerGateOpen(true);
        m_queueCv.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }

    void waitForIdle() {
        std::unique_lock lock(m_queueMutex);
        m_idleCv.wait(lock, [this] { return m_queue.empty() && !m_workerBusy; });
    }

    void setWorkerGateOpen(bool open) {
        {
            std::lock_guard lock(m_testGateMutex);
            m_testGateOpen = open;
        }
        m_testGateCv.notify_all();
    }

    void waitForWorkerGate() {
        std::unique_lock lock(m_testGateMutex);
        m_testGateCv.wait(lock, [this] {
            return m_testGateOpen || m_stop.load(std::memory_order_acquire);
        });
    }

    void enqueueMessage(QueuedMessage msg) {
        ensureWorker();
        if (isDocMutationMethod(msg.req.method))
            cancelState.cancelAllPendingAndCurrent();
        if (msg.req.id) {
            // Eligible for $/cancelRequest only while pending or in-flight.
            cancelState.registerPending(requestIdToString(msg.req.id.value()));
        }
        {
            std::lock_guard lock(m_queueMutex);
            m_queue.push(std::move(msg));
        }
        m_queueCv.notify_one();
    }

    void completeQueuedMessage(QueuedMessage& msg, RpcResult result, const HandlerTiming& timing) {
        logHandlerTiming(timing);
        if (msg.resultPromise)
            msg.resultPromise->set_value(std::move(result));
        if (msg.donePromise)
            msg.donePromise->set_value();
    }

    void workerLoop() {
        while (true) {
            QueuedMessage msg;
            {
                std::unique_lock lock(m_queueMutex);
                m_queueCv.wait(lock, [this] {
                    return m_stop.load(std::memory_order_acquire) || !m_queue.empty();
                });
                if (m_stop.load(std::memory_order_acquire) && m_queue.empty())
                    break;
                msg = std::move(m_queue.front());
                m_queue.pop();
                m_workerBusy = true;
            }

            struct BusyGuard {
                JsonRpcServer& self;
                ~BusyGuard() {
                    {
                        std::lock_guard lock(self.m_queueMutex);
                        self.m_workerBusy = false;
                    }
                    self.m_idleCv.notify_all();
                }
            } busyGuard{*this};

            // Test-only gate: allows deterministic pending-cancel coverage.
            waitForWorkerGate();
            if (m_stop.load(std::memory_order_acquire)) {
                // Shutting down after dequeue: finish promises so waiters cannot hang.
                if (msg.req.id)
                    cancelState.dropPending(requestIdToString(msg.req.id.value()));
                HandlerTiming timing;
                timing.method = msg.req.method;
                timing.kind = HandlerTiming::Kind::Ignored;
                completeQueuedMessage(msg, std::nullopt, timing);
                continue;
            }

            HandlerTiming timing;
            RpcResult result = std::nullopt;
            // Own the request so timing.method/id views remain valid after unlock.
            RpcRequest req = std::move(msg.req);
            {
                // Mutex serializes server state + stdout (also taken by WCP).
                // Stdin stays free to read $/cancelRequest and enqueue work.
                std::lock_guard lock(mutex);
                result = processMessage(req, timing);
                if (msg.sendResponse) {
                    std::visit(
                        [&req](auto&& value) {
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
            }
            completeQueuedMessage(msg, std::move(result), timing);
        }
    }

    /// Enqueue @p req for the worker. $/cancelRequest is applied immediately on
    /// this thread so it is never stuck behind the request it cancels.
    void handleMessage(RpcRequest req) {
        if (req.method == "$/cancelRequest") {
            applyCancelRequest(req);
            return;
        }
        enqueueMessage(QueuedMessage{.req = std::move(req)});
    }

    std::string line;
    std::string content;
    /// Serializes handler execution (server state) and stdout; shared with WCP.
    std::mutex mutex;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::condition_variable m_idleCv;
    std::queue<QueuedMessage> m_queue;
    std::thread m_worker;
    std::atomic<bool> m_stop{false};
    bool m_workerBusy = false;

    std::mutex m_testGateMutex;
    std::condition_variable m_testGateCv;
    bool m_testGateOpen = true;

public:
    ~JsonRpcServer() { stopWorker(); }

    /// Test helper: pause the worker after dequeue, before processMessage.
    void pauseWorkerForTest() { setWorkerGateOpen(false); }
    void resumeWorkerForTest() { setWorkerGateOpen(true); }
    void waitForIdleForTest() { waitForIdle(); }

    /// Enqueue and wait until the worker finishes this message.
    void handleMessageAndWait(RpcRequest req) {
        if (req.method == "$/cancelRequest") {
            applyCancelRequest(req);
            return;
        }
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();
        enqueueMessage(QueuedMessage{.req = std::move(req), .donePromise = std::move(done)});
        future.wait();
    }

    /// Enqueue without waiting; returns a future for the RPC result (no stdout write).
    std::future<RpcResult> enqueueForTest(RpcRequest req) {
        auto resultPromise = std::make_shared<std::promise<RpcResult>>();
        auto future = resultPromise->get_future();
        if (req.method == "$/cancelRequest") {
            applyCancelRequest(req);
            resultPromise->set_value(std::nullopt);
            return future;
        }
        enqueueMessage(QueuedMessage{.req = std::move(req),
                                     .sendResponse = false,
                                     .resultPromise = std::move(resultPromise)});
        return future;
    }

    /// Process on the worker without writing to stdout; returns the RPC result.
    /// Used by unit tests to exercise cancel / offload without a real client.
    RpcResult handleMessageForTest(RpcRequest req) { return enqueueForTest(std::move(req)).get(); }

    void run() {
        // Handle initialize first (must finish before advertising readiness).
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
            handleMessageAndWait(std::move(req));
            break;
        }

        // Run until shutdown. Enqueue without waiting so stdin can keep reading
        // cancels / didChange while a heavy request runs on the worker.
        std::string lastMethod;
        do {
            req = readJson<RpcRequest>(line, content);
            lastMethod = req.method;
            handleMessage(std::move(req));
        } while (lastMethod.compare("shutdown") != 0);

        waitForIdle();
        stopWorker();

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
