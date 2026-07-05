#include "test.h"
#include "updater.h"

int main(void) {
    CHECK_EQ(wave_version_compare("v0.1.1-alpha", "0.1.0-alpha"), 1);
    CHECK_EQ(wave_version_compare("0.1.1-alpha", "v0.1.1-alpha"), 0);
    CHECK_EQ(wave_version_compare("v0.1.2-alpha", "0.1.2-alpha"), 0);
    CHECK_EQ(wave_version_compare("0.1.1-beta", "0.1.1-alpha"), 1);
    CHECK_EQ(wave_version_compare("0.1.1-rc1", "0.1.1-beta"), 1);
    CHECK_EQ(wave_version_compare("0.1.1", "0.1.1-rc1"), 1);
    CHECK_EQ(wave_version_compare("0.2.0", "0.10.0"), -1);
    CHECK(wave_version_is_newer("v0.1.2", "0.1.1-alpha"));
    CHECK(!wave_version_is_newer("v0.1.2-alpha", "0.1.2-alpha"));
    CHECK(!wave_version_is_newer("v0.1.1-alpha", "0.1.1-alpha"));

    TEST_REPORT();
}
