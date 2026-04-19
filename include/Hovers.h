
#include "document/ShallowAnalysis.h"
#include "lsp/LspTypes.h"
#include <optional>
#include <string>

namespace server {

lsp::MarkupContent getHover(const SourceManager& sm, const BufferID docBuffer,
                            const DefinitionInfo& info,
                            const std::optional<std::string>& elaboratedParamValue = {});

}
