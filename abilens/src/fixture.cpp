#include <string>

extern "C" int abilens_fixture_value(const char* input) {
    const std::string value = input == nullptr ? "fixture" : input;
    return static_cast<int>(value.size());
}

struct FixtureVtable {
    virtual ~FixtureVtable() = default;
    virtual int kind() const;
};

int FixtureVtable::kind() const { return 1; }
