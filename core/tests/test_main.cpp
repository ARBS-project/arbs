// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core test runner. Usage: arbs_core_tests.exe <golden-dir> [filter-substring]
// Local only: no device, no network, no files written (output goes to stdout).
#include <cstdio>
#include <exception>
#include <string>

#include "mini_test.h"

int main(int argc, char** argv) {
    if (argc > 1) mt::goldenDir() = argv[1];
    const std::string filter = argc > 2 ? argv[2] : "";
    int ok = 0, failed = 0;
    for (const mt::Case& c : mt::registry()) {
        const std::string full = c.group + "." + c.name;
        if (!filter.empty() && full.find(filter) == std::string::npos) continue;
        try {
            c.fn();
            ok++;
            std::printf("[ OK ] %s\n", full.c_str());
        } catch (const std::exception& e) {
            failed++;
            std::printf("[FAIL] %s : %s\n", full.c_str(), e.what());
        }
        std::fflush(stdout);
    }
    std::printf("\n---- report ----\n");
    for (const std::string& l : mt::report()) std::printf("%s\n", l.c_str());
    std::printf("\n---- summary ----\ncases ok=%d failed=%d checks=%lld golden=%s\n", ok, failed, mt::checks(), mt::goldenDir().c_str());
    return failed == 0 ? 0 : 1;
}
