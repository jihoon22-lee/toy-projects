#include "buildscope-example/message.hpp"

#ifndef BUILDSCOPE_CMAKE_EXAMPLE
#error "BuildScope CMake example definition is missing"
#endif

namespace buildscope_example {

std::string message() {
    return "BuildScope CMake example";
}

}  // namespace buildscope_example
