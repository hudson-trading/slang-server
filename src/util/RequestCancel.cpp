//------------------------------------------------------------------------------
// RequestCancel.cpp
// Cooperative cancellation for in-flight JSON-RPC requests.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "util/RequestCancel.h"

namespace lsp {
namespace {
// Single definition for all TUs — a header `thread_local` is unsafe across DSO/musl.
thread_local RequestCancelState* g_activeCancelState = nullptr;
} // namespace

void RequestCancelState::registerPending(const std::string& id) {
    std::lock_guard lock(m_mutex);
    m_pending.insert(id);
}

void RequestCancelState::dropPending(const std::string& id) {
    std::lock_guard lock(m_mutex);
    m_pending.erase(id);
    m_cancelled.erase(id);
}

void RequestCancelState::cancel(const std::string& id) {
    std::lock_guard lock(m_mutex);
    const bool known = m_pending.contains(id) || (m_currentId && *m_currentId == id);
    if (!known)
        return;

    m_cancelled.insert(id);
    if (m_currentId && *m_currentId == id)
        m_currentCancelled.store(true, std::memory_order_release);
}

void RequestCancelState::cancelCurrent() {
    std::lock_guard lock(m_mutex);
    if (!m_currentId)
        return;
    m_cancelled.insert(*m_currentId);
    m_currentCancelled.store(true, std::memory_order_release);
}

void RequestCancelState::cancelAllPendingAndCurrent() {
    std::lock_guard lock(m_mutex);
    for (const auto& id : m_pending)
        m_cancelled.insert(id);
    if (m_currentId) {
        m_cancelled.insert(*m_currentId);
        m_currentCancelled.store(true, std::memory_order_release);
    }
}

void RequestCancelState::beginRequest(const std::string& id) {
    std::lock_guard lock(m_mutex);
    m_pending.erase(id);
    m_currentId = id;
    m_currentCancelled.store(m_cancelled.contains(id), std::memory_order_release);
    installActive();
}

void RequestCancelState::endRequest() {
    std::lock_guard lock(m_mutex);
    if (m_currentId) {
        m_cancelled.erase(*m_currentId);
        m_currentId.reset();
    }
    m_currentCancelled.store(false, std::memory_order_release);
    clearActive();
}

bool RequestCancelState::isCancelled(const std::string& id) const {
    std::lock_guard lock(m_mutex);
    return m_cancelled.contains(id);
}

void RequestCancelState::throwIfCancelled() const {
    if (m_currentCancelled.load(std::memory_order_acquire))
        throw RequestCancelledException();
}

void RequestCancelState::throwIfActiveCancelled() {
    if (g_activeCancelState)
        g_activeCancelState->throwIfCancelled();
}

void RequestCancelState::installActive() {
    g_activeCancelState = this;
}

void RequestCancelState::clearActive() {
    if (g_activeCancelState == this)
        g_activeCancelState = nullptr;
}

} // namespace lsp
