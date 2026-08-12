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
    void registerPending(const std::string& id) {
        std::lock_guard lock(m_mutex);
        m_pending.insert(id);
    }

    /// Forget a pending id that will never run (e.g. discarded during shutdown).
    void dropPending(const std::string& id) {
        std::lock_guard lock(m_mutex);
        m_pending.erase(id);
        m_cancelled.erase(id);
    }

    void cancel(const std::string& id) {
        std::lock_guard lock(m_mutex);
        const bool known = m_pending.contains(id) || (m_currentId && *m_currentId == id);
        if (!known)
            return;

        m_cancelled.insert(id);
        if (m_currentId && *m_currentId == id)
            m_currentCancelled.store(true, std::memory_order_release);
    }

    /// Cancel whatever the worker is currently executing (used to preempt on didChange).
    void cancelCurrent() {
        std::lock_guard lock(m_mutex);
        if (!m_currentId)
            return;
        m_cancelled.insert(*m_currentId);
        m_currentCancelled.store(true, std::memory_order_release);
    }

    /// Cancel the in-flight request and every still-queued pending request.
    void cancelAllPendingAndCurrent() {
        std::lock_guard lock(m_mutex);
        for (const auto& id : m_pending)
            m_cancelled.insert(id);
        if (m_currentId) {
            m_cancelled.insert(*m_currentId);
            m_currentCancelled.store(true, std::memory_order_release);
        }
    }

    void beginRequest(const std::string& id) {
        std::lock_guard lock(m_mutex);
        m_pending.erase(id);
        m_currentId = id;
        m_currentCancelled.store(m_cancelled.contains(id), std::memory_order_release);
        t_active = this;
    }

    void endRequest() {
        std::lock_guard lock(m_mutex);
        if (m_currentId) {
            m_cancelled.erase(*m_currentId);
            m_currentId.reset();
        }
        m_currentCancelled.store(false, std::memory_order_release);
        if (t_active == this)
            t_active = nullptr;
    }

    bool isCancelled(const std::string& id) const {
        std::lock_guard lock(m_mutex);
        return m_cancelled.contains(id);
    }

    void throwIfCancelled() const {
        if (m_currentCancelled.load(std::memory_order_acquire))
            throw RequestCancelledException();
    }

    /// Cooperative checkpoint for code that does not have a RequestCancelState&.
    /// No-op when no request is active on this thread (e.g. harness direct calls).
    static void throwIfActiveCancelled() {
        if (t_active)
            t_active->throwIfCancelled();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_set<std::string> m_pending;
    std::unordered_set<std::string> m_cancelled;
    std::optional<std::string> m_currentId;
    std::atomic<bool> m_currentCancelled{false};

    /// Worker-thread active cancel state for deep checkpoints without API plumbing.
    static inline thread_local RequestCancelState* t_active = nullptr;
};

} // namespace lsp
