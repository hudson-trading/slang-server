//------------------------------------------------------------------------------
// ServerCompilation.h
// Server compilation class that tracks document dependencies
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "HierarchicalView.h"
#include "InstanceIndexer.h"
#include "document/SlangDoc.h"
#include <memory>
#include <vector>

#include "slang/util/Bag.h"

namespace server {
using namespace slang;

/// @brief A server compilation that is a thin wrapper around a slang compilation, and can be
/// updated as changes come in. Updates to the documents trigger recompilation, but not a reparse of
/// all the trees. Trees can be added if new modules are added, but will not be removed.
class ServerCompilation {
public:
    std::unique_ptr<slang::ast::Compilation> comp;
    /// @brief Constructs a new ServerCompilation instance
    /// @param documents Vector of weak pointers to SlangDocuments this compilation is based on
    /// @param options Copy of the options bag for this compilation
    ServerCompilation(std::vector<std::shared_ptr<SlangDoc>> documents, Bag options,
                      SourceManager& sourceManager);

    ~ServerCompilation() = default;

    /// Update the compilation based by requesting all syntax trees from the documents
    void refresh();

    InstanceIndexer& getInstances() { return m_instances; }

    /// Get instances by module; Used for the 'instances' view. Only contains the module name and
    /// count
    std::vector<hier::InstanceSet> getScopesByModule();

    /// Get instances of a specific module
    std::vector<hier::QualifiedInstance> getInstancesOfModule(const std::string& moduleName);

    /// Retrun the children of the scope at the given hierarchical path
    std::vector<hier::HierItem_t> getScope(const std::string& hierPath);

private:
    /// The Slang documents this compilation is based on
    std::vector<std::shared_ptr<SlangDoc>> m_documents;

    /// Copy of compilation options
    Bag m_options;

    /// Index of buffer -> definitions and definition -> instances given a compilation. Used for
    /// navigating a compilation
    InstanceIndexer m_instances;

    /// Reference to the source manager for this compilation,
    /// owned by the driver
    SourceManager& m_sourceManager;
};

} // namespace server
