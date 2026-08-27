//------------------------------------------------------------------------------
// HierarchicalView.h
// Hierarchical view structures for representing SystemVerilog design hierarchy
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once
#include "lsp/LspTypes.h"
#include "util/Converters.h"
#include "util/Formatting.h"
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
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/types/DeclaredType.h"
#include "slang/text/SourceManager.h"

namespace hier {

using namespace server;

static std::optional<bool> getFromExpansion(const SourceRange& range, const SourceManager& sm) {
    if (range.start() && sm.getFullPath(range.start().buffer()).empty())
        return true;
    return std::nullopt;
}

static std::optional<bool> getFromExpansion(const slang::ast::Symbol& symbol,
                                            const SourceManager& sm) {
    if (auto* syntax = symbol.getSyntax())
        return getFromExpansion(syntax->sourceRange(), sm);
    if (symbol.location && sm.getFullPath(symbol.location.buffer()).empty())
        return true;
    return std::nullopt;
}

enum class SlangKind {
    Instance,
    Scope,
    ScopeArray,
    InterfacePort,
    InterfacePortArray,
    Port,
    Param,
    Logic,
    InstanceArray,
    Package,
};

enum class DeclKind {
    Module,
    Interface,
    Program,
    Package,
};

struct Item;

struct Var;
struct Scope;
struct Instance;

// Hierarchy View

using HierItem_t = rfl::Variant<Var, Scope, Instance>;

// TODO: Migrate the Neovim hierarchy and cells views to slang.showHierLocation and
// slang.showModuleDefinition before removing the deprecated location fields below.
struct Item {
    SlangKind kind;
    std::string instName;
    /// @deprecated Use slang.showHierLocation with the item's hierarchy path.
    lsp::Location instLoc;
    std::optional<bool> fromExpansion;
};

struct Var {
    SlangKind kind;
    std::string instName;
    /// @deprecated Use slang.showHierLocation with the item's hierarchy path.
    lsp::Location instLoc;
    std::optional<bool> fromExpansion;
    std::string type;
    std::optional<std::string> value;
};

struct Scope {
    SlangKind kind;
    std::string instName;
    /// @deprecated Use slang.showHierLocation with the item's hierarchy path.
    lsp::Location instLoc;
    std::optional<bool> fromExpansion;
    std::optional<std::string> type;
    std::vector<HierItem_t> children;
};

struct Instance {
    SlangKind kind;
    std::string instName;
    /// @deprecated Use slang.showHierLocation with the instance's hierarchy path.
    lsp::Location instLoc;
    std::optional<bool> fromExpansion;
    std::string declName;
    /// @deprecated Use slang.showModuleDefinition with declName.
    lsp::Location declLoc;
    DeclKind declKind = DeclKind::Module;
    std::vector<HierItem_t> children;
};

// Instances View
struct QualifiedInstance {
    std::string instPath;
    /// @deprecated Use slang.showHierLocation with instPath.
    lsp::Location instLoc;
};
struct InstanceSet {
    std::string declName;
    /// @deprecated Use slang.showModuleDefinition with declName.
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
            .fromExpansion = getFromExpansion(block.getSyntax()->sourceRange(), sm),
            .children = children,
        }));
    }
}

static void handleBlockScope(std::vector<HierItem_t>& result,
                             const slang::ast::GenerateBlockSymbol& block,
                             const SourceManager& sm) {
    if (block.isUninstantiated) {
        // Don't return uninstantiated blocks
        return;
    }
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
        result.push_back(HierItem_t(
            Scope{.kind = SlangKind::ScopeArray,
                  .instName = array.getExternalName(),
                  .instLoc = toLocation(array.getSyntax()->sourceRange(), sm),
                  .fromExpansion = getFromExpansion(array.getSyntax()->sourceRange(), sm),
                  .children = entries}));
    }
}

static Instance toInstance(const slang::ast::InstanceSymbol& inst, const SourceManager& sm,
                           std::string&& nameOverride, bool filled = false) {
    auto sourceRange = inst.getSyntax() ? inst.getSyntax()->sourceRange()
                                        : inst.getDefinition().getSyntax()->sourceRange();
    auto declKind = DeclKind::Module;
    switch (inst.getDefinition().definitionKind) {
        case slang::ast::DefinitionKind::Interface:
            declKind = DeclKind::Interface;
            break;
        case slang::ast::DefinitionKind::Program:
            declKind = DeclKind::Program;
            break;
        case slang::ast::DefinitionKind::Module:
        default:
            break;
    }
    auto children = filled ? getScopeChildren(inst.body, sm) : std::vector<HierItem_t>{};
    return Instance{
        .kind = SlangKind::Instance,
        .instName = nameOverride,
        .instLoc = toLocation(sourceRange, sm),
        .fromExpansion = getFromExpansion(sourceRange, sm),
        .declName = std::string(inst.getDefinition().name),
        .declLoc = toLocation(inst.getDefinition().getSyntax()->sourceRange(), sm),
        .declKind = declKind,
        .children = std::move(children),
    };
}

