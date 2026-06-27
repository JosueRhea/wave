#include "watch.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <sys/event.h>
#include <sys/time.h>
#ifndef O_EVTONLY
#define O_EVTONLY 0
#endif
#endif

static long stat_mtime_nsec(const struct stat *st) {
#if defined(__APPLE__)
    return st->st_mtimespec.tv_nsec;
#elif defined(__linux__)
    return st->st_mtim.tv_nsec;
#else
    (void)st;
    return 0;
#endif
}

static long stat_ctime_nsec(const struct stat *st) {
#if defined(__APPLE__)
    return st->st_ctimespec.tv_nsec;
#elif defined(__linux__)
    return st->st_ctim.tv_nsec;
#else
    (void)st;
    return 0;
#endif
}

void watch_service_init(WatchService *svc) {
    if (!svc) return;
    svc->file_kq = -1;
    svc->workspace_stream = NULL;
    svc->workspace_queue = NULL;
    atomic_init(&svc->workspace_pending, 0);
#ifdef __APPLE__
    svc->file_kq = kqueue();
#endif
}

void watch_service_shutdown(WatchService *svc) {
    if (!svc) return;
    watch_workspace_stop(svc);
    if (svc->file_kq >= 0) {
        close(svc->file_kq);
        svc->file_kq = -1;
    }
}

void watch_file_init(FileWatch *fw) {
    if (!fw) return;
    fw->seen = 0;
    fw->mtime_sec = 0;
    fw->mtime_nsec = 0;
    fw->ctime_sec = 0;
    fw->ctime_nsec = 0;
    fw->size = 0;
    fw->native_fd = -1;
}

void watch_file_stop(FileWatch *fw) {
    if (!fw || fw->native_fd < 0) return;
    close(fw->native_fd);
    fw->native_fd = -1;
}

void watch_file_mark_seen(FileWatch *fw, const struct stat *st) {
    if (!fw || !st) return;
    fw->seen = 1;
    fw->mtime_sec = st->st_mtime;
    fw->mtime_nsec = stat_mtime_nsec(st);
    fw->ctime_sec = st->st_ctime;
    fw->ctime_nsec = stat_ctime_nsec(st);
    fw->size = st->st_size;
}

int watch_file_stat_changed(const FileWatch *fw, const struct stat *st) {
    if (!fw || !st) return 0;
    return !fw->seen || fw->mtime_sec != st->st_mtime ||
           fw->mtime_nsec != stat_mtime_nsec(st) ||
           fw->ctime_sec != st->st_ctime ||
           fw->ctime_nsec != stat_ctime_nsec(st) ||
           fw->size != st->st_size;
}

int watch_file_start(WatchService *svc, FileWatch *fw, const char *path) {
    if (!fw || !path) return -1;
    watch_file_stop(fw);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fw->seen = 0;
        return -1;
    }
    watch_file_mark_seen(fw, &st);

#ifdef __APPLE__
    if (!svc || svc->file_kq < 0) return 0;
    int fd = open(path, O_EVTONLY);
    if (fd < 0) return -1;
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE |
           NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK, 0, NULL);
    if (kevent(svc->file_kq, &kev, 1, NULL, 0, NULL) != 0) {
        close(fd);
        return -1;
    }
    fw->native_fd = fd;
#else
    (void)svc;
#endif
    return 0;
}

int watch_file_native_id(const FileWatch *fw) {
    return fw ? fw->native_fd : -1;
}

int watch_poll_file_events(WatchService *svc, int *ids, int cap) {
    if (!svc || !ids || cap <= 0) return 0;
#ifdef __APPLE__
    if (svc->file_kq < 0) return 0;
    struct timespec ts = {0, 0};
    struct kevent ev[16];
    int want = cap < (int)(sizeof ev / sizeof ev[0]) ? cap : (int)(sizeof ev / sizeof ev[0]);
    int n = kevent(svc->file_kq, NULL, 0, ev, want, &ts);
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) ids[i] = (int)ev[i].ident;
    return n;
#else
    (void)svc; (void)ids; (void)cap;
    return 0;
#endif
}

#ifdef __APPLE__
static void workspace_events_cb(ConstFSEventStreamRef stream, void *info,
                                size_t n, void *paths,
                                const FSEventStreamEventFlags flags[],
                                const FSEventStreamEventId ids[]) {
    (void)stream; (void)n; (void)paths; (void)flags; (void)ids;
    WatchService *svc = info;
    if (svc) atomic_store(&svc->workspace_pending, 1);
}
#endif

void watch_workspace_stop(WatchService *svc) {
    if (!svc) return;
#ifdef __APPLE__
    FSEventStreamRef stream = (FSEventStreamRef)svc->workspace_stream;
    if (stream) {
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        svc->workspace_stream = NULL;
    }
    dispatch_queue_t queue = (dispatch_queue_t)svc->workspace_queue;
    if (queue) {
#if !OS_OBJECT_USE_OBJC
        dispatch_release(queue);
#endif
        svc->workspace_queue = NULL;
    }
#endif
    atomic_store(&svc->workspace_pending, 0);
}

int watch_workspace_start(WatchService *svc, const char *root) {
    if (!svc || !root) return -1;
    watch_workspace_stop(svc);
#ifdef __APPLE__
    CFStringRef path = CFStringCreateWithCString(NULL, root, kCFStringEncodingUTF8);
    if (!path) return -1;
    CFArrayRef paths = CFArrayCreate(NULL, (const void **)&path, 1,
                                     &kCFTypeArrayCallBacks);
    CFRelease(path);
    if (!paths) return -1;

    FSEventStreamContext ctx = {0, svc, NULL, NULL, NULL};
    FSEventStreamCreateFlags flags =
        kFSEventStreamCreateFlagNoDefer |
        kFSEventStreamCreateFlagWatchRoot |
        kFSEventStreamCreateFlagFileEvents;
    FSEventStreamRef stream =
        FSEventStreamCreate(NULL, workspace_events_cb, &ctx, paths,
                            kFSEventStreamEventIdSinceNow, 0.25, flags);
    CFRelease(paths);
    if (!stream) return -1;

    dispatch_queue_t queue =
        dispatch_queue_create("com.wave.workspace-events", DISPATCH_QUEUE_SERIAL);
    if (!queue) {
        FSEventStreamRelease(stream);
        return -1;
    }
    FSEventStreamSetDispatchQueue(stream, queue);
    if (!FSEventStreamStart(stream)) {
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
#if !OS_OBJECT_USE_OBJC
        dispatch_release(queue);
#endif
        return -1;
    }

    svc->workspace_stream = stream;
    svc->workspace_queue = queue;
    return 0;
#else
    (void)root;
    return 0;
#endif
}

int watch_workspace_consume(WatchService *svc) {
    if (!svc) return 0;
    return atomic_exchange(&svc->workspace_pending, 0) != 0;
}
