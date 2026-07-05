/* test_recent.c - recent project list ordering and filtering. */
#include "test.h"
#include "recent.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_dir(const char *path) {
    mkdir(path, 0755);
}

int main(void) {
    char root[256];
    snprintf(root, sizeof root, "/tmp/wave_recent_%ld", (long)getpid());
    make_dir(root);

    char alpha[512], beta[512], gamma[512], missing[512];
    char alpha_real[512], beta_real[512], gamma_real[512];
    snprintf(alpha, sizeof alpha, "%s/alpha-app", root);
    snprintf(beta, sizeof beta, "%s/beta-tools", root);
    snprintf(gamma, sizeof gamma, "%s/gamma-site", root);
    snprintf(missing, sizeof missing, "%s/missing", root);
    make_dir(alpha);
    make_dir(beta);
    make_dir(gamma);
    realpath(alpha, alpha_real);
    realpath(beta, beta_real);
    realpath(gamma, gamma_real);

    RecentProjects recent;
    recent_projects_init(&recent);
    CHECK(recent_projects_add(&recent, alpha));
    CHECK(recent_projects_add(&recent, beta));
    CHECK(recent_projects_add(&recent, gamma));
    CHECK(!recent_projects_add(&recent, missing));
    CHECK_EQ((int)recent.count, 3);
    CHECK_STR(recent_projects_selected(&recent), gamma_real);

    CHECK(recent_projects_add(&recent, alpha));
    CHECK_EQ((int)recent.count, 3);
    CHECK_STR(recent_projects_selected(&recent), alpha_real);
    CHECK_STR(recent.paths[1], gamma_real);

    recent_projects_set_query(&recent, "bt");
    CHECK_EQ((int)recent.filtered_count, 1);
    CHECK_STR(recent_projects_selected(&recent), beta_real);

    recent_projects_insert_text(&recent, "x");
    CHECK_EQ((int)recent.filtered_count, 0);
    CHECK(recent_projects_selected(&recent) == NULL);
    recent_projects_backspace(&recent);
    CHECK_STR(recent_projects_selected(&recent), beta_real);

    recent_projects_set_query(&recent, "");
    recent_projects_move(&recent, +1);
    CHECK_STR(recent_projects_selected(&recent), gamma_real);

    char cmd[1024];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    system(cmd);
    TEST_REPORT();
}
