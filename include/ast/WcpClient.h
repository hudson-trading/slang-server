//------------------------------------------------------------------------------
/// @file WcpClient.h
/// @brief Waveform viewer Control Protocol client
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "SlangServerWcp.h"
#include "wcp/WcpTypes.h"
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>

namespace waves {

struct ItemToWaveform {
    std::string path;
    bool recursive;
};

class WcpClient {
private:
    std::atomic<bool> m_running = true;
    std::thread m_clientThread;

    // Starts the waveform viewer which will connect to this WCP client
    void runViewer();

    // WCP client main loop
    void runClient();

    // WCP socket
    int m_serverFd;
    int m_clientFd;

    // WCP TCP port
    int m_port;

    // Slang LSP client
    SlangServerWcp* m_lspServer;

    // Socket receive buffer
    std::string m_recvBuffer;

    // Waveform viewer command
    std::string m_command;

    // Close the sockets and stop the client loop.
    void stop();

    template<typename T>
    void sendMessage(const T& message) {
        auto str = rfl::json::write(message);
        sendBuffer(str.c_str(), str.length());
        char null = '\0';
        sendBuffer(&null, 1);
    }

    // Send all bytes in a buffer, stopping on a socket error.
    void sendBuffer(const char* buff, size_t len);

    // Poll the socket and return a complete null-terminated message when available.
    std::optional<std::string> getMessage();

    void initClient();
    void greet();

    void onWaveformsLoaded(const wcp::WaveformsLoaded&);
    void onGotoDeclaration(const wcp::GotoDeclaration&);
    void onAddDrivers(const wcp::AddDrivers&);
    void onAddLoads(const wcp::AddLoads&);

public:
    WcpClient(SlangServerWcp* lspServer, std::string& command) :
        m_lspServer(lspServer), m_command(std::move(command)) {
        initClient();
        runViewer();
        greet();
        m_clientThread = std::thread(&WcpClient::runClient, this);
    }

    bool running() { return m_running; }

    void addItem(const ItemToWaveform&);
    void loadWaveform(const std::string&);
};

} // namespace waves
