#ifndef WAVE_UPDATER_H
#define WAVE_UPDATER_H

int wave_version_compare(const char *a, const char *b);
int wave_version_is_newer(const char *latest, const char *current);

/* Progress of a check, reported to the front-end in order. A check that finds
 * nothing new ends at CURRENT; one that does runs AVAILABLE -> DOWNLOADING… ->
 * DOWNLOADED and then quits the app so the installer can replace it. ERROR can
 * end it at any point.
 *
 * `manual` gates the *check* phase only: an automatic check reports neither
 * CHECKING nor CURRENT, and swallows a failure to reach GitHub, so a launch
 * that is offline or already current says nothing at all. Once an update has
 * been found every state reports either way — by then the app is going to
 * replace itself, and doing that silently would be worse. */
enum {
    UPDATE_STATE_CHECKING = 1,
    UPDATE_STATE_CURRENT = 2,
    UPDATE_STATE_AVAILABLE = 3,
    UPDATE_STATE_DOWNLOADING = 4,
    UPDATE_STATE_ERROR = 5,
    UPDATE_STATE_DOWNLOADED = 6
};

/* `version` is the release being reported on, `detail` a human-readable note
 * (the error text, or the version being upgraded *from* on AVAILABLE), and
 * `progress` runs 0..1 while downloading. Always called on the main thread. */
typedef void (*WaveUpdateCallback)(int state, const char *version,
                                   const char *detail, double progress);

#ifdef __APPLE__
/* Non-blocking: returns as soon as the request is in flight, and reports every
 * state through `callback`. `current_version` is only a fallback — a bundled
 * Wave uses its Info.plist version. Implemented in updater_mac.m. */
void wave_check_for_updates(const char *current_version, int manual,
                            WaveUpdateCallback callback);
#endif

#endif
