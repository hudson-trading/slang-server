//------------------------------------------------------------------------------
// ActiveDesignContext.h
// Projects selected full-design instances onto a shallow compilation
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "slang/text/SourceLocation.h"

namespace slang::ast {
class Compilation;
class InstanceSymbol;
class InterfacePortSymbol;
class Symbol;
} // namespace slang::ast

namespace server {

class ServerCompilation;
class ServerCompilationAnalysis;

struct InterfaceConnection {
    // Forwarded ports followed by the source-backed endpoint, excluding the queried port.
    std::vector<const slang::ast::Symbol*> sourcePath;

    // Slang can synthesize this symbol for elaboration, while sourcePath remains navigable.
    const slang::ast::Symbol* resolvedEndpoint = nullptr;
};

InterfaceConnection resolveInterfaceConnection(const slang::ast::InterfacePortSymbol& port);

/// Projects selected full-design instances onto a shallow compilation and maps shallow symbols back
/// to the active instances
class ActiveDesignContext {
public:
    /// Install the selected design's parameter values into a shallow compilation.
    void applyOverrides(slang::ast::Compilation& shallowCompilation) const;

    /// Return the corresponding symbol from the selected full-design instance, if any.
    const slang::ast::Symbol* getDesignSymbol(const slang::ast::Symbol& shallowSymbol) const;

    /// Return the selected full-design connection for an interface port, if any.
    const InterfaceConnection* getInterfaceConnection(
        const slang::ast::InterfacePortSymbol& port) const;

    ServerCompilationAnalysis& getAnalysis() const { return *m_analysis; }

private:
    /// Records one definition's selected instance and resolved interface connections.
    struct DefinitionBinding {
        const slang::ast::InstanceSymbol* instance;
        std::unordered_map<std::string, InterfaceConnection> interfaceConnections;
    };

    /// Associates a source port declaration with its connected design interface instance.
    struct InterfaceInstanceBinding {
        slang::SourceLocation portLocation;
        const slang::ast::InstanceSymbol* designInstance;
    };

    explicit ActiveDesignContext(std::shared_ptr<ServerCompilationAnalysis> analysis) :
        m_analysis(std::move(analysis)) {}

    /// Add the selected full-design data for one definition to this context.
    void bindInstance(const slang::ast::InstanceSymbol& instance);

    /// Owns the full-design compilation and analysis referenced by the bindings.
    std::shared_ptr<ServerCompilationAnalysis> m_analysis;

    /// Stores selected design state by definition name, keeping parameter namespaces independent.
    std::unordered_map<std::string, DefinitionBinding> m_definitions;

    /// Distinguishes synthetic shallow interface instances by their source port location.
    std::vector<InterfaceInstanceBinding> m_interfaceInstances;

    friend class ServerCompilation;
};

} // namespace server
