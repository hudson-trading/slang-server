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
#include <mutex>
#include <thread>

#ifndef _WIN32
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace waves {

struct ScopeToWaveform {
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

    void stop() {
#ifndef _WIN32
        close(m_clientFd);
        close(m_serverFd);
        m_running = false;
#endif
    }

    template<typename T>
    void sendMessage(const T& message) {
        auto str = rfl::json::write(message);
        sendBuffer(str.c_str(), str.length());
        char null = '\0';
        sendBuffer(&null, 1);
    }

    void sendBuffer(const char* buff, size_t len) {
#ifndef _WIN32
        size_t sentLen = 0;
        while (sentLen < len) {
            ssize_t sent = send(m_clientFd, buff + sentLen, len - sentLen, 0);
            if (sent < 0) {
                std::cerr << "WCP send failed: " << strerror(errno) << std::endl;
                return;
            }
            else {
                sentLen += sent;
            }
        }
#endif
    }

    std::optional<std::string> getMessage() {
#ifndef _WIN32
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        fd_set clientFdSet;
        FD_ZERO(&clientFdSet);
        FD_SET(m_clientFd, &clientFdSet);

        int retval = select(m_clientFd + 1, &clientFdSet, nullptr, nullptr, &tv);
        if (retval < 0) {
            std::cerr << "WCP select() failure: " << strerror(errno) << std::endl;
            stop();
        }
        else if (retval > 0) {
            // TODO -- one less copy here
            const ssize_t buffSize = 1024;
            char buff[buffSize];
            ssize_t size = recv(m_clientFd, buff, buffSize, MSG_DONTWAIT);
            if (size < 0) {
                std::cerr << "WCP recv() failure: " << strerror(errno) << std::endl;
                stop();
            }
            else if (size == 0) {
                std::cerr << "WCP server disconnected" << std::endl;
                stop();
            }
            else {
                m_recvBuffer.append(buff, size);
                size_t pos = m_recvBuffer.find('\0');
                if (pos != std::string::npos) {
                    std::string result = m_recvBuffer.substr(0, pos);
                    m_recvBuffer.erase(0, pos + 1);
                    return result;
                }
            }
        }
#endif

        return std::nullopt;
    }

    void initClient();
    void greet();

    void onWaveformsLoaded(const wcp::WaveformsLoaded&);
    void onGotoDeclaration(const wcp::GotoDeclaration&);
    void onAddDrivers(const wcp::AddDrivers&);
    void onAddLoads(const wcp::AddLoads&);

public:
    WcpClient(SlangServerWcp* lspServer, const std::string& command) :
        m_lspServer(lspServer), m_command(command) {
        initClient();
        runViewer();
        greet();
        m_clientThread = std::thread(&WcpClient::runClient, this);
    }

    bool running() { return m_running; }

    void addVariable(const std::string&);
    void addScope(const ScopeToWaveform&);
    void loadWaveform(const std::string&);
};

} // namespace waves
