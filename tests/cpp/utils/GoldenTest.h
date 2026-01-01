// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#pragma once

#include "Utils.h"
#include "catch2/internal/catch_context.hpp"
#include "rfl/json.hpp"
#include "rfl/to_generic.hpp"
#include <filesystem>
#include <unordered_map>
#define CATCH_CONFIG_RUNNER
#include "Test.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// --------------------------------------------------
// RAII class to handle golden test logic
// --------------------------------------------------
extern bool g_updateGoldenFlag;

class GoldenTestBase {

public:
    GoldenTestBase(std::string ext) : m_updateGolden(g_updateGoldenFlag) {

        m_goldenFilePath = findSlangRoot() / "tests" / "cpp" / "golden" /
                           (Catch::getCurrentContext().getResultCapture()->getCurrentTestName() +
                            ext);

        std::ifstream ifs(m_goldenFilePath);

        if (ifs.is_open()) {
            m_expected.assign(std::istreambuf_iterator<char>(ifs),
                              std::istreambuf_iterator<char>());

            INFO("Loaded golden file: " << m_goldenFilePath);
        }
        else {
            // If file doesn't exist, maybe the test is new, or the user wants to create it
            INFO("Making new golden file" << m_goldenFilePath);
            m_expected.clear();
        }
    }

    ~GoldenTestBase() {

        auto actual_str = m_actual.str();

        if (actual_str == m_expected) {
            return;
        }

        if (m_updateGolden) {
            // Overwrite golden file with new content
            std::ofstream ofs(m_goldenFilePath);
            if (ofs.is_open()) {
                ofs << actual_str;
                ofs.close();
                INFO("Updated golden file: " << m_goldenFilePath);
            }
            else {
                FAIL_CHECK("Failed to open golden file for writing: " << m_goldenFilePath);
            }
        }
        else {
            // Print a minimal diff and fail
            std::cout << "[GoldenTestBase] Mismatch found in: " << m_goldenFilePath << "\n";
            // Write actual output to a temporary file for diff
            auto tmpPath = std::filesystem::temp_directory_path() /
                           (m_goldenFilePath.filename().string() + ".actual");
            {
                std::ofstream tmpFile(tmpPath);
                tmpFile << actual_str;
            }
            // Run diff tool
#ifdef _WIN32
            std::string cmd = "fc \"" + m_goldenFilePath.string() + "\" \"" + tmpPath.string() +
                              "\"";
#else
            std::string cmd = "diff -u \"" + m_goldenFilePath.string() + "\" \"" +
                              tmpPath.string() + "\"";
#endif

            int ret = std::system(cmd.c_str());
            if (ret == 0) {
                std::cout << "No differences found by diff, but strings did not match.\n";
            }
            else {
                std::cout << "See diff above.\n";
            }
            FAIL_CHECK("Mismatch found in golden file: " << m_goldenFilePath);
            // Optionally, remove temp file
            std::filesystem::remove(tmpPath);
        }
    }

protected:
    std::filesystem::path m_goldenFilePath;
    std::string m_expected;
    std::ostringstream m_actual;
    bool m_updateGolden;

    // A very naive line-by-line diff print
    void printDiff(const std::string& expected, const std::string& actual) {
        std::cout << "---- EXPECTED ----\n" << expected << "\n";
        std::cout << "---- ACTUAL ------\n" << actual << "\n";
        std::cout << "------------------\n";
    }
};

class GoldenTest : public GoldenTestBase {
public:
    GoldenTest() : GoldenTestBase(".out") {}
    // We set actual output in the test
    void record(const std::string_view actual) { m_actual << actual; }
};

class JsonGoldenTest : public GoldenTestBase {

public:
    std::vector<rfl::Generic> m_entries;
    bool m_relativeUris;

    JsonGoldenTest(bool relativeUris = true) :
        GoldenTestBase(".json"), m_relativeUris(relativeUris) {}

    template<typename T>
    void record(const T& some_struct) {
        m_entries.push_back(rfl::to_generic(some_struct));
    }
    template<typename T>
    void record(std::string label, const T& some_struct) {
        m_entries.push_back(
            rfl::to_generic(std::unordered_map<std::string, T>{{label, (some_struct)}}));
    }

    /// Helper to convert file:// URIs to just filename in a Generic value
    static void makeUrisRelative(rfl::Generic& g) {
        std::visit(
            [](auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    constexpr std::string_view file_prefix = "file://";
                    if (val.starts_with(file_prefix)) {
                        size_t last_slash = val.rfind('/');
                        if (last_slash != std::string::npos && last_slash >= file_prefix.length()) {
                            val = std::string(file_prefix) + val.substr(last_slash + 1);
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, rfl::Generic::Object>) {
                    for (auto& [key, child] : val) {
                        makeUrisRelative(child);
                    }
                }
                else if constexpr (std::is_same_v<T, rfl::Generic::Array>) {
                    for (auto& child : val) {
                        makeUrisRelative(child);
                    }
                }
            },
            g.variant());
    }

    ~JsonGoldenTest() {
        if (m_relativeUris) {
            for (auto& entry : m_entries) {
                makeUrisRelative(entry);
            }
        }
        std::string json = rfl::json::write(m_entries, YYJSON_WRITE_PRETTY_TWO_SPACES);
        m_actual << json << "\n";
    }
};
