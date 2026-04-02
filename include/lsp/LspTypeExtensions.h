#pragma once

#include "LspTypes.h"

namespace lsp {

struct InactiveRegionsClientCapabilities {
    std::optional<bool> inactiveRegions;
};

struct ExperimentalClientCapabilities {
    std::optional<InactiveRegionsClientCapabilities> inactiveRegions;
};

} // namespace lsp