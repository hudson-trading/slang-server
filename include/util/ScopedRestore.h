//------------------------------------------------------------------------------
//! @file ScopedRestore.h
//! @brief Restores a value when leaving the current scope
//!
//! These guards help AST passes temporarily change traversal state while visiting nested nodes.
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include "slang/util/Util.h"

namespace server::utils {

/// Saves a value and restores it when this guard leaves scope.
/// The referenced value must outlive the guard.
template<typename T>
class ScopedRestore {
public:
    explicit ScopedRestore(T& value) : value(value), savedValue(value) {}
    ~ScopedRestore() { std::swap(value, savedValue); }

    ScopedRestore(const ScopedRestore&) = delete;
    ScopedRestore& operator=(const ScopedRestore&) = delete;
    ScopedRestore(ScopedRestore&&) = delete;
    ScopedRestore& operator=(ScopedRestore&&) = delete;

private:
    T& value;
    T savedValue;
};

/// Requires a bool or pointer to initially be false or null and resets it when this guard leaves
/// scope. The referenced value must outlive the guard.
template<typename T>
    requires(std::same_as<T, bool> || std::is_pointer_v<T>)
class ScopedReset {
public:
    explicit ScopedReset(T& value) : value(value) { SLANG_ASSERT(!value); }
    ScopedReset(T& value, T replacement) : ScopedReset(value) { value = std::move(replacement); }
    ~ScopedReset() { value = T{}; }

    ScopedReset(const ScopedReset&) = delete;
    ScopedReset& operator=(const ScopedReset&) = delete;
    ScopedReset(ScopedReset&&) = delete;
    ScopedReset& operator=(ScopedReset&&) = delete;

private:
    T& value;
};

} // namespace server::utils
