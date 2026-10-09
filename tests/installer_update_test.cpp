#include "setup_core.h"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = pt::setup::Utf8Path(argv[1]);
    const auto failures = pt::setup::SelfTestUpdate(root / "files") +
        pt::setup::SelfTestBundleUpdate(root / "bundles") + pt::setup::SelfTestIcon(root / "icons") +
        pt::setup::SelfTestUnicodePaths(root / "unicode");
    std::cout << (failures.empty() ? "PASS installer update, bundle rollback, icons and Unicode paths" : "FAIL" + failures) << '\n';
    return failures.empty() ? 0 : 1;
}
