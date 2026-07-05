#ifndef WAVE_UPDATER_H
#define WAVE_UPDATER_H

int wave_version_compare(const char *a, const char *b);
int wave_version_is_newer(const char *latest, const char *current);

#endif
