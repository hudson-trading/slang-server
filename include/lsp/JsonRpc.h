//------------------------------------------------------------------------------
// JsonRpc.h
// JSON-RPC 2.0 protocol types and message handling utilities
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "rfl/Generic.hpp"
#include <iostream>
#include <optional>
#include <rfl/json.hpp> // IWYU pragma: keep
#include <string>

namespace lsp {

using ID_t = std::optional<rfl::Variant<int, std::string>>;
using Params_t = std::optional<rfl::Generic>;

struct RpcRequest {
    /// includes notifications
    std::string jsonrpc;
    ID_t id;
    std::string method;
    Params_t params;
};

struct RpcNotification {
    std::string jsonrpc;
    std::string method;
    Params_t params;
};

struct RpcResponse {
    std::string jsonrpc;
    ID_t id;
    Params_t result;
};

struct RpcError {
    /// A number indicating the error type that occurred.
    int code;
    /// A string providing a short description of the error.
    std::string message;
};
struct RpcErrorResponse {
    std::string jsonrpc;
    ID_t id;
    RpcError error;
};

template<typename T>
void sendMessage(const T& message) {
    auto message_str = rfl::json::write<rfl::UnderlyingEnums>(message);
    std::cout << "Content-Length: " << message_str.length() << "\r\n\r\n";
    std::cout << message_str;
    std::cout.flush();
}

inline void sendNotification(const std::string& method, const rfl::Generic& params) {
    sendMessage(RpcNotification{
        .jsonrpc = "2.0",
        .method = method,
        .params = params,
    });
    std::cerr << "---> " << method;
    // std::cerr << ": " << rfl::json::write(params) << std::endl;
    std::cerr << std::endl;
}

inline void sendRequest(const std::string& method, const rfl::Generic& params) {
    sendMessage(RpcRequest{
        .jsonrpc = "2.0",
        .id = 0,
        .method = method,
        .params = params,
    });
    std::cerr << "---> " << method;
    // std::cerr << ": " << rfl::json::write(params) << std::endl;
    std::cerr << std::endl;
}

template<typename T>
T readJson(std::string& line, std::string& content) {
    while (std::getline(std::cin, line)) {
        if (line.find("Content-Length: ") != 0) {
            std::cerr << "<-/- " << "Invalid Line: " << line << std::endl;
            continue;
        }

        int contentLength = std::stoi(line.substr(16));
        content.resize(contentLength);

        do {
            // Read maybe charset and empty line. Neovim and Vscode both don't
            // have this header row.
            std::getline(std::cin, line);
        } while (line.size() > 1);

        std::cin.read(&content[0], contentLength);

        rfl::Result<T> request = rfl::json::read<T>(content);
        if (!request) {
            auto response = rfl::json::read<RpcResponse>(content);
            if (response) {
                continue;
            }
            std::cerr << "Error parsing JSON: " << content << std::endl;
            std::cerr << "Rfl Error: " << request.error().what() << std::endl;
            sendMessage(
                RpcErrorResponse{.jsonrpc = "2.0",
                                 .id = 0,
                                 .error = RpcError{
                                     .code = 1,
                                     .message = "Error parsing JSON: " + request.error().what() +
                                                " for content: " + content,
                                 }});
            continue;
        }

        return std::move(request.value());
    }
    return T{};
}

} // namespace lsp
