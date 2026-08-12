//------------------------------------------------------------------------------
// RequestCancel.h
// Cooperative cancellation for in-flight JSON-RPC requests.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace lsp {

/// Thrown by handlers when $/cancelRequest (or a preempting doc mutation) wins.
class RequestCancelledException : public std::runtime_error {
public:
    RequestCancelledException() : std::runtime_error("Request cancelled") {}
};

/// Tracks pending / in-flight request ids and which of those have been cancelled.
///
/// Cancel notifications are applied on the stdin thread without waiting for the
/// worker; the worker observes @c throwIfCancelled() at cooperative checkpoints.
///
/// Unknown or already-finished ids are ignored so a late $/cancelRequest cannot
/// poison a future request that reuses the same id.
class RequestCancelState {
public:
    /// Record that @p id has been enqueued and is eligible for cancel.
    void registerPending(const std::string& id);

    /// Forget a pending id that will never run (e.g. discarded during shutdown).
    void dropPending(const std::string& id);

    void cancel(const std::string& id);

    /// Cancel whatever the worker is currently executing.
    void cancelCurrent();

    /// Cancel the in-flight request and every still-queued pending request.
    void cancelAllPendingAndCurrent();

    void beginRequest(const std::string& id);
    void endRequest();

    bool isCancelled(const std::string& id) const;

    void throwIfCancelled() const;

    /// Cooperative checkpoint for code that does not have a RequestCancelState&.
    /// No-op when no request is active on this thread (e.g. harness direct calls).
    static void throwIfActiveCancelled();

private:
    void installActive();
    void clearActive();

    mutable std::mutex m_mutex;
    std::unordered_set<std::string> m_pending;
    std::unordered_set<std::string> m_cancelled;
    std::optional<std::string> m_currentId;
    std::atomic<bool> m_currentCancelled{false};
};

} // namespace lsp
