#ifndef WAVE_TABS_H
#define WAVE_TABS_H

#include "editor.h"
#include "terminal.h"

#define WAVE_MAX_TABS 32

typedef enum {
    TAB_ITEM_EDITOR,
    TAB_ITEM_TERMINAL
} TabItemKind;

typedef struct {
    TabItemKind kind;
    Editor editor;
    Terminal terminal;
    char label[64];
} TabItem;

typedef struct {
    TabItem items[WAVE_MAX_TABS];
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

typedef struct {
    int has_message;
    int reset_mode;
    char message[256];
} TabWatchEffect;

typedef struct {
    int close_window;
    int reset_mode;
} TabActionEffect;

typedef struct {
    Editor *editor;
    int enter_insert;
} TabStartupEffect;

typedef struct {
    Editor *editor;
    int ok;
    int loaded_file;
    TabOpenKind kind;
} TabOpenResult;

Editor *tabs_current(TabSet *tabs);
const Editor *tabs_current_const(const TabSet *tabs);
Terminal *tabs_current_terminal(TabSet *tabs);
const Terminal *tabs_current_terminal_const(const TabSet *tabs);
TabItemKind tabs_current_kind(const TabSet *tabs);
Editor *tabs_at(TabSet *tabs, int index);
const Editor *tabs_at_const(const TabSet *tabs, int index);
Terminal *tabs_terminal_at(TabSet *tabs, int index);
TabItemKind tabs_kind_at(const TabSet *tabs, int index);
Editor *tabs_new(TabSet *tabs);
Terminal *tabs_new_terminal(TabSet *tabs, const char *label, const char *cwd,
                            const char *const argv[]);
int tabs_close(TabSet *tabs, int index);
void tabs_goto(TabSet *tabs, int delta);
int tabs_set_active(TabSet *tabs, int index);
TabActionEffect tabs_close_with_effect(TabSet *tabs, int index);
TabActionEffect tabs_goto_with_effect(TabSet *tabs, int delta);
TabActionEffect tabs_set_active_with_effect(TabSet *tabs, int index);
TabActionEffect tabs_click_with_effect(TabSet *tabs, int index, int close);
TabStartupEffect tabs_ensure_startup(TabSet *tabs, int workspace_open);
int tabs_find_path(const TabSet *tabs, const char *path);
Editor *tabs_find_preview(TabSet *tabs);
Editor *tabs_find_empty_scratch(TabSet *tabs);
Editor *tabs_use_empty_scratch(TabSet *tabs);
TabOpenPlan tabs_prepare_open(TabSet *tabs, const char *path, int preview);
int tabs_apply_existing_open(TabOpenPlan *plan, int preview);
void tabs_cancel_open(TabSet *tabs, const TabOpenPlan *plan);
TabOpenResult tabs_open_file(TabSet *tabs, const char *path, int preview,
                             WatchService *watch);
Editor *tabs_find_file_watch(TabSet *tabs, int native_id);
int tabs_apply_file_watch_event(TabSet *tabs, WatchService *watch, int native_id,
                                TabDiskChange *out);
int tabs_apply_file_watch_poll(TabSet *tabs, WatchService *watch,
                               TabDiskChange *out, int cap);
TabDiskChangeEffect tabs_describe_disk_change(TabSet *tabs, const TabDiskChange *event,
                                              char *message, size_t message_cap);
TabWatchEffect tabs_process_file_watchers(TabSet *tabs, WatchService *watch,
                                          double now, double *next_poll,
                                          double poll_interval);
int tabs_count(const TabSet *tabs);
int tabs_active_index(const TabSet *tabs);
void tabs_label(const TabSet *tabs, int index, char *out, size_t cap);
void tabs_free(TabSet *tabs);

#endif
