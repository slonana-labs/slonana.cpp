#include <type_traits>
#include <iostream>
int test() { return 42; }
int main() {
  using T = std::invoke_result_t<int(*)()>;
  std::cout << "invoke_result_t works" << std::endl;
  return 0;
}
