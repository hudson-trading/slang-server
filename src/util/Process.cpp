// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Process.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

#include "slang/util/ScopeGuard.h"

#ifdef _WIN32
#    define NOMINMAX
#    include <io.h>
#    include <windows.h>
#elif !defined(__wasi__)
#    include <cerrno>
#    include <fcntl.h>
#    include <spawn.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char** environ;
#endif

std::optional<server::ProcessResult> server::runProcess(std::span<const std::string> arguments,
                                                        std::string_view input) {
#ifdef __wasi__
    return std::nullopt;
#else
    if (arguments.empty())
        return std::nullopt;
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos)
            return std::nullopt;
    }

    // File-backed streams avoid pipe deadlocks when both input and output are large.
    auto closeFile = [](FILE* file) { std::fclose(file); };
    std::unique_ptr<FILE, decltype(closeFile)> in(std::tmpfile(), closeFile);
    std::unique_ptr<FILE, decltype(closeFile)> out(std::tmpfile(), closeFile);
    if (!in || !out ||
        (!input.empty() && std::fwrite(input.data(), 1, input.size(), in.get()) != input.size()) ||
        std::fseek(in.get(), 0, SEEK_SET) != 0)
        return std::nullopt;

    int exitCode;
#    ifdef _WIN32
    std::wstring command;
    for (const auto& argument : arguments) {
        if (!command.empty())
            command += L' ';
        command += L'"';
        size_t backslashes = 0;
        for (auto ch : std::filesystem::path(argument).wstring()) {
            if (ch == L'\\') {
                ++backslashes;
                continue;
            }
            command.append(ch == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
            command += ch;
            backslashes = 0;
        }
        command.append(backslashes * 2, L'\\');
        command += L'"';
    }
    auto stdinHandle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(in.get())));
    auto stdoutHandle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(out.get())));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    auto stderrHandle = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stderrHandle == INVALID_HANDLE_VALUE)
        return std::nullopt;
    slang::ScopeGuard closeStderr([&] { CloseHandle(stderrHandle); });
    if (!SetHandleInformation(stdinHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
        !SetHandleInformation(stdoutHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        return std::nullopt;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinHandle;
    startup.hStdOutput = stdoutHandle;
    startup.hStdError = stderrHandle;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &startup, &process))
        return std::nullopt;
    slang::ScopeGuard closeProcess([&] {
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    });
    DWORD status;
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &status))
        return std::nullopt;
    exitCode = static_cast<int>(status);
#    else
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return std::nullopt;
    slang::ScopeGuard destroyActions([&] { posix_spawn_file_actions_destroy(&actions); });
    if (posix_spawn_file_actions_adddup2(&actions, fileno(in.get()), STDIN_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, fileno(out.get()), STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0)
        return std::nullopt;
    std::vector<char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child;
    if (posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), environ) != 0)
        return std::nullopt;
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return std::nullopt;
    }
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#    endif

    if (std::fseek(out.get(), 0, SEEK_SET) != 0)
        return std::nullopt;
    ProcessResult result{.exitCode = exitCode};
    char buffer[4096];
    while (auto count = std::fread(buffer, 1, sizeof(buffer), out.get()))
        result.output.append(buffer, count);
    if (std::ferror(out.get()))
        return std::nullopt;
    return result;
#endif
}
