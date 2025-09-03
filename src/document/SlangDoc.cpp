//------------------------------------------------------------------------------
// SlangDoc.cpp
// Implementation of SlangDoc class
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "document/SlangDoc.h"

#include "document/ShallowAnalysis.h"
#include "lsp/URI.h"
#include "util/Logging.h"
#include <fmt/ranges.h>
#include <stdexcept>

#include "slang/ast/Compilation.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/diagnostics/ExpressionsDiags.h"
#include "slang/diagnostics/LookupDiags.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
namespace server {

using namespace slang;
SlangDoc::SlangDoc(URI uri, SourceManager& sourceManager, Bag options,
                   std::shared_ptr<syntax::SyntaxTree> tree) :
    m_uri(uri), m_sourceManager(sourceManager), m_options(std::move(options)), m_tree(tree) {
    SLANG_ASSERT(tree->getSourceBufferIds().size() == 1);
    m_buffer = tree->getSourceBufferIds()[0];
}

SlangDoc::SlangDoc(URI uri, SourceManager& sourceManager, Bag options, std::string_view text) :
    m_uri(uri), m_sourceManager(sourceManager), m_options(std::move(options)) {
    auto buf = sourceManager.assignText<true>(m_uri.getPath(), text);
    m_buffer = buf.id;
}

SlangDoc::SlangDoc(URI uri, SourceManager& sourceManager, Bag options, BufferID buffer) :
    m_uri(uri), m_sourceManager(sourceManager), m_options(std::move(options)), m_buffer(buffer) {
}

std::shared_ptr<SlangDoc> SlangDoc::fromTree(std::shared_ptr<syntax::SyntaxTree> tree,
                                             SourceManager& sourceManager, const Bag& options) {
    auto uri = URI::fromFile(sourceManager.getFullPath(tree->getSourceBufferIds()[0]));
    return std::make_shared<SlangDoc>(uri, sourceManager, options, tree);
}

std::shared_ptr<SlangDoc> SlangDoc::fromText(const URI& uri, SourceManager& sourceManager,
                                             const Bag& options, std::string_view text) {
    std::string_view path = uri.getPath();
    if (sourceManager.isCached(path)) {
        auto buffer = sourceManager.readSource(path, nullptr).value();
        return std::make_shared<SlangDoc>(uri, sourceManager, options, buffer.id);
    }
    else {
        return std::make_shared<SlangDoc>(uri, sourceManager, options, text);
    }
}

std::shared_ptr<SlangDoc> SlangDoc::open(const URI& uri, SourceManager& sourceManager,
                                         const Bag& options) {
    auto buffer = sourceManager.readSource(uri.getPath(), nullptr).value();
    return std::make_shared<SlangDoc>(uri, sourceManager, options, buffer.id);
}

const std::string_view SlangDoc::getText() const {
    // null terminator is included in data
    return m_sourceManager.getSourceText(m_buffer);
}

std::shared_ptr<syntax::SyntaxTree> SlangDoc::getSyntaxTree() {
    if (!m_tree) {
        auto buf = m_sourceManager.readSource(m_uri.getPath(), nullptr).value();
        // Will read the cached file data if it exists
        m_tree = syntax::SyntaxTree::fromBuffer(buf, m_sourceManager, m_options);
        m_buffer = buf.id;
    }
    else {
        // validate the buffers in the current tree
        SLANG_ASSERT(m_tree->getSourceBufferIds().size() == 1);
        // TODO: validate included files
        if (!m_sourceManager.isValid(m_tree->getSourceBufferIds()[0])) {
            auto buf = m_sourceManager.readSource(m_uri.getPath(), nullptr).value();
            m_tree = syntax::SyntaxTree::fromBuffer(buf, m_sourceManager, m_options);
            m_buffer = buf.id;
        }
    }
    return m_tree;
}

ShallowAnalysis& SlangDoc::getAnalysis() {
    if (!m_analysis || !m_analysis->hasValidBuffers()) {
        std::vector<std::shared_ptr<syntax::SyntaxTree>> trees = {getSyntaxTree()};
        for (const auto& doc : m_dependentDocuments) {
            if (auto depTree = doc->getSyntaxTree()) {
                trees.push_back(depTree);
            }
        }
        m_analysis = std::make_unique<ShallowAnalysis>(m_sourceManager, m_buffer, m_tree, m_options,
                                                       trees);
        INFO("Analyzed {} with tops: {}", m_uri.getPath(),
             fmt::join(m_analysis->getCompilation()->getRoot().topInstances |
                           std::views::transform([](const auto& top) { return top->name; }),
                       ", "));
    }

    return *m_analysis;
}

std::string SlangDoc::getPrevText(const lsp::Position& position) {
    return std::string(
        m_sourceManager.getLine(m_buffer, position.line + 1).substr(0, position.character));
}

bool SlangDoc::textMatches(std::string_view text) {
    // Just compute line offsets to validate UTF-8
    auto bufText = getText();
    if (bufText.size() != text.size() + 1) {
        ERROR("Text size mismatch: have {}, expected {}", bufText.size(), text.size() + 1);
        return false;
    }
    if (std::memcmp(bufText.data(), text.data(), bufText.size()) != 0) {
        ERROR("Text content mismatch");
        return false;
    }
    return true;
}

void SlangDoc::onChange(const std::vector<lsp::TextDocumentContentChangeEvent>& contentChanges) {
    std::string text;
    std::string_view textView = getText();
    std::vector<size_t> lineOffsets;

    // This is typically length 1, but sometimes can be more
    for (auto& change_ : contentChanges) {
        // Only one thread is able to call onchange, so the offsets remain valid without locking
        lineOffsets.clear();
        SourceManager::computeLineOffsets(textView, lineOffsets);
        auto& change = rfl::get<lsp::TextDocumentContentChangePartial>(change_);

        size_t curOffset = 0;

        auto& start = change.range.start;
        auto& end = change.range.end;
        if (start.line >= lineOffsets.size() || end.line >= lineOffsets.size()) {
            throw std::out_of_range("Range out of bounds" + std::to_string(start.line) + "," +
                                    std::to_string(end.line) + "/" +
                                    std::to_string(lineOffsets.size()));
        }
        auto startOffset = lineOffsets[start.line] + start.character;
        auto endOffset = lineOffsets[end.line] + end.character;

        text = std::string(textView.substr(0, startOffset)) + change.text +
               std::string(textView.substr(endOffset));
        textView = text;
    }
    // TODO: use assignBuffer to avoid copy
    m_buffer = m_sourceManager.assignText<true>(m_uri.getPath(), textView).id;

    // Invalidate pointers to old buffer
    m_tree.reset();
    m_analysis.reset();
}

bool isValidShallow(const DiagCode& code) {
    switch (code.getCode()) {
        case diag::IndexOOB.getCode():
        case diag::ScopeIndexOutOfRange.getCode():
            return false;
        default:
            return true;
    }
}

void SlangDoc::issueDiagnosticsTo(DiagnosticEngine& diagEngine) {
    for (auto& diag : getAnalysis().getCompilation()->getAllDiagnostics()) {
        // Only issue diagnostics that belong to this document's syntax tree
        if (m_buffer != m_sourceManager.getFullyOriginalLoc(diag.location).buffer()) {
            continue;
        }
        if (!isValidShallow(diag.code)) {
            // Some diagnostics are not valid for shallow analysis
            continue;
        }
        // Some diags should be ignored with the AllGenerates flag
        diagEngine.issue(diag);
    }
}

} // namespace server
