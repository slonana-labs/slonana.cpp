#include "common/types.h"

namespace slonana {
namespace common {

/**
 * @file types.cpp
 * @brief Template instantiations and implementations for common types
 *
 * This file provides explicit template instantiations for commonly used
 * Result<T> specializations to reduce compilation time and binary size.
 */

// Explicit template instantiations for frequently used Result types
// This reduces compilation time by avoiding repeated instantiation
template class Result<bool>;
template class Result<std::string>;
template class Result<uint64_t>;
template class Result<int>;

} // namespace common
} // namespace slonana