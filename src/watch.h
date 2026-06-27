#ifndef WAVE_WATCH_H
#define WAVE_WATCH_H

#include <sys/stat.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <time.h>

typedef struct {
    int seen;
    time_t mtime_sec;
    long mtime_nsec;
    time_t ctime_sec;
    long ctime_nsec;
    off_t size;
    int native_fd;
} FileWatch;

typedef struct {
    int file_kq;
    void *workspace_stream;
    void *workspace_queue;
    atomic_int workspace_pending;
} WatchService;

void watch_service_init(WatchService *svc);
void watch_service_shutdown(WatchService *svc);

void watch_file_init(FileWatch *fw);
void watch_file_stop(FileWatch *fw);
int watch_file_start(WatchService *svc, FileWatch *fw, const char *path);
void watch_file_mark_seen(FileWatch *fw, const struct stat *st);
int watch_file_stat_changed(const FileWatch *fw, const struct stat *st);
int watch_file_native_id(const FileWatch *fw);
int watch_poll_file_events(WatchService *svc, int *ids, int cap);

void watch_workspace_stop(WatchService *svc);
int watch_workspace_start(WatchService *svc, const char *root);
int watch_workspace_consume(WatchService *svc);

#endif