[[maybe_unused]] static QualifiedInstance toQualifiedInstance(
    const slang::ast::InstanceSymbol& inst, const SourceManager& sm) {
    return QualifiedInstance{
        .instPath = inst.getHierarchicalPath(),
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

[[maybe_unused]] static void handlePackage(std::vector<HierItem_t>& result,
                                           const slang::ast::PackageSymbol& pkg,
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
        .declKind = DeclKind::Package,
        .children = {},
    }));
}

static void handleInstanceArray(std::vector<HierItem_t>& result,
                                const slang::ast::InstanceArraySymbol& array,
                                const SourceManager& sm) {
    std::vector<HierItem_t> elements;

    // Need to handle instance indices manually
    int32_t instanceIdx = array.range.left;
    int8_t step = array.range.isDescending() ? -1 : 1;

    for (const slang::ast::Symbol* block : array.elements) {
        if (auto inst = block->as_if<slang::ast::InstanceSymbol>()) {
            handleInstance(elements, *inst, sm, fmt::format("[{}]", instanceIdx));
            instanceIdx += step;
        }
    }

    if (elements.empty()) {
        return;
    }
    // Extract declaration info from the first array element, and append array
    auto& firstElement = rfl::get<Instance>(elements.front());
    result.push_back(HierItem_t(Instance{
        .kind = SlangKind::InstanceArray,
        .instName = std::string(array.getArrayName()),
        .instLoc = toLocation(array.getSyntax()->sourceRange(), sm),
        .fromExpansion = getFromExpansion(array.getSyntax()->sourceRange(), sm),
        .declName = fmt::format("{}{}", firstElement.declName, array.range.toString()),
        .declLoc = firstElement.declLoc,
        .declKind = firstElement.declKind,
        .children = elements,
    }));
}

static void handleParameter(std::vector<HierItem_t>& result,
                            const slang::ast::ParameterSymbol& param, const SourceManager& sm) {
    result.push_back(HierItem_t(Var{
        .kind = SlangKind::Param,
        .instName = std::string(param.name),
        .instLoc = toLocation(param.getSyntax()->sourceRange(), sm),
        .fromExpansion = getFromExpansion(param, sm),
        .type = getTypeString(param),
        .value = param.getValue().toString(),
    }));
}

static void handleTypeParameter(std::vector<HierItem_t>& result,
                                const slang::ast::TypeParameterSymbol& param,
                                const SourceManager& sm) {
    auto* syntax = param.getSyntax();
    std::optional<std::string> value;
    auto& type = param.targetType.getType();
    if (!type.isError()) {
        value = getTypeString(type);
    }

    result.push_back(HierItem_t(Var{
        .kind = SlangKind::Param,
        .instName = std::string(param.name),
        .instLoc = syntax ? toLocation(syntax->sourceRange(), sm) : toLocation(param.location, sm),
        .fromExpansion = getFromExpansion(param, sm),
        .type = "type",
        .value = std::move(value),
    }));
}

// Includes ports
static void handleValue(std::vector<HierItem_t>& result, const slang::ast::ValueSymbol& val,
                        const SourceManager& sm) {

    result.push_back(HierItem_t(Var{
        .kind = val.getFirstPortBackref() ? SlangKind::Port : SlangKind::Logic,
        .instName = std::string(val.name),
        .instLoc = toLocation(val.getSyntax()->sourceRange(), sm),
        .fromExpansion = getFromExpansion(val.getSyntax()->sourceRange(), sm),
        .type = getTypeString(val),
    }));
}

static SourceRange getSymbolSourceRange(const slang::ast::Symbol& symbol) {
    if (auto* syntax = symbol.getSyntax()) {
        return syntax->sourceRange();
    }
    return SourceRange(symbol.location, symbol.location);
}

