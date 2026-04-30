// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "Config.h"
#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"

namespace server {

lsp::MarkupContent getHover(const SourceManager& sm, const BufferID docBuffer,
                            const DefinitionInfo& info, const Config::HoverConfig& hovers);

}
