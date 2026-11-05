// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "debug/WorkspaceScan.h"

#include "SlangServer.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <tuple>

#include "slang/util/ScopeGuard.h"

namespace server {
namespace {

class ScanClient final : public SlangLspClient {
public:
    std::unordered_map<URI, std::vector<lsp::Diagnostic>> diagnostics;
    std::vector<lsp::ShowMessageParams> messages;

    void onWindowShowMessage(const lsp::ShowMessageParams& message) override {
        messages.push_back(message);
    }
    void onDocPublishDiagnostics(const lsp::PublishDiagnosticsParams& params) override {
        diagnostics.insert_or_assign(params.uri, params.diagnostics);
    }
    void setConfig(const Config&) override {}
    std::monostate getClientRegisterCapability(const lsp::RegistrationParams&) override {
        return {};
    }
    void onTextDocumentInactiveRegions(const lsp::InactiveRegionsParams&) override {}
    void onWorkspaceInlayHintRefresh(std::monostate) override {}
    void onWorkspaceCodeLensRefresh(std::monostate) override {}
    void onActiveInstanceChanged(const ActivateInstanceParams&) override {}
};

} // namespace

DebugWorkspaceScanResult debugScanWorkspace(const std::filesystem::path& targetPath,
                                            const std::filesystem::path& workspacePath) {
    namespace fs = std::filesystem;
    DebugWorkspaceScanResult report;
    ScanClient client;
    try {
        auto target = fs::canonical(targetPath);
        auto workspace = fs::canonical(
            workspacePath.empty() ? (fs::is_directory(target) ? target : target.parent_path())
                                  : workspacePath);
        report.target = target.generic_string();
        report.workspace = workspace.generic_string();
        if (!fs::is_directory(workspace))
            throw std::runtime_error("Workspace must be a directory");
        auto relative = target.lexically_relative(workspace);
        if (relative.empty() || *relative.begin() == "..")
            throw std::runtime_error("Target must be inside the workspace");

        auto isHdl = [](const fs::path& path) {
            auto extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return extension == ".sv" || extension == ".svh" || extension == ".v" ||
                   extension == ".vh";
        };
        std::vector<fs::path> files;
        if (fs::is_regular_file(target)) {
            if (isHdl(target))
                files.push_back(target);
        }
        else {
            for (fs::recursive_directory_iterator it(target), end; it != end; ++it) {
                if (it->is_symlink() || it->path().filename() == ".git") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() && isHdl(it->path()))
                    files.push_back(it->path());
            }
        }
        std::ranges::sort(files);
        report.summary.filesSelected = files.size();
        if (files.empty())
            throw std::runtime_error("No HDL files selected");

        auto previousDirectory = fs::current_path();
        slang::ScopeGuard restoreDirectory([&] {
            std::error_code ec;
            fs::current_path(previousDirectory, ec);
        });
        fs::current_path(workspace);
        SlangServer server(client);
        auto initialized = server.getInitialize({
            .rootUri = URI::fromFile(workspace),
            .capabilities = {.general =
                                 lsp::GeneralClientCapabilities{
                                     .positionEncodings =
                                         std::vector<lsp::PositionEncodingKind>{"utf-8"}}},
        });
        report.serverInfo = initialized.serverInfo;
        server.onInitialized({});
        for (const auto& path : files) {
            auto uri = URI::fromFile(path);
            bool opened = false;
            try {
                std::ifstream input(path, std::ios::binary);
                if (!input)
                    throw std::runtime_error("Cannot open source file");
                std::string source(std::istreambuf_iterator<char>{input}, {});
                if (input.bad())
                    throw std::runtime_error("Cannot read source file");
                server.onDocDidOpen({.textDocument = {.uri = uri,
                                                      .languageId = "systemverilog",
                                                      .version = 1,
                                                      .text = std::move(source)}});
                opened = true;
                DebugScannedDocument document{
                    .result = server.debugScanDocument({uri}),
                    .path = path.lexically_relative(workspace).generic_string(),
                    .compilerDiagnostics = client.diagnostics[uri],
                };
                std::erase_if(document.compilerDiagnostics, [](const auto& diagnostic) {
                    return diagnostic.source == "slang-server-debug";
                });
                report.documents.push_back(std::move(document));
                std::cerr << "Scanned " << report.documents.size() << '/' << files.size() << ' '
                          << path.lexically_relative(workspace).generic_string() << '\n';
            }
            catch (const std::exception& error) {
                report.errors.push_back({path.generic_string(), error.what()});
            }
            if (opened)
                server.onDocDidClose({.textDocument = {uri}});
            client.diagnostics.clear();
        }
        server.onShutdown(std::nullopt);
    }
    catch (const std::exception& error) {
        report.errors.push_back({std::nullopt, error.what()});
    }
    report.messages = std::move(client.messages);

    using Key = std::tuple<std::string, std::string, std::string, std::string, std::string, bool>;
    std::map<Key, DebugScanGroup> groups;
    for (const auto& document : report.documents) {
        const auto& diagnostics = document.result.value().diagnostics;
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            const auto& diagnostic = diagnostics[i];
            struct Context {
                std::string syntaxKind;
                std::string role;
                std::string completionKind;
                std::string trigger;
                bool resolved;
            };
            auto context = rfl::from_generic<Context>(*diagnostic.data).value();
            auto code = rfl::get<std::string>(*diagnostic.code);
            Key key{code,
                    context.syntaxKind,
                    context.role,
                    context.completionKind,
                    context.trigger,
                    context.resolved};
            auto [it, inserted] =
                groups.try_emplace(key, DebugScanGroup{.code = code,
                                                       .syntaxKind = context.syntaxKind,
                                                       .role = context.role,
                                                       .completionKind = context.completionKind,
                                                       .trigger = context.trigger,
                                                       .resolved = context.resolved});
            ++it->second.count;
            it->second.occurrences.push_back({document.path, i, diagnostic.range.start.line + 1,
                                              diagnostic.range.start.character + 1});
        }
    }
    for (auto& [key, group] : groups) {
        report.summary.issues += group.count;
        report.groups.push_back(std::move(group));
    }
    std::stable_sort(report.groups.begin(), report.groups.end(),
                     [](const auto& a, const auto& b) { return a.count > b.count; });
    report.summary.filesScanned = report.documents.size();
    report.summary.groups = report.groups.size();
    report.summary.complete = report.errors.empty();
    return report;
}

} // namespace server
