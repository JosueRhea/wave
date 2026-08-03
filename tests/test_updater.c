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

    /* The releases actually published, in order: whatever the comparison does
     * with hand-made version strings, it has to get the real sequence right —
     * an update that compares backwards would loop, reinstalling forever. */
    CHECK(wave_version_is_newer("v0.1.17-alpha", "0.1.16-alpha"));
    CHECK(wave_version_is_newer("v0.1.18-alpha", "0.1.17-alpha"));
    CHECK(!wave_version_is_newer("v0.1.17-alpha", "0.1.18-alpha"));
    CHECK(!wave_version_is_newer("v0.1.18-alpha", "0.1.18-alpha"));

    /* Reachable from the core, not just from the GLFW front-end. This is the
     * regression that shipped in 0.1.17: the check lived in mac.m, which only
     * the GLFW binary links, so Wave.app — by then the GPUI build — had no
     * updater at all and could not pull itself forward. Taking its address is
     * the whole test; calling it would hit the network. */
#ifdef __APPLE__
    CHECK(wave_check_for_updates != NULL);
#endif

    TEST_REPORT();
}
