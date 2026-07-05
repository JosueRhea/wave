#include "tabs.h"

#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "runtime.h"
#include "view.h"

static void tab_item_init_editor(TabItem *item) {
    if (!item) return;
    memset(item, 0, sizeof *item);
    item->kind = TAB_ITEM_EDITOR;
    editor_init(&item->editor);
}

static void tab_item_init_terminal(TabItem *item) {
    if (!item) return;
    memset(item, 0, sizeof *item);
    item->kind = TAB_ITEM_TERMINAL;
    terminal_init(&item->terminal);
}

static void tab_item_close(TabItem *item) {
    if (!item) return;
    if (item->kind == TAB_ITEM_TERMINAL)
        terminal_free(&item->terminal);
    else
        editor_close(&item->editor);
}

Editor *tabs_current(TabSet *tabs) {
    if (!tabs || tabs->count <= 0) return NULL;
    if (tabs->active < 0) tabs->active = 0;
    if (tabs->active >= tabs->count) tabs->active = tabs->count - 1;
    if (tabs->items[tabs->active].kind != TAB_ITEM_EDITOR) return NULL;
    return &tabs->items[tabs->active].editor;
}

const Editor *tabs_current_const(const TabSet *tabs) {
    if (!tabs || tabs->count <= 0 || tabs->active < 0 || tabs->active >= tabs->count)
        return NULL;
    if (tabs->items[tabs->active].kind != TAB_ITEM_EDITOR) return NULL;
    return &tabs->items[tabs->active].editor;
}

Terminal *tabs_current_terminal(TabSet *tabs) {
    if (!tabs || tabs->count <= 0) return NULL;
    if (tabs->active < 0) tabs->active = 0;
    if (tabs->active >= tabs->count) tabs->active = tabs->count - 1;
    if (tabs->items[tabs->active].kind != TAB_ITEM_TERMINAL) return NULL;
    return &tabs->items[tabs->active].terminal;
}

const Terminal *tabs_current_terminal_const(const TabSet *tabs) {
    if (!tabs || tabs->count <= 0 || tabs->active < 0 || tabs->active >= tabs->count)
        return NULL;
    if (tabs->items[tabs->active].kind != TAB_ITEM_TERMINAL) return NULL;
    return &tabs->items[tabs->active].terminal;
}

TabItemKind tabs_current_kind(const TabSet *tabs) {
    if (!tabs || tabs->count <= 0 || tabs->active < 0 || tabs->active >= tabs->count)
        return TAB_ITEM_EDITOR;
    return tabs->items[tabs->active].kind;
}

Editor *tabs_at(TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return NULL;
    if (tabs->items[index].kind != TAB_ITEM_EDITOR) return NULL;
    return &tabs->items[index].editor;
}

const Editor *tabs_at_const(const TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return NULL;
    if (tabs->items[index].kind != TAB_ITEM_EDITOR) return NULL;
    return &tabs->items[index].editor;
}

Terminal *tabs_terminal_at(TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return NULL;
    if (tabs->items[index].kind != TAB_ITEM_TERMINAL) return NULL;
    return &tabs->items[index].terminal;
}

TabItemKind tabs_kind_at(const TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return TAB_ITEM_EDITOR;
    return tabs->items[index].kind;
}

Editor *tabs_new(TabSet *tabs) {
    if (!tabs) return NULL;
    if (tabs->count >= WAVE_MAX_TABS) {
        Editor *e = tabs_current(tabs);
        if (e) editor_close(e);
        return e;
    }
    tabs->active = tabs->count++;
    tab_item_init_editor(&tabs->items[tabs->active]);
    return &tabs->items[tabs->active].editor;
}

Terminal *tabs_new_terminal(TabSet *tabs, const char *label, const char *cwd,
                            const char *const argv[]) {
    if (!tabs || !argv || !argv[0]) return NULL;
    if (tabs->count >= WAVE_MAX_TABS) return tabs_current_terminal(tabs);
    tabs->active = tabs->count++;
    TabItem *item = &tabs->items[tabs->active];
    tab_item_init_terminal(item);
    snprintf(item->label, sizeof item->label, "%s", label ? label : argv[0]);
    if (!terminal_spawn(&item->terminal, item->label, cwd, argv)) return &item->terminal;
    return &item->terminal;
}

int tabs_close(TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return tabs ? tabs->count : 0;
    tab_item_close(&tabs->items[index]);
    for (int j = index; j < tabs->count - 1; j++) tabs->items[j] = tabs->items[j + 1];
    tabs->count--;
    if (tabs->count <= 0) {
        tabs->active = 0;
        return 0;
    }
    if (tabs->active >= tabs->count) tabs->active = tabs->count - 1;
    return tabs->count;
}

void tabs_goto(TabSet *tabs, int delta) {
    if (!tabs || tabs->count <= 0) return;
    tabs->active = (tabs->active + delta % tabs->count + tabs->count) % tabs->count;
}

int tabs_set_active(TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return 0;
    tabs->active = index;
    return 1;
}

TabActionEffect tabs_close_with_effect(TabSet *tabs, int index) {
    TabActionEffect effect = {0};
    int before = tabs ? tabs->count : 0;
    int remaining = tabs_close(tabs, index);
    if (before <= 0) return effect;
    if (remaining <= 0) effect.close_window = 1;
    else effect.reset_mode = 1;
    return effect;
}

TabActionEffect tabs_goto_with_effect(TabSet *tabs, int delta) {
    TabActionEffect effect = {0};
    if (!tabs || tabs->count <= 0) return effect;
    tabs_goto(tabs, delta);
    effect.reset_mode = 1;
    return effect;
}

TabActionEffect tabs_set_active_with_effect(TabSet *tabs, int index) {
    TabActionEffect effect = {0};
    if (tabs_set_active(tabs, index)) effect.reset_mode = 1;
    return effect;
}

TabActionEffect tabs_click_with_effect(TabSet *tabs, int index, int close) {
    if (!tabs || index < 0 || index >= tabs->count) return (TabActionEffect){0};
    if (close) return tabs_close_with_effect(tabs, index);
    return tabs_set_active_with_effect(tabs, index);
}

TabStartupEffect tabs_ensure_startup(TabSet *tabs, int workspace_open) {
    TabStartupEffect effect = {0};
    if (!tabs) return effect;
    if (tabs->count > 0) {
        effect.editor = tabs_current(tabs);
        return effect;
    }

    effect.editor = tabs_new(tabs);
    if (!effect.editor) return effect;
    if (!workspace_open) {
        effect.editor->buf = buffer_new();
        effect.enter_insert = effect.editor->buf != NULL;
    }
    return effect;
}

int tabs_find_path(const TabSet *tabs, const char *path) {
    if (!tabs || !path) return -1;
    for (int i = 0; i < tabs->count; i++)
        if (tabs->items[i].kind == TAB_ITEM_EDITOR &&
            tabs->items[i].editor.path && !strcmp(tabs->items[i].editor.path, path))
            return i;
    return -1;
}

Editor *tabs_find_preview(TabSet *tabs) {
    if (!tabs) return NULL;
    for (int i = 0; i < tabs->count; i++) {
        if (tabs->items[i].kind == TAB_ITEM_EDITOR && tabs->items[i].editor.preview) {
            tabs->active = i;
            return &tabs->items[i].editor;
        }
    }
    return NULL;
}

Editor *tabs_find_empty_scratch(TabSet *tabs) {
    if (!tabs || tabs->count != 1) return NULL;
    if (tabs->items[0].kind != TAB_ITEM_EDITOR) return NULL;
    Editor *e = &tabs->items[0].editor;
    if (e->path || e->modified || !e->buf || buffer_length(e->buf) != 0)
        return NULL;
    tabs->active = 0;
    return e;
}

Editor *tabs_use_empty_scratch(TabSet *tabs) {
    Editor *e = tabs_find_empty_scratch(tabs);
    if (!e) return NULL;
    editor_close(e);
    return e;
}

TabOpenPlan tabs_prepare_open(TabSet *tabs, const char *path, int preview) {
    TabOpenPlan plan = {0};
    if (!tabs || !path) return plan;

    int existing = tabs_find_path(tabs, path);
    if (existing >= 0) {
        tabs_set_active(tabs, existing);
        plan.editor = tabs_at(tabs, existing);
        plan.kind = TAB_OPEN_EXISTING;
        plan.index = existing;
        return plan;
    }

    if (preview) {
        Editor *slot = tabs_find_preview(tabs);
        if (slot) {
            plan.editor = slot;
            plan.kind = TAB_OPEN_REUSE_PREVIEW;
            plan.index = tabs_active_index(tabs);
            return plan;
        }
    }

    Editor *scratch = tabs_find_empty_scratch(tabs);
    if (scratch) {
        plan.editor = scratch;
        plan.kind = TAB_OPEN_REUSE_SCRATCH;
        plan.index = tabs_active_index(tabs);
        return plan;
    }

    Editor *created = tabs_new(tabs);
    plan.editor = created;
    plan.kind = TAB_OPEN_NEW;
    plan.index = tabs_active_index(tabs);
    return plan;
}

int tabs_apply_existing_open(TabOpenPlan *plan, int preview) {
    if (!plan || plan->kind != TAB_OPEN_EXISTING || !plan->editor) return 0;
    if (!preview) plan->editor->preview = 0;
    return 1;
}

void tabs_cancel_open(TabSet *tabs, const TabOpenPlan *plan) {
    if (!tabs || !plan) return;
    if (plan->kind == TAB_OPEN_NEW) tabs_close(tabs, plan->index);
}

