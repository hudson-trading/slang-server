//------------------------------------------------------------------------------
/// @file WcpClient.h
/// @brief Interface for WCP -> slang server
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <mutex>
#include <string>
#include <vector>

// See here for more information on WCP:
// https://gitlab.com/waveform-control-protocol/wcp

class SlangServerWcp {
public:
    virtual void onGotoDeclaration(const std::string&) = 0;
    virtual void onWaveformLoaded(const std::string&) = 0;
    virtual std::vector<std::string> getDrivers(const std::string&) = 0;
    virtual std::vector<std::string> getLoads(const std::string&) = 0;
    virtual std::mutex& getMutex() = 0;
};
