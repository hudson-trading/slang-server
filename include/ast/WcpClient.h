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

#ifdef _WIN32
// Handle Windows macro collision. MinGW's libstdc++ already defines NOMINMAX.
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
// Keep rarely used Windows headers out of every file that includes this one.
// Without it, windows.h pulls in rpcndr.h, whose global 'byte' typedef clashes
// with slang::byte in any file that has 'using namespace slang'.
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#        define SLANG_SERVER_UNDEF_WIN32_LEAN_AND_MEAN
#    endif

#    include <winsock2.h>
#    include <ws2tcpip.h>

#    ifdef SLANG_SERVER_UNDEF_WIN32_LEAN_AND_MEAN
#        undef WIN32_LEAN_AND_MEAN
#        undef SLANG_SERVER_UNDEF_WIN32_LEAN_AND_MEAN
#    endif
// CMake links ws2_32 for all Windows toolchains; this covers other MSVC builds.
#    ifdef _MSC_VER
#        pragma comment(lib, "ws2_32.lib")
#    endif
typedef int socklen_t;
typedef SSIZE_T ssize_t;
#    define MSG_DONTWAIT 0
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

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
    void stop() {
#ifdef _WIN32
        closesocket(m_clientFd);
        closesocket(m_serverFd);
#else
        close(m_clientFd);
        close(m_serverFd);
#endif
        m_running = false;
    }

    template<typename T>
    void sendMessage(const T& message) {
        auto str = rfl::json::write(message);
        sendBuffer(str.c_str(), str.length());
        char null = '\0';
        sendBuffer(&null, 1);
    }

    void sendBuffer(const char* buff, size_t len) {
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
    }

    std::optional<std::string> getMessage() {
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

                // prevent nagle / delayed ack deadlock
                // should also make sure servers use TCP_NODELAY
#ifdef __linux__
                int flag = 1;
                setsockopt(m_clientFd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
#endif
                size_t pos = m_recvBuffer.find('\0');
                if (pos != std::string::npos) {
                    std::string result = m_recvBuffer.substr(0, pos);
                    m_recvBuffer.erase(0, pos + 1);
                    return result;
                }
            }
        }

        return std::nullopt;
    }

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