TabOpenResult tabs_open_file(TabSet *tabs, const char *path, int preview,
                             WatchService *watch) {
    TabOpenResult result = {0};
    TabOpenPlan plan = tabs_prepare_open(tabs, path, preview);
    result.editor = plan.editor;
    result.kind = plan.kind;
    if (!plan.editor) return result;

    if (tabs_apply_existing_open(&plan, preview)) {
        result.ok = 1;
        return result;
    }

    if (editor_open_file(plan.editor, path, preview, watch) != 0) {
        tabs_cancel_open(tabs, &plan);
        result.editor = NULL;
        return result;
    }

    result.editor = plan.editor;
    result.ok = 1;
    result.loaded_file = 1;
    return result;
}

Editor *tabs_find_file_watch(TabSet *tabs, int native_id) {
    if (!tabs || native_id < 0) return NULL;
    for (int i = 0; i < tabs->count; i++) {
        if (tabs->items[i].kind != TAB_ITEM_EDITOR) continue;
        Editor *e = &tabs->items[i].editor;
        if (watch_file_native_id(&e->watch) == native_id) return e;
    }
    return NULL;
}

int tabs_apply_file_watch_event(TabSet *tabs, WatchService *watch, int native_id,
                                TabDiskChange *out) {
    Editor *e = tabs_find_file_watch(tabs, native_id);
    if (!e) return 0;
    EditorDiskChange change = editor_apply_disk_change(e, watch);
    if (out) *out = (TabDiskChange){ e, change };
    return 1;
}

int tabs_apply_file_watch_poll(TabSet *tabs, WatchService *watch,
                               TabDiskChange *out, int cap) {
    if (!tabs || !out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < tabs->count && n < cap; i++) {
        if (tabs->items[i].kind != TAB_ITEM_EDITOR) continue;
        Editor *e = &tabs->items[i].editor;
        EditorDiskChange change = editor_apply_disk_change(e, watch);
        if (change == EDITOR_DISK_NOOP) continue;
        out[n++] = (TabDiskChange){ e, change };
    }
    return n;
}

TabDiskChangeEffect tabs_describe_disk_change(TabSet *tabs, const TabDiskChange *event,
                                              char *message, size_t message_cap) {
    TabDiskChangeEffect effect = {0};
    if (!event || !event->editor || event->change == EDITOR_DISK_NOOP) return effect;
    effect.has_message = editor_disk_change_message(event->editor, event->change,
                                                    message, message_cap);
    effect.reset_active_mode = event->change == EDITOR_DISK_RELOADED &&
                               event->editor == tabs_current(tabs);
    return effect;
}

static void tabs_accumulate_watch_effect(TabSet *tabs, const TabDiskChange *event,
                                         TabWatchEffect *out) {
    if (!out) return;
    TabDiskChangeEffect effect = tabs_describe_disk_change(
        tabs, event, out->message, sizeof out->message);
    if (effect.has_message) out->has_message = 1;
    if (effect.reset_active_mode) out->reset_mode = 1;
}

TabWatchEffect tabs_process_file_watchers(TabSet *tabs, WatchService *watch,
                                          double now, double *next_poll,
                                          double poll_interval) {
    TabWatchEffect effect = {0};
#ifdef __APPLE__
    (void)now;
    (void)next_poll;
    (void)poll_interval;
    for (;;) {
        int ids[16];
        int n = watch_poll_file_events(watch, ids, 16);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            TabDiskChange event;
            if (tabs_apply_file_watch_event(tabs, watch, ids[i], &event))
                tabs_accumulate_watch_effect(tabs, &event, &effect);
        }
        if (n < 16) break;
    }
#else
    if (!wave_runtime_periodic_due(now, next_poll, poll_interval)) return effect;
    TabDiskChange events[16];
    int n = tabs_apply_file_watch_poll(tabs, watch, events, 16);
    for (int i = 0; i < n; i++)
        tabs_accumulate_watch_effect(tabs, &events[i], &effect);
#endif
    return effect;
}

int tabs_count(const TabSet *tabs) {
    return tabs ? tabs->count : 0;
}

int tabs_active_index(const TabSet *tabs) {
    return tabs ? tabs->active : 0;
}

void tabs_label(const TabSet *tabs, int index, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!tabs || index < 0 || index >= tabs->count) return;
    const TabItem *item = &tabs->items[index];
    if (item->kind == TAB_ITEM_TERMINAL) {
        snprintf(out, cap, "%s", item->label[0] ? item->label : "terminal");
        return;
    }
    view_tab_label(&item->editor, out, cap);
}

void tabs_free(TabSet *tabs) {
    if (!tabs) return;
    for (int i = 0; i < tabs->count; i++) tab_item_close(&tabs->items[i]);
    tabs->count = 0;
    tabs->active = 0;
}
