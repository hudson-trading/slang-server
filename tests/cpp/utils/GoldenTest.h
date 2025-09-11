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
            std::string cmd = "diff -u \"" + m_goldenFilePath.string() + "\" \"" +
                              tmpPath.string() + "\"";
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
    JsonGoldenTest() : GoldenTestBase(".json") {}

    template<typename T>
    void record(const T& some_struct) {
        m_entries.push_back(rfl::to_generic(some_struct));
    }
    template<typename T>
    void record(std::string label, const T& some_struct) {
        m_entries.push_back(
            rfl::to_generic(std::unordered_map<std::string, T>{{label, (some_struct)}}));
    }
    ~JsonGoldenTest() {
        m_actual << rfl::json::write(m_entries, YYJSON_WRITE_PRETTY_TWO_SPACES) << "\n";
    }
};
