#include "common/types.h"

namespace slonana {
namespace common {

// Template specializations for common result types
template class Result<bool>;
template class Result<std::string>;

} // namespace common
} // namespace slonana