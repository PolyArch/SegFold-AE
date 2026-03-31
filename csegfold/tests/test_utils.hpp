#pragma once

#include <iostream>
#include <string>
#include <cstdlib>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <sys/stat.h>

namespace test_utils {

/**
 * Assert helper for tests
 * Prints PASS/FAIL message and exits on failure
 */
inline void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ PASS: " << test_name << std::endl;
    } else {
        std::cerr << "✗ FAIL: " << test_name << std::endl;
        exit(1);
    }
}

/**
 * Print stack trace on signal (for debugging segfaults, etc.)
 */
inline void print_stacktrace(int sig) {
    void *array[50];
    size_t size = backtrace(array, 50);
    
    std::cerr << "\n=== Stack trace (signal " << sig << ") ===" << std::endl;
    std::cerr << "Obtained " << size << " stack frames." << std::endl;
    
    char **messages = backtrace_symbols(array, size);
    for (size_t i = 0; i < size; i++) {
        std::cerr << "[" << i << "] " << (messages[i] ? messages[i] : "???") << std::endl;
    }
    free(messages);
    
    std::cerr << "\nUse: addr2line -e <executable> <address> to get line numbers" << std::endl;
    std::cerr << "Or: gdb <executable> -ex 'info symbol <address>'" << std::endl;
    
    exit(1);
}

/**
 * Setup signal handlers for debugging
 */
inline void setup_signal_handlers() {
    signal(SIGINT, print_stacktrace);
    signal(SIGSEGV, print_stacktrace);
    signal(SIGABRT, print_stacktrace);
}

/**
 * Determine workspace root by looking for csegfold directory
 * Returns the path to the workspace root (SegFold directory)
 */
inline std::string get_workspace_root() {
    // Try to find it by checking for csegfold directory using stat
    std::string workspace_root = ".";
    struct stat info;
    
    // Check if we're in the workspace root (has csegfold subdirectory)
    if (stat("csegfold", &info) != 0 || !(info.st_mode & S_IFDIR)) {
        // Try going up one level (from build directory)
        if (stat("../csegfold", &info) == 0 && (info.st_mode & S_IFDIR)) {
            workspace_root = "..";
        } else if (stat("../../csegfold", &info) == 0 && (info.st_mode & S_IFDIR)) {
            // Try going up two levels
            workspace_root = "../..";
        }
    }
    return workspace_root;
}

/**
 * Get temporary directory path within the workspace
 * Creates the directory if it doesn't exist
 * Falls back to /tmp if workspace tmp directory cannot be created
 */
inline std::string get_tmp_dir() {
    std::string workspace_root = get_workspace_root();
    std::string tmp_dir = workspace_root + "/tmp";
    
    // Create tmp directory if it doesn't exist
    struct stat info;
    if (stat(tmp_dir.c_str(), &info) != 0) {
        std::string mkdir_cmd = "mkdir -p " + tmp_dir;
        if (system(mkdir_cmd.c_str()) != 0) {
            std::cerr << "Warning: Failed to create tmp directory: " << tmp_dir << std::endl;
            tmp_dir = "/tmp";  // Fallback to system tmp
        }
    }
    return tmp_dir;
}

/**
 * Check if a file exists
 */
inline bool file_exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFREG);
}

/**
 * Check if a directory exists
 */
inline bool dir_exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR);
}

/**
 * Print a test section header
 */
inline void print_test_section(const std::string& section_name) {
    std::cout << "\n=== " << section_name << " ===" << std::endl;
}

/**
 * Print test results summary
 */
inline void print_test_summary(int passed, int total) {
    std::cout << "\n====================================" << std::endl;
    std::cout << "Test Results: " << passed << "/" << total << " passed" << std::endl;
    if (passed == total) {
        std::cout << "✅ All tests passed!" << std::endl;
    } else {
        std::cout << "❌ Some tests failed!" << std::endl;
    }
    std::cout << "====================================" << std::endl;
}

} // namespace test_utils

// Convenience macros for common usage
#define TEST_ASSERT(condition, message) test_utils::test_assert(condition, message)
#define SETUP_SIGNALS() test_utils::setup_signal_handlers()
#define TEST_SECTION(name) test_utils::print_test_section(name)
