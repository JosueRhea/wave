#include "updater.h"

#include <ctype.h>
#include <string.h>

static const char *skip_prefix(const char *s) {
    if (!s) return "";
    while (*s == 'v' || *s == 'V' || *s == ' ' || *s == '\t') s++;
    return s;
}

static int read_number(const char **sp) {
    const char *s = *sp;
    int n = 0;
    while (isdigit((unsigned char)*s)) {
        n = n * 10 + (*s - '0');
        s++;
    }
    *sp = s;
    return n;
}

static int prerelease_rank(const char *s) {
    if (!s || !*s) return 3;
    if (strstr(s, "alpha")) return 0;
    if (strstr(s, "beta")) return 1;
    if (strstr(s, "rc")) return 2;
    return 0;
}

int wave_version_compare(const char *a, const char *b) {
    const char *pa = skip_prefix(a);
    const char *pb = skip_prefix(b);
    for (int i = 0; i < 3; i++) {
        int na = read_number(&pa);
        int nb = read_number(&pb);
        if (na != nb) return na > nb ? 1 : -1;
        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
    }

    int ra = prerelease_rank(pa);
    int rb = prerelease_rank(pb);
    if (ra != rb) return ra > rb ? 1 : -1;

    int extra_a = 0, extra_b = 0;
    while (*pa) {
        if (isdigit((unsigned char)*pa)) {
            extra_a = read_number(&pa);
            break;
        }
        pa++;
    }
    while (*pb) {
        if (isdigit((unsigned char)*pb)) {
            extra_b = read_number(&pb);
            break;
        }
        pb++;
    }
    if (extra_a != extra_b) return extra_a > extra_b ? 1 : -1;
    return 0;
}

int wave_version_is_newer(const char *latest, const char *current) {
    return wave_version_compare(latest, current) > 0;
}
