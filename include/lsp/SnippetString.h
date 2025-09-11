//------------------------------------------------------------------------------
// SnippetString.h
// Text snippet string builder for LSP completion items
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

/**
 * @brief A snippet string is a template which allows to insert text
 * and to control the editor cursor when insertion happens.
 *
 * A snippet can define tab stops and placeholders with `$1`, `$2`
 * and `${3:foo}`. `$0` defines the final tab stop, it defaults to
 * the end of the snippet. Variables are defined with `$name` and
 * `${name:default value}`.
 */
class SnippetString {
public:
    /**
     * @brief Create a new snippet string.
     *
     * @param value An optional initial snippet string value.
     */
    SnippetString(std::string_view value = "") : _value(value) {}

    /**
     * @brief Gets the snippet string.
     *
     * @return The complete snippet string.
     */
    std::string_view getValue() const { return _value; }

    /**
     * @brief Appends the given text to this snippet string.
     * The string will be escaped to remove snippet special characters.
     *
     * @param text The text to append.
     * @return A reference to this SnippetString.
     */
    // SnippetString& appendText(std::string_view text) {
    //     _value += _escape(text);
    //     return *this;
    // }
    SnippetString& appendText(std::string_view text) {
        _value += _escape(std::string(text));
        return *this;
    }

    /**
     * @brief Appends a tabstop (`$1`, `$2`, etc.) to this snippet string.
     *
     * @param number The number for this tabstop. If not provided, an auto-incremented
     * value is used.
     * @return A reference to this SnippetString.
     */
    SnippetString& appendTabstop(std::optional<int> number = std::nullopt) {
        _value += '$';
        _value += std::to_string(_nextTabstop(number));
        return *this;
    }

    /**
     * @brief Appends a placeholder (`${1:value}`) to this snippet string.
     *
     * @param value The placeholder value.
     * @param number The number for this tabstop. If not provided, an auto-incremented
     * value is used.
     * @return A reference to this SnippetString.
     */
    SnippetString& appendPlaceholder(std::string_view value,
                                     std::optional<int> number = std::nullopt) {
        _value += "${";
        _value += std::to_string(_nextTabstop(number));
        _value += ':';
        _value += _escape(std::string(value));
        _value += '}';
        return *this;
    }

    /**
     * @brief Appends a choice (`${1|a,b,c|}`) to this snippet string.
     *
     * @param values A vector of strings for the choices.
     * @param number The number of this tabstop. If not provided, an auto-incremented
     * value is used.
     * @return A reference to this SnippetString.
     */
    SnippetString& appendChoice(const std::vector<std::string>& values,
                                std::optional<int> number = std::nullopt) {
        if (values.empty()) {
            return *this;
        }
        _value += "${";
        _value += std::to_string(_nextTabstop(number));
        _value += '|';

        std::stringstream ss;
        bool first = true;
        for (const auto& val : values) {
            if (!first) {
                ss << ',';
            }
            // Escape commas and pipes within choices
            std::string escapedVal = val;
            size_t pos = 0;
            while ((pos = escapedVal.find_first_of(",|\\", pos)) != std::string::npos) {
                escapedVal.insert(pos, 1, '\\');
                pos += 2;
            }
            ss << escapedVal;
            first = false;
        }

        _value += ss.str();
        _value += "|}";
        return *this;
    }

    /**
     * @brief Appends a variable (`${VAR:default}`) to this snippet string.
     *
     * @param name The name of the variable (excluding the `$`).
     * @param defaultValue The default value for the variable.
     * @return A reference to this SnippetString.
     */
    SnippetString& appendVariable(std::string_view name, std::string_view defaultValue) {
        _value += "${";
        _value += name;
        if (!defaultValue.empty()) {
            _value += ':';
            _value += _escape(defaultValue);
        }
        _value += '}';
        return *this;
    }

    /**
     * @brief Appends a variable with a nested snippet as its default value.
     *
     * @param name The name of the variable (excluding the `$`).
     * @param callback A function to build the nested snippet for the default value.
     * @return A reference to this SnippetString.
     */
    SnippetString& appendVariable(std::string_view name,
                                  const std::function<void(SnippetString&)>& callback) {
        SnippetString nestedSnippet;
        callback(nestedSnippet);
        _value += "${";
        _value += name;
        if (!nestedSnippet.getValue().empty()) {
            _value += ':';
            _value += nestedSnippet.getValue();
        }
        _value += '}';
        return *this;
    }

private:
    std::string _value;
    int _tabstop = 1;

    /**
     * @brief Escapes a string for safe inclusion in a snippet.
     */
    static std::string _escape(std::string_view value) {
        std::string result;
        result.reserve(value.length());
        for (char c : value) {
            switch (c) {
                case '$':
                case '}':
                case '\\':
                    result += '\\';
                    break;
            }
            result += c;
        }
        return result;
    }

    /**
     * @brief Gets the next tabstop number and increments the internal counter.
     */
    int _nextTabstop(std::optional<int> number) {
        if (number.has_value()) {
            // If a specific number is provided, use it, but also make sure our
            // auto-increment counter is at least one greater for the next call.
            _tabstop = std::max(_tabstop, number.value() + 1);
            return number.value();
        }
        return _tabstop++;
    }
};
