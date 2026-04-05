#pragma once

#include "LspTypes.h"

namespace lsp {

struct InactiveRegionsClientCapabilities {
    std::optional<bool> inactiveRegions;
};

struct ExperimentalClientCapabilities {
    std::optional<InactiveRegionsClientCapabilities> inactiveRegions;
};

struct InactiveRegionsParams {
    URI uri;
    std::vector<Range> regions;
};

} // namespace lsp
