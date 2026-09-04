#include <string>

extern "C" int abilens_fixture_value(const char* input) {
    const std::string value = input == nullptr ? "fixture" : input;
    return static_cast<int>(value.size());
}
