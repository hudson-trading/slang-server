// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "GoldenTest.h"
#include "lsp/LspTypes.h"
#include "rfl/visit.hpp"
#include <map>
#include <string>

class DocumentHandle;

class InlayHintScanner {
public:
    void scanDocument(DocumentHandle hdl);

private:
    GoldenTest test;
};
