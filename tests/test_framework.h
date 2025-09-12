#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Port management for tests to avoid conflicts
class TestPortManager {
public:
  static int get_next_port() {
    static std::atomic<int> next_port{
        20000}; // Start from 20000 to avoid conflicts with commonly used ports
    return next_port.fetch_add(1);
  }

  static std::string get_next_rpc_address() {
    return "127.0.0.1:" + std::to_string(get_next_port());
  }
};

// Helper function to print vector<uint8_t> as hex string
inline std::string vector_to_hex_string(const std::vector<uint8_t> &data) {
  std::stringstream ss;
  ss << "0x";
  for (size_t i = 0; i < data.size(); ++i) {
    ss << std::hex << std::setfill('0') << std::setw(2)
       << static_cast<int>(data[i]);
    if (i < data.size() - 1)
      ss << " ";
  }
  return ss.str();
}

// Overload operator<< for std::vector<uint8_t>
inline std::ostream &operator<<(std::ostream &os,
                                const std::vector<uint8_t> &data) {
  os << vector_to_hex_string(data);
  return os;
}

// Enhanced test framework for comprehensive testing
class TestRunner {
private:
  int tests_run_ = 0;
  int tests_passed_ = 0;
  std::vector<std::string> failed_tests_;

public:
  void run_test(const std::string &test_name, std::function<void()> test_func) {
    tests_run_++;
    std::cout << "Running test: " << test_name << "... ";

    auto start_time = std::chrono::high_resolution_clock::now();

    try {
      test_func();
      tests_passed_++;
      auto end_time = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      std::cout << "PASSED (" << duration.count() << "ms)" << std::endl;
    } catch (const std::exception &e) {
      std::cout << "FAILED - " << e.what() << std::endl;
      failed_tests_.push_back(test_name + ": " + e.what());
    } catch (...) {
      std::cout << "FAILED - Unknown exception" << std::endl;
      failed_tests_.push_back(test_name + ": Unknown exception");
    }
  }

  void run_benchmark(const std::string &benchmark_name,
                     std::function<void()> benchmark_func,
                     int iterations = 100) {
    std::cout << "Running benchmark: " << benchmark_name << " (" << iterations
              << " iterations)... ";

    auto start_time = std::chrono::high_resolution_clock::now();

    try {
      for (int i = 0; i < iterations; ++i) {
        benchmark_func();
      }
      auto end_time = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
          end_time - start_time);
      double avg_time = static_cast<double>(duration.count()) / iterations;
      std::cout << "COMPLETED (avg: " << avg_time << "μs per iteration)"
                << std::endl;
    } catch (const std::exception &e) {
      std::cout << "FAILED - " << e.what() << std::endl;
    }
  }

  void print_summary() {
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Tests run: " << tests_run_ << std::endl;
    std::cout << "Tests passed: " << tests_passed_ << std::endl;
    std::cout << "Tests failed: " << (tests_run_ - tests_passed_) << std::endl;

    if (!failed_tests_.empty()) {
      std::cout << "\nFailed tests:" << std::endl;
      for (const auto &failure : failed_tests_) {
        std::cout << "  - " << failure << std::endl;
      }
    }

    if (tests_passed_ == tests_run_) {
      std::cout << "\nAll tests PASSED!" << std::endl;
    } else {
      std::cout << "\nSome tests FAILED!" << std::endl;
    }

    double pass_rate = static_cast<double>(tests_passed_) / tests_run_ * 100.0;
    std::cout << "Pass rate: " << pass_rate << "%" << std::endl;
  }

  bool all_passed() const { return tests_passed_ == tests_run_; }

  int get_tests_run() const { return tests_run_; }
  int get_tests_passed() const { return tests_passed_; }
};

// Enhanced test helper macros
#define ASSERT_TRUE(condition)                                                 \
  if (!(condition)) {                                                          \
    throw std::runtime_error("Assertion failed: " #condition);                 \
  }

#define ASSERT_FALSE(condition)                                                \
  if (condition) {                                                             \
    throw std::runtime_error("Assertion failed: " #condition                   \
                             " should be false");                              \
  }

#define ASSERT_EQ(expected, actual)                                            \
  if ((expected) != (actual)) {                                                \
    std::stringstream assert_ss;                                               \
    assert_ss << "Assertion failed: expected '" << (expected) << "' but got '" \
              << (actual) << "'";                                              \
    throw std::runtime_error(assert_ss.str());                                 \
  }

#define ASSERT_NE(expected, actual)                                            \
  if ((expected) == (actual)) {                                                \
    std::stringstream assert_ss;                                               \
    assert_ss << "Assertion failed: expected '" << (expected)                  \
              << "' to not equal '" << (actual) << "'";                        \
    throw std::runtime_error(assert_ss.str());                                 \
  }

#define ASSERT_LT(a, b)                                                        \
  if (!((a) < (b))) {                                                          \
    std::stringstream assert_ss;                                               \
    assert_ss << "Assertion failed: " << (a) << " should be less than "        \
              << (b);                                                          \
    throw std::runtime_error(assert_ss.str());                                 \
  }

#define ASSERT_LE(a, b)                                                        \
  if (!((a) <= (b))) {                                                         \
    std::stringstream assert_ss;                                               \
    assert_ss << "Assertion failed: " << (a)                                   \
              << " should be less than or equal to " << (b);                   \
    throw std::runtime_error(assert_ss.str());                                 \
  }

#define ASSERT_GT(a, b)                                                        \
  if (!((a) > (b))) {                                                          \
    std::stringstream assert_ss;                                               \
    assert_ss << "Assertion failed: " << (a) << " should be greater than "     \
              << (b);                                                          \
    throw std::runtime_error(assert_ss.str());                                 \
  }

#define ASSERT_GE(a, b)                                                        \
  if (!((a) >= (b))) {                                                         \
    std::stringstream assert_ss;                                               \
    assert_ss << "Assertion failed: " << (a)                                   \
              << " should be greater than or equal to " << (b);                \
    throw std::runtime_error(assert_ss.str());                                 \
  }

#define ASSERT_CONTAINS(str, substr)                                           \
  do {                                                                         \
    std::string __test_str = (str);                                            \
    std::string __test_substr = (substr);                                      \
    if (__test_str.find(__test_substr) == std::string::npos) {                 \
      std::string __error_msg = "Assertion failed: string '" + __test_str +    \
                                "' does not contain '" + __test_substr + "'";  \
      throw std::runtime_error(__error_msg);                                   \
    }                                                                          \
  } while (0)

#define ASSERT_NOT_EMPTY(container)                                            \
  if ((container).empty()) {                                                   \
    throw std::runtime_error(                                                  \
        "Assertion failed: container should not be empty");                    \
  }

#define ASSERT_THROWS(statement, exception_type)                               \
  try {                                                                        \
    statement;                                                                 \
    throw std::runtime_error("Assertion failed: expected " #exception_type     \
                             " to be thrown");                                 \
  } catch (const exception_type &) {                                           \
    /* Expected exception caught */                                            \
  }

#define EXPECT_NO_THROW(statement)                                             \
  try {                                                                        \
    statement;                                                                 \
  } catch (const std::exception &e) {                                          \
    std::string __error_msg =                                                  \
        "Expected no exception, but got: " + std::string(e.what());            \
    throw std::runtime_error(__error_msg);                                     \
  } catch (...) {                                                              \
    throw std::runtime_error("Assertion failed: unexpected exception thrown"); \
  }
