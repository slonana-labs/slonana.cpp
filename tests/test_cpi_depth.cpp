#include "svm/engine.h"
#include <cassert>
#include <iostream>

namespace slonana {
namespace test {

using namespace slonana::svm;

class CPIDepthTester {
public:
  bool run_all_tests() {
    std::cout << "=== Running CPI Depth Tracking Tests ===" << std::endl;

    bool all_passed = true;
    all_passed &= test_initialization();
    all_passed &= test_depth_increment();
    all_passed &= test_depth_decrement();
    all_passed &= test_depth_limit();
    all_passed &= test_can_invoke_check();
    all_passed &= test_error_handling();
    all_passed &= test_nested_calls();
    all_passed &= test_max_depth_constant();

    if (all_passed) {
      std::cout << "✅ All CPI Depth Tracking tests passed!" << std::endl;
    } else {
      std::cout << "❌ Some CPI Depth Tracking tests failed!" << std::endl;
    }

    return all_passed;
  }

private:
  bool test_initialization() {
    std::cout << "Testing CPI depth initialization..." << std::endl;

    ExecutionContext context;
    
    assert(context.current_cpi_depth == 0);
    assert(context.can_invoke_cpi());
    assert(context.get_cpi_depth() == 0);
    assert(context.transaction_succeeded);

    std::cout << "✅ Initialization test passed" << std::endl;
    return true;
  }

  bool test_depth_increment() {
    std::cout << "Testing CPI depth increment..." << std::endl;

    ExecutionContext context;
    
    assert(context.enter_cpi());
    assert(context.get_cpi_depth() == 1);
    assert(context.transaction_succeeded);
    
    assert(context.enter_cpi());
    assert(context.get_cpi_depth() == 2);
    
    assert(context.enter_cpi());
    assert(context.get_cpi_depth() == 3);

    std::cout << "✅ Depth increment test passed" << std::endl;
    return true;
  }

  bool test_depth_decrement() {
    std::cout << "Testing CPI depth decrement..." << std::endl;

    ExecutionContext context;
    
    // Increment depth
    context.enter_cpi();
    context.enter_cpi();
    context.enter_cpi();
    assert(context.get_cpi_depth() == 3);
    
    // Decrement depth
    context.exit_cpi();
    assert(context.get_cpi_depth() == 2);
    
    context.exit_cpi();
    assert(context.get_cpi_depth() == 1);
    
    context.exit_cpi();
    assert(context.get_cpi_depth() == 0);
    
    // Should not go below 0
    context.exit_cpi();
    assert(context.get_cpi_depth() == 0);

    std::cout << "✅ Depth decrement test passed" << std::endl;
    return true;
  }

  bool test_depth_limit() {
    std::cout << "Testing CPI depth limit enforcement..." << std::endl;

    ExecutionContext context;
    
    // Should succeed up to MAX_DEPTH
    for (size_t i = 0; i < ExecutionContext::MAX_CPI_DEPTH; ++i) {
      bool result = context.enter_cpi();
      assert(result);
      assert(context.transaction_succeeded);
    }
    
    assert(context.get_cpi_depth() == ExecutionContext::MAX_CPI_DEPTH);
    
    // Next call should fail
    bool result = context.enter_cpi();
    assert(!result);
    assert(!context.transaction_succeeded);
    assert(!context.error_message.empty());
    assert(context.get_cpi_depth() == ExecutionContext::MAX_CPI_DEPTH);

    std::cout << "  Max CPI depth: " << ExecutionContext::MAX_CPI_DEPTH << std::endl;
    std::cout << "  Error message: " << context.error_message << std::endl;

    std::cout << "✅ Depth limit test passed" << std::endl;
    return true;
  }

  bool test_can_invoke_check() {
    std::cout << "Testing can_invoke_cpi check..." << std::endl;

    ExecutionContext context;
    
    // Should be able to invoke initially
    assert(context.can_invoke_cpi());
    
    // Increment to limit
    for (size_t i = 0; i < ExecutionContext::MAX_CPI_DEPTH; ++i) {
      assert(context.can_invoke_cpi());
      context.enter_cpi();
    }
    
    // Should not be able to invoke at limit
    assert(!context.can_invoke_cpi());
    
    // After decrement, should be able to invoke again
    context.exit_cpi();
    assert(context.can_invoke_cpi());

    std::cout << "✅ Can invoke check test passed" << std::endl;
    return true;
  }

  bool test_error_handling() {
    std::cout << "Testing CPI depth error handling..." << std::endl;

    ExecutionContext context;
    
    // Reach the limit
    for (size_t i = 0; i < ExecutionContext::MAX_CPI_DEPTH; ++i) {
      context.enter_cpi();
    }
    
    // Clear error state
    context.transaction_succeeded = true;
    context.error_message.clear();
    
    // Try to exceed limit
    bool result = context.enter_cpi();
    
    assert(!result);
    assert(!context.transaction_succeeded);
    assert(!context.error_message.empty());
    assert(context.error_message.find("CPI depth") != std::string::npos);

    std::cout << "✅ Error handling test passed" << std::endl;
    return true;
  }

  bool test_nested_calls() {
    std::cout << "Testing nested CPI calls..." << std::endl;

    ExecutionContext context;
    
    // Simulate nested program calls
    auto call_level_1 = [&]() {
      assert(context.enter_cpi());
      assert(context.get_cpi_depth() == 1);
      
      auto call_level_2 = [&]() {
        assert(context.enter_cpi());
        assert(context.get_cpi_depth() == 2);
        
        auto call_level_3 = [&]() {
          assert(context.enter_cpi());
          assert(context.get_cpi_depth() == 3);
          
          auto call_level_4 = [&]() {
            assert(context.enter_cpi());
            assert(context.get_cpi_depth() == 4);
            
            // At max depth now
            assert(!context.can_invoke_cpi());
            
            context.exit_cpi();
          };
          
          call_level_4();
          assert(context.get_cpi_depth() == 3);
          
          context.exit_cpi();
        };
        
        call_level_3();
        assert(context.get_cpi_depth() == 2);
        
        context.exit_cpi();
      };
      
      call_level_2();
      assert(context.get_cpi_depth() == 1);
      
      context.exit_cpi();
    };
    
    call_level_1();
    assert(context.get_cpi_depth() == 0);

    std::cout << "✅ Nested calls test passed" << std::endl;
    return true;
  }

  bool test_max_depth_constant() {
    std::cout << "Testing MAX_CPI_DEPTH constant..." << std::endl;

    // Verify MAX_CPI_DEPTH is 4 (Agave compatibility)
    assert(ExecutionContext::MAX_CPI_DEPTH == 4);
    
    std::cout << "  MAX_CPI_DEPTH = " << ExecutionContext::MAX_CPI_DEPTH << std::endl;

    std::cout << "✅ Max depth constant test passed" << std::endl;
    return true;
  }
};

} // namespace test
} // namespace slonana

int main() {
  slonana::test::CPIDepthTester tester;
  return tester.run_all_tests() ? 0 : 1;
}
