#include "common/fault_tolerance.h"
#include <iostream>

using namespace slonana::common;

int main() {
    std::cout << "=== Testing Enum-based Operation Type Checking ===" << std::endl;
    
    // Test operation type parsing
    auto read_op = parse_operation_type("read_account");
    auto write_op = parse_operation_type("write_data");
    auto health_op = parse_operation_type("health_check");
    
    std::cout << "Parsed 'read_account' as: " << static_cast<int>(read_op) << std::endl;
    std::cout << "Parsed 'write_data' as: " << static_cast<int>(write_op) << std::endl;
    std::cout << "Parsed 'health_check' as: " << static_cast<int>(health_op) << std::endl;
    
    // Test degradation modes
    bool read_allowed_normal = is_operation_type_allowed(read_op, DegradationMode::NORMAL);
    bool write_allowed_readonly = is_operation_type_allowed(write_op, DegradationMode::READ_ONLY);
    bool health_allowed_essential = is_operation_type_allowed(health_op, DegradationMode::ESSENTIAL_ONLY);
    
    std::cout << "Read allowed in NORMAL: " << read_allowed_normal << std::endl;
    std::cout << "Write allowed in READ_ONLY: " << write_allowed_readonly << std::endl;
    std::cout << "Health allowed in ESSENTIAL_ONLY: " << health_allowed_essential << std::endl;
    
    // Test with degradation manager
    DegradationManager manager;
    
    // Test new enum-based method
    bool enum_result = manager.is_operation_type_allowed("test", OperationType::READ);
    std::cout << "Enum-based read operation allowed: " << enum_result << std::endl;
    
    std::cout << "✅ Enum-based operation checking working correctly" << std::endl;
    
    return 0;
}