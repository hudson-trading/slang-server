//------------------------------------------------------------------------------
/// @file WcpTypes.h
/// @brief Waveform viewer Control Protocol
/// see: https://gitlab.com/lklemmer/wcp-protocol/
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "rfl/TaggedUnion.hpp"
#include <string>
#include <vector>

namespace wcp {
using VariablePath = std::string;
using ScopePath = std::string;

struct Message {
    std::string type;
};

struct Event {
    using Tag = rfl::Literal<"event">;
    std::string event;
};

struct Greeting {
    std::string type;
    std::string version;
    std::vector<std::string> commands;
};

struct Load {
    std::string type;
    std::string command;
    std::string source;
};

struct AddVariables {
    std::string type;
    std::string command;
    std::vector<VariablePath> variables;
};

struct AddScope {
    std::string type;
    std::string command;
    ScopePath scope;
    bool recursive;
};

struct AddItems {
    std::string type;
    std::string command;
    std::vector<VariablePath> items;
    bool recursive;
};

struct WaveformsLoaded {
    using Tag = rfl::Literal<"waveforms_loaded">;
    std::string type;
    std::string source;
};

struct GotoDeclaration {
    using Tag = rfl::Literal<"goto_declaration">;
    std::string type;
    std::string variable;
};

struct AddDrivers {
    using Tag = rfl::Literal<"add_drivers">;
    std::string type;
    std::string variable;
};

struct AddLoads {
    using Tag = rfl::Literal<"add_loads">;
    std::string type;
    std::string variable;
};

struct Response {
    using Tag = rfl::Literal<"response">;
    std::string command;
};

using S2CMessage = rfl::TaggedUnion<"type", Response, Event>;
using S2CEvent = rfl::TaggedUnion<"event", WaveformsLoaded, GotoDeclaration, AddDrivers, AddLoads>;

} // namespace wcp
