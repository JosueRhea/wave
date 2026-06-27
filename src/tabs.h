#ifndef WAVE_TABS_H
#define WAVE_TABS_H

#include "editor.h"

#define WAVE_MAX_TABS 32

typedef struct {
    Editor items[WAVE_MAX_TABS];
    int count;
    int active;
} TabSet;

typedef enum {
    TAB_OPEN_EXISTING,
    TAB_OPEN_REUSE_PREVIEW,
    TAB_OPEN_REUSE_SCRATCH,
    TAB_OPEN_NEW,
} TabOpenKind;

typedef struct {
    Editor *editor;
    TabOpenKind kind;
    int index;
} TabOpenPlan;

typedef struct {
    Editor *editor;
    EditorDiskChange change;
} TabDiskChange;

typedef struct {
    int has_message;
    int reset_active_mode;
} TabDiskChangeEffect;

Editor *tabs_current(TabSet *tabs);
const Editor *tabs_current_const(const TabSet *tabs);
Editor *tabs_at(TabSet *tabs, int index);
const Editor *tabs_at_const(const TabSet *tabs, int index);
Editor *tabs_new(TabSet *tabs);
int tabs_close(TabSet *tabs, int index);
void tabs_goto(TabSet *tabs, int delta);
int tabs_set_active(TabSet *tabs, int index);
int tabs_find_path(const TabSet *tabs, const char *path);
Editor *tabs_find_preview(TabSet *tabs);
Editor *tabs_find_empty_scratch(TabSet *tabs);
Editor *tabs_use_empty_scratch(TabSet *tabs);
TabOpenPlan tabs_prepare_open(TabSet *tabs, const char *path, int preview);
int tabs_apply_existing_open(TabOpenPlan *plan, int preview);
void tabs_cancel_open(TabSet *tabs, const TabOpenPlan *plan);
Editor *tabs_find_file_watch(TabSet *tabs, int native_id);
int tabs_apply_file_watch_event(TabSet *tabs, WatchService *watch, int native_id,
                                TabDiskChange *out);
int tabs_apply_file_watch_poll(TabSet *tabs, WatchService *watch,
                               TabDiskChange *out, int cap);
TabDiskChangeEffect tabs_describe_disk_change(TabSet *tabs, const TabDiskChange *event,
                                              char *message, size_t message_cap);
int tabs_count(const TabSet *tabs);
int tabs_active_index(const TabSet *tabs);
void tabs_free(TabSet *tabs);

#endif
