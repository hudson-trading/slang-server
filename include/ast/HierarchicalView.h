//------------------------------------------------------------------------------
// HierarchicalView.h
// Hierarchical view structures for representing SystemVerilog design hierarchy
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include "Converters.h"
#include "lsp/LspTypes.h"
#include <fmt/format.h>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "slang/ast/Expression.h"
#include "slang/ast/Scope.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/DeclaredType.h"
#include "slang/ast/types/Type.h"
#include "slang/text/SourceManager.h"

namespace hier {

using namespace server;

enum class SlangKind {
    Instance,
    Scope,
    ScopeArray,
    Port,
    Param,
    Logic,
    InstanceArray,
    Package,
};

struct Item;

struct Var;
struct Scope;
struct Instance;

// Hierarchy View

using HierItem_t = rfl::Variant<Var, Scope, Instance>;

struct Item {
    SlangKind kind;
    std::string instName;
    lsp::Location instLoc;
};

struct Var {
    SlangKind kind;
    std::string instName;
    lsp::Location instLoc;
    std::string type;
    std::optional<std::string> value;
};

struct Scope {
    SlangKind kind;
    std::string instName;
    lsp::Location instLoc;
    std::vector<HierItem_t> children;
};

struct Instance {
    SlangKind kind;
    std::string instName;
    lsp::Location instLoc;
    std::string declName;
    lsp::Location declLoc;
    std::vector<HierItem_t> children;
};

// Instances View
struct QualifiedInstance {
    std::string instPath;
    lsp::Location instLoc;
};
struct InstanceSet {
    std::string declName;
    lsp::Location declLoc;
    size_t instCount;
    // Will be filled if there's only one
    std::optional<QualifiedInstance> inst;
};

static std::vector<HierItem_t> getScopeChildren(const slang::ast::Scope& scope,
                                                const SourceManager& sm);

static std::vector<HierItem_t> getScopeChildren(const slang::ast::Scope& scope,
                                                const SourceManager& sm);

static void handleBlockScope(std::vector<HierItem_t>& result,
                             const slang::ast::GenerateBlockSymbol& block, const SourceManager& sm,
                             std::string&& nameOverride) {
    // Recurse into subscopes
    auto children = getScopeChildren(block, sm);

    // By default, don't return empty scopes
    if (!children.empty()) {
        result.push_back(HierItem_t(Scope{
            .kind = SlangKind::Scope,
            .instName = nameOverride,
            .instLoc = toLocation(block.getSyntax()->sourceRange(), sm),
            .children = children,
        }));
    }
}

static void handleBlockScope(std::vector<HierItem_t>& result,
                             const slang::ast::GenerateBlockSymbol& block,
                             const SourceManager& sm) {
    handleBlockScope(result, block, sm, block.getExternalName());
}

static void handleBlockScopeArray(std::vector<HierItem_t>& result,
                                  const slang::ast::GenerateBlockArraySymbol& array,
                                  const SourceManager& sm) {
    std::vector<HierItem_t> entries;

    for (const slang::ast::GenerateBlockSymbol* block : array.entries) {
        handleBlockScope(entries, *block, sm, fmt::format("[{}]", block->constructIndex));
    }

    // Don't return empty arrays
    if (!entries.empty()) {
        result.push_back(
            HierItem_t(Scope{.kind = SlangKind::ScopeArray,
                             .instName = array.getExternalName(),
                             .instLoc = toLocation(array.getSyntax()->sourceRange(), sm),
                             .children = entries}));
    }
}

static Instance toInstance(const slang::ast::InstanceSymbol& inst, const SourceManager& sm,
                           std::string&& nameOverride, bool filled = false) {
    return Instance{
        .kind = SlangKind::Instance,
        .instName = nameOverride,
        .instLoc = toLocation(inst.getSyntax() ? inst.getSyntax()->sourceRange()
                                               : inst.getDefinition().getSyntax()->sourceRange(),
                              sm),
        .declName = std::string(inst.getDefinition().name),
        .declLoc = toLocation(inst.getDefinition().getSyntax()->sourceRange(), sm),
        .children = filled ? getScopeChildren(inst.body, sm) : std::vector<HierItem_t>{},
    };
}

static Instance toInstance(const slang::ast::InstanceSymbol& inst, const SourceManager& sm,
                           bool filled = false) {
    return toInstance(inst, sm, std::string(inst.name), filled);
}

static QualifiedInstance toQualifiedInstance(const slang::ast::InstanceSymbol& inst,
                                             const SourceManager& sm) {
    auto hierPath = inst.getHierarchicalPath();
    return QualifiedInstance{
        .instPath = hierPath,
        .instLoc = toLocation(inst.getSyntax() ? inst.getSyntax()->sourceRange()
                                               : inst.getDefinition().getSyntax()->sourceRange(),
                              sm),
    };
}

static void handleInstance(std::vector<HierItem_t>& result, const slang::ast::InstanceSymbol& inst,
                           const SourceManager& sm, std::string&& nameOverride,
                           bool filled = false) {
    result.push_back(HierItem_t(toInstance(inst, sm, std::move(nameOverride), filled)));
}

static void handleInstance(std::vector<HierItem_t>& result, const slang::ast::InstanceSymbol& inst,
                           const SourceManager& sm, bool filled = false) {
    handleInstance(result, inst, sm, std::string(inst.name), filled);
}

static void handlePackage(std::vector<HierItem_t>& result, const slang::ast::PackageSymbol& pkg,
                          const SourceManager& sm) {

    auto syntax = pkg.getSyntax();
    if (syntax == nullptr) {
        return;
    }
    auto loc = toLocation(syntax->sourceRange(), sm);
    result.push_back(HierItem_t(Instance{
        .kind = SlangKind::Package,
        .instName = std::string(pkg.name),
        .instLoc = loc,
        .declName = std::string(pkg.name),
        .declLoc = loc,
        .children = {},
    }));
}

static void handleInstanceArray(std::vector<HierItem_t>& result,
                                const slang::ast::InstanceArraySymbol& array,
                                const SourceManager& sm) {
    std::vector<HierItem_t> elements;

    // Need to handle instance indices manually
    // Use isLittleEndian to determine whether indexing high->low or vice versa
    int32_t instanceIdx = array.range.left;
    int8_t step = array.range.isLittleEndian() ? -1 : 1;

    for (const slang::ast::Symbol* block : array.elements) {
        if (auto inst = block->as_if<slang::ast::InstanceSymbol>()) {
            handleInstance(elements, *inst, sm, fmt::format("[{}]", instanceIdx));
            instanceIdx += step;
        }
    }

    if (!elements.empty()) {
        std::string declName;
        lsp::Location declLoc;

        // Extract declaration info from the array elements
        rfl::visit(
            [&](auto&& x) {
                using sym_t = typename std::decay_t<decltype(x)>;
                if constexpr (std::is_same<sym_t, Instance>()) {
                    declName = fmt::format("{}{}", x.declName, array.range.toString());
                    declLoc = x.declLoc;
                }
            },
            elements.front());

        result.push_back(HierItem_t(Instance{
            .kind = SlangKind::InstanceArray,
            .instName = std::string(array.getArrayName()),
            .instLoc = toLocation(array.getSyntax()->sourceRange(), sm),
            .declName = declName,
            .declLoc = declLoc,
            .children = elements,
        }));
    }
}

static void handleParameter(std::vector<HierItem_t>& result,
                            const slang::ast::ParameterSymbol& param, const SourceManager& sm) {
    result.push_back(HierItem_t(Var{
        .kind = SlangKind::Param,
        .instName = std::string(param.name),
        .instLoc = toLocation(param.getSyntax()->sourceRange(), sm),
        // the type of params isn't really relevant- they're mostly ints.
        // TODO: have enums print the string value, rather than the int value
        .type = param.getType().toString(),
        .value = param.getValue().toString(),
    }));
}

static void handlePort(std::vector<HierItem_t>& result, const slang::ast::PortSymbol& port,
                       const SourceManager& sm) {
    auto declType = port.getDeclaredType();
    auto typeStr = declType ? declType->getType().toString() : port.getType().toString();
    result.push_back(HierItem_t(Var{
        .kind = SlangKind::Port,
        .instName = std::string(port.name),
        .instLoc = toLocation(port.getSyntax()->sourceRange(), sm),
        .type = fmt::format("{} {}", portString(port.direction), typeStr),
    }));
}

static void handleValue(std::vector<HierItem_t>& result, const slang::ast::ValueSymbol& val,
                        const SourceManager& sm) {

    // Ignore if the current symbol has already been added as a Port
    bool is_port = false;
    if (!result.empty()) {
        rfl::visit(
            [&](auto&& x) {
                if (x.kind == SlangKind::Port && x.instName == val.name) {
                    is_port = true;
                }
            },
            result.back());
    }

    if (!is_port) {
        result.push_back(HierItem_t(Var{
            .kind = SlangKind::Logic,
            .instName = std::string(val.name),
            .instLoc = toLocation(val.getSyntax()->sourceRange(), sm),
            .type = val.getType().toString(),
        }));
    }
}

static std::vector<HierItem_t> getScopeChildren(const slang::ast::Scope& scope,
                                                const SourceManager& sm) {
    std::vector<HierItem_t> result;
    for (auto& sym : scope.members()) {
        if (auto inst = sym.as_if<slang::ast::InstanceSymbol>()) {
            handleInstance(result, *inst, sm);
        }
        else if (auto param = sym.as_if<slang::ast::ParameterSymbol>()) {
            handleParameter(result, *param, sm);
        }
        else if (auto port = sym.as_if<slang::ast::PortSymbol>()) {
            handlePort(result, *port, sm);
        }
        else if (auto val = sym.as_if<slang::ast::ValueSymbol>()) {
            handleValue(result, *val, sm);
        }
        else if (auto block = sym.as_if<slang::ast::GenerateBlockSymbol>()) {
            handleBlockScope(result, *block, sm);
        }
        else if (auto block = sym.as_if<slang::ast::GenerateBlockArraySymbol>()) {
            handleBlockScopeArray(result, *block, sm);
        }
        else if (auto instArray = sym.as_if<slang::ast::InstanceArraySymbol>()) {
            handleInstanceArray(result, *instArray, sm);
        }
    }

    return result;
}

} // namespace hier
