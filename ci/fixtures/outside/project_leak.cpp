int main() {
    auto *project_leak = new char[64];
    project_leak[0] = 'x';
    return project_leak[0] == 'x' ? 0 : 1;
}
