#include "tabs.h"

#include <string.h>

#include "buffer.h"

Editor *tabs_current(TabSet *tabs) {
    if (!tabs || tabs->count <= 0) return NULL;
    if (tabs->active < 0) tabs->active = 0;
    if (tabs->active >= tabs->count) tabs->active = tabs->count - 1;
    return &tabs->items[tabs->active];
}

const Editor *tabs_current_const(const TabSet *tabs) {
    if (!tabs || tabs->count <= 0 || tabs->active < 0 || tabs->active >= tabs->count)
        return NULL;
    return &tabs->items[tabs->active];
}

Editor *tabs_at(TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return NULL;
    return &tabs->items[index];
}

const Editor *tabs_at_const(const TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return NULL;
    return &tabs->items[index];
}

Editor *tabs_new(TabSet *tabs) {
    if (!tabs) return NULL;
    if (tabs->count >= WAVE_MAX_TABS) {
        Editor *e = tabs_current(tabs);
        if (e) editor_close(e);
        return e;
    }
    tabs->active = tabs->count++;
    Editor *e = tabs_current(tabs);
    if (e) editor_init(e);
    return e;
}

int tabs_close(TabSet *tabs, int index) {
    if (!tabs || index < 0 || index >= tabs->count) return tabs ? tabs->count : 0;
    editor_close(&tabs->items[index]);
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

int tabs_find_path(const TabSet *tabs, const char *path) {
    if (!tabs || !path) return -1;
    for (int i = 0; i < tabs->count; i++)
        if (tabs->items[i].path && !strcmp(tabs->items[i].path, path))
            return i;
    return -1;
}

Editor *tabs_find_preview(TabSet *tabs) {
    if (!tabs) return NULL;
    for (int i = 0; i < tabs->count; i++) {
        if (tabs->items[i].preview) {
            tabs->active = i;
            return &tabs->items[i];
        }
    }
    return NULL;
}

Editor *tabs_find_empty_scratch(TabSet *tabs) {
    if (!tabs || tabs->count != 1) return NULL;
    Editor *e = &tabs->items[0];
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

Editor *tabs_find_file_watch(TabSet *tabs, int native_id) {
    if (!tabs || native_id < 0) return NULL;
    for (int i = 0; i < tabs->count; i++) {
        Editor *e = &tabs->items[i];
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
        Editor *e = &tabs->items[i];
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

int tabs_count(const TabSet *tabs) {
    return tabs ? tabs->count : 0;
}

int tabs_active_index(const TabSet *tabs) {
    return tabs ? tabs->active : 0;
}

void tabs_free(TabSet *tabs) {
    if (!tabs) return;
    for (int i = 0; i < tabs->count; i++) editor_close(&tabs->items[i]);
    tabs->count = 0;
    tabs->active = 0;
}
