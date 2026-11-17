//------------------------------------------------------------------------------
// gen_config_main.cpp
// Standalone tool to generate JSON schema from Config.h
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "Config.h"
#include <iostream>
#include <rfl/DefaultIfMissing.hpp>
#include <rfl/json.hpp>
#include <rfl/to_generic.hpp>

int main() {
    try {
        using Object = rfl::Generic::Object;
        auto schema = rfl::json::read<rfl::Generic>(
                          rfl::json::to_schema<Config, rfl::DefaultIfMissing>())
                          .value();
        auto& definitions = std::get<Object>(std::get<Object>(schema.get()).at("$defs").get());
        auto addDefaults = [&](auto&& self, rfl::Generic& node,
                               const rfl::Generic& defaults) -> void {
            const auto* values = std::get_if<Object>(&defaults.get());
            if (!values)
                return;
            auto* definition = &std::get<Object>(node.get());
            if (auto reference = definition->get("$ref")) {
                auto name = reference->to_string().value().substr(
                    std::string_view("#/$defs/").size());
                definition = &std::get<Object>(definitions.at(name).get());
            }
            if (!definition->count("properties"))
                return;
            auto& properties = std::get<Object>(definition->at("properties").get());
            for (const auto& [name, value] : *values) {
                auto& property = properties.at(name);
                std::get<Object>(property.get())["default"] = value;
                self(self, property, value);
            }
        };
        addDefaults(addDefaults, schema, rfl::to_generic(Config{}));
        std::cout << rfl::json::write(schema, rfl::json::pretty | YYJSON_WRITE_PRETTY_TWO_SPACES)
                  << '\n';
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error generating config schema: " << e.what() << '\n';
        return 1;
    }
}
