// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <system_error>

#include "slang/util/Util.h"

namespace fs = std::filesystem;

/// Create a directory symlink, skipping tests when symlinks are unsupported or disallowed.
inline void createDirectorySymlinkOrSkip(const fs::path& target, const fs::path& link) {
    std::error_code ec;
    fs::create_directory_symlink(target, link, ec);
    if (ec == std::errc::function_not_supported || ec == std::errc::operation_not_supported ||
        ec == std::errc::operation_not_permitted || ec == std::errc::permission_denied)
        SKIP("Directory symlinks unavailable: " + ec.message());
    if (ec)
        throw fs::filesystem_error("Cannot create directory symlink", target, link, ec);
}

static std::filesystem::path findSlangRoot() {
    auto path = fs::current_path();
    while (!fs::exists(path / "tests")) {
        path = path.parent_path();
        SLANG_ASSERT(!path.empty());
    }
    return path;
}