static const slang::ast::Scope* getInterfacePortScope(const slang::ast::InterfacePortSymbol& port,
                                                      const slang::ast::InstanceSymbol* ifaceInst) {
    if (!ifaceInst) {
        if (port.interfaceDef) {
            ifaceInst = &slang::ast::InstanceSymbol::createDefault(
                port.getParentScope()->getCompilation(), *port.interfaceDef);
        }
        else {
            return nullptr;
        }
    }

    auto [_, modport] = port.getConnection();
    if (modport) {
        if (auto sym = ifaceInst->body.find(modport->name)) {
            if (auto modportSym = sym->as_if<slang::ast::ModportSymbol>()) {
                return modportSym;
            }
        }
    }
    else if (!port.modport.empty()) {
        if (auto sym = ifaceInst->body.find(port.modport)) {
            if (auto modportSym = sym->as_if<slang::ast::ModportSymbol>()) {
                return modportSym;
            }
        }
    }

    return &ifaceInst->body;
}

static std::vector<HierItem_t> getInterfacePortElementChildren(
    const slang::ast::InterfacePortSymbol& port, const slang::ast::InstanceSymbol* ifaceInst,
    const SourceManager& sm) {
    auto* scope = getInterfacePortScope(port, ifaceInst);
    if (!scope) {
        return {};
    }
    return getScopeChildren(*scope, sm);
}

static std::string getInterfacePortType(const slang::ast::InterfacePortSymbol& port);

static std::vector<HierItem_t> getInterfacePortChildren(const slang::ast::InterfacePortSymbol& port,
                                                        const SourceManager& sm) {
    auto sourceRange = getSymbolSourceRange(port);
    auto type = getInterfacePortType(port);
    auto [connSym, _modport] = port.getConnection();
    if (auto declaredRange = port.getDeclaredRange(); declaredRange && !declaredRange->empty()) {
        std::vector<HierItem_t> elements;
        const auto& range = declaredRange->front();
        int32_t index = range.left;
        int32_t step = range.isDescending() ? -1 : 1;

        auto* connArray = connSym ? connSym->as_if<slang::ast::InstanceArraySymbol>() : nullptr;
        for (size_t i = 0; i < range.width(); i++) {
            const slang::ast::InstanceSymbol* ifaceInst = nullptr;
            if (connArray && i < connArray->elements.size()) {
                ifaceInst = connArray->elements[i]->as_if<slang::ast::InstanceSymbol>();
            }

            auto children = getInterfacePortElementChildren(port, ifaceInst, sm);
            elements.push_back(HierItem_t(Scope{
                .kind = SlangKind::InterfacePort,
                .instName = fmt::format("[{}]", index),
                .instLoc = toLocation(sourceRange, sm),
                .fromExpansion = getFromExpansion(sourceRange, sm),
                .type = type,
                .children = std::move(children),
            }));
            index += step;
        }

        return elements;
    }

    const slang::ast::InstanceSymbol* ifaceInst = connSym
                                                      ? connSym->as_if<slang::ast::InstanceSymbol>()
                                                      : nullptr;
    return getInterfacePortElementChildren(port, ifaceInst, sm);
}

static std::string getInterfacePortType(const slang::ast::InterfacePortSymbol& port) {
    std::string result;
    if (port.interfaceDef) {
        result = std::string(port.interfaceDef->name);
    }
    if (!port.modport.empty()) {
        if (!result.empty()) {
            result += ".";
        }
        result += std::string(port.modport);
    }
    return result;
}

static void handleInterfacePort(std::vector<HierItem_t>& result,
                                const slang::ast::InterfacePortSymbol& port,
                                const SourceManager& sm) {
    auto sourceRange = getSymbolSourceRange(port);
    auto children = getInterfacePortChildren(port, sm);
    auto type = getInterfacePortType(port);
    if (auto declaredRange = port.getDeclaredRange(); declaredRange && !declaredRange->empty()) {
        result.push_back(HierItem_t(Scope{
            .kind = SlangKind::InterfacePortArray,
            .instName = std::string(port.name),
            .instLoc = toLocation(sourceRange, sm),
            .fromExpansion = getFromExpansion(sourceRange, sm),
            .type = type,
            .children = std::move(children),
        }));
        return;
    }

    result.push_back(HierItem_t(Scope{
        .kind = SlangKind::InterfacePort,
        .instName = std::string(port.name),
        .instLoc = toLocation(sourceRange, sm),
        .fromExpansion = getFromExpansion(sourceRange, sm),
        .type = type,
        .children = std::move(children),
    }));
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
        else if (auto typeParam = sym.as_if<slang::ast::TypeParameterSymbol>()) {
            handleTypeParameter(result, *typeParam, sm);
        }
        else if (auto val = sym.as_if<slang::ast::ValueSymbol>()) {
            handleValue(result, *val, sm);
        }
        else if (auto ifacePort = sym.as_if<slang::ast::InterfacePortSymbol>()) {
            handleInterfacePort(result, *ifacePort, sm);
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
