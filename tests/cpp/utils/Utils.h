// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>

#include "slang/util/Util.h"

namespace fs = std::filesystem;

static std::filesystem::path findSlangRoot() {
    auto path = fs::current_path();
    while (!fs::exists(path / "tests")) {
        path = path.parent_path();
        SLANG_ASSERT(!path.empty());
    }
    return path;
}
