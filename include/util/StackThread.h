//------------------------------------------------------------------------------
// StackThread.h
// std::thread-like helper with an explicit stack size (needed on musl).
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

#ifndef _WIN32
#    include <pthread.h>
#endif

namespace lsp {

/// Joinable thread with a configurable stack size on POSIX.
///
/// musl's default pthread stack is ~128KiB, which is too small for CTRE's
/// recursive URI matcher and deep analysis. Main threads typically get 8MiB.
class StackThread {
public:
    static constexpr std::size_t kDefaultStackSize = 8 * 1024 * 1024;

    StackThread() = default;

    template<typename F>
    explicit StackThread(F&& f, std::size_t stackSize = kDefaultStackSize) {
        start(std::forward<F>(f), stackSize);
    }

    StackThread(const StackThread&) = delete;
    StackThread& operator=(const StackThread&) = delete;

    StackThread(StackThread&& other) noexcept { *this = std::move(other); }

    StackThread& operator=(StackThread&& other) noexcept {
        if (this == &other)
            return *this;
        if (joinable())
            join();
#ifdef _WIN32
        m_thread = std::move(other.m_thread);
#else
        m_thread = other.m_thread;
        m_joinable = other.m_joinable;
        other.m_joinable = false;
#endif
        return *this;
    }

    ~StackThread() {
        if (joinable())
            join();
    }

    template<typename F>
    void start(F&& f, std::size_t stackSize = kDefaultStackSize) {
        if (joinable())
            throw std::system_error(std::make_error_code(std::errc::device_or_resource_busy),
                                    "StackThread already started");

#ifdef _WIN32
        (void)stackSize;
        m_thread = std::thread(std::forward<F>(f));
#else
        auto* fn = new std::function<void()>(std::forward<F>(f));
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        // Round up to page size requirements; ignore failure and keep default.
        (void)pthread_attr_setstacksize(&attr, stackSize);
        const int rc = pthread_create(&m_thread, &attr, &StackThread::trampoline, fn);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            delete fn;
            throw std::system_error(rc, std::generic_category(), "pthread_create");
        }
        m_joinable = true;
#endif
    }

    bool joinable() const {
#ifdef _WIN32
        return m_thread.joinable();
#else
        return m_joinable;
#endif
    }

    void join() {
#ifdef _WIN32
        if (m_thread.joinable())
            m_thread.join();
#else
        if (!m_joinable)
            return;
        pthread_join(m_thread, nullptr);
        m_joinable = false;
#endif
    }

private:
#ifndef _WIN32
    static void* trampoline(void* p) {
        std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()>*>(p));
        (*fn)();
        return nullptr;
    }

    pthread_t m_thread{};
    bool m_joinable = false;
#else
    std::thread m_thread;
#endif
};

} // namespace lsp
