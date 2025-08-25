#include <type_traits>
#include <iostream>
int test() { return 42; }
int main() {
  using T = std::result_of<int()>::type;
  std::cout << "Works" << std::endl;
  return 0;
}
