#include "buildscope-example/message.hpp"

#ifndef BUILDSCOPE_QMAKE_EXAMPLE
#error "BuildScope qmake example definition is missing"
#endif

namespace buildscope_example {

std::string message() {
    return "BuildScope qmake example";
}

}  // namespace buildscope_example
