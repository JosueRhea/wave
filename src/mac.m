/* mac.m — macOS-only window chrome: background blur behind the window.
 *
 * Uses the private (but decades-stable, and what Alacritty / kitty / iTerm use)
 * window-server call CGSSetWindowBackgroundBlurRadius to frost whatever is
 * behind the window's transparent areas. Resolved via dlsym so there's no
 * link-time dependency on a private framework, and it simply no-ops if the
 * symbol ever goes away. The window already has a transparent framebuffer
 * (GLFW), so the blur shows through wherever we draw at alpha < 1. */
#import <Cocoa/Cocoa.h>
#import <dlfcn.h>

enum {
    SIDEBAR_MENU_NONE = 0,
    SIDEBAR_MENU_OPEN_FILE = 1,
    SIDEBAR_MENU_OPEN_FOLDER = 2,
    SIDEBAR_MENU_NEW_FILE = 3,
    SIDEBAR_MENU_NEW_FOLDER = 4,
    SIDEBAR_MENU_COPY = 5,
    SIDEBAR_MENU_CUT = 6,
    SIDEBAR_MENU_PASTE = 7,
    SIDEBAR_MENU_DELETE = 8
};

@interface WaveMenuTarget : NSObject
@property(nonatomic) int selection;
- (void)choose:(NSMenuItem *)sender;
@end

static void (*g_open_file_cb)(void);
static void (*g_open_folder_cb)(void);

@interface WaveAppMenuTarget : NSObject
- (void)openFile:(id)sender;
- (void)openFolder:(id)sender;
@end

@implementation WaveMenuTarget
- (void)choose:(NSMenuItem *)sender {
    self.selection = (int)sender.tag;
}
@end

@implementation WaveAppMenuTarget
- (void)openFile:(id)sender {
    (void)sender;
    if (g_open_file_cb) g_open_file_cb();
}
- (void)openFolder:(id)sender {
    (void)sender;
    if (g_open_folder_cb) g_open_folder_cb();
}
@end

static WaveAppMenuTarget *app_menu_target(void) {
    static WaveAppMenuTarget *target;
    if (!target) target = [WaveAppMenuTarget new];
    return target;
}

static NSMenu *ensure_main_menu(void) {
    NSMenu *main = NSApp.mainMenu;
    if (main) return main;

    main = [NSMenu new];
    NSApp.mainMenu = main;

    NSMenuItem *app_item = [NSMenuItem new];
    [main addItem:app_item];
    NSMenu *app_menu = [[NSMenu alloc] initWithTitle:@"Wave"];
    app_item.submenu = app_menu;
    [app_menu addItemWithTitle:@"Quit Wave" action:@selector(terminate:)
                 keyEquivalent:@"q"];
    return main;
}

static NSMenu *ensure_file_menu(void) {
    NSMenu *main = ensure_main_menu();
    for (NSMenuItem *item in main.itemArray) {
        if ([item.title isEqualToString:@"File"] && item.submenu) return item.submenu;
    }

    NSMenuItem *file_item = [[NSMenuItem alloc] initWithTitle:@"File"
                                                       action:nil
                                                keyEquivalent:@""];
    NSMenu *file_menu = [[NSMenu alloc] initWithTitle:@"File"];
    file_item.submenu = file_menu;
    NSInteger index = main.numberOfItems > 0 ? 1 : 0;
    if (index <= main.numberOfItems) [main insertItem:file_item atIndex:index];
    else [main addItem:file_item];
    return file_menu;
}

static void upsert_file_menu_item(NSMenu *menu, NSString *title, SEL action,
                                  NSString *key, NSEventModifierFlags mods) {
    NSMenuItem *item = [menu itemWithTitle:title];
    if (!item) {
        item = [[NSMenuItem alloc] initWithTitle:title
                                          action:action
                                   keyEquivalent:key];
        [menu addItem:item];
    }
    item.action = action;
    item.target = app_menu_target();
    item.keyEquivalent = key;
    item.keyEquivalentModifierMask = mods;
}

void mac_install_app_menu(void (*open_file)(void), void (*open_folder)(void)) {
    g_open_file_cb = open_file;
    g_open_folder_cb = open_folder;
    NSMenu *file = ensure_file_menu();
    upsert_file_menu_item(file, @"Open File...", @selector(openFile:), @"o",
                          NSEventModifierFlagCommand);
    upsert_file_menu_item(file, @"Open Folder...", @selector(openFolder:), @"o",
                          NSEventModifierFlagCommand | NSEventModifierFlagShift);
}

void mac_set_blur(void *win_ptr, int enable) {
    if (!win_ptr) return;
    NSWindow *win = (__bridge NSWindow *)win_ptr;

    typedef int CGSConnectionID;
    typedef int CGSWindowID;
    static CGSConnectionID (*main_conn)(void);
    static int (*set_blur)(CGSConnectionID, CGSWindowID, int);
    static int resolved;
    if (!resolved) {
        resolved = 1;
        main_conn = (CGSConnectionID (*)(void))dlsym(RTLD_DEFAULT, "CGSMainConnectionID");
        set_blur = (int (*)(CGSConnectionID, CGSWindowID, int))
            dlsym(RTLD_DEFAULT, "CGSSetWindowBackgroundBlurRadius");
    }
    if (!main_conn || !set_blur) return;

    set_blur(main_conn(), (CGSWindowID)[win windowNumber], enable ? 30 : 0);
}

/* Make the window's title bar transparent and let the content view run the full
 * height of the window. The result is the macOS pattern used by VS Code / Cursor:
 * the native traffic-light buttons (close / minimise / zoom) float over the
 * top-left of our own chrome, the title text is hidden, and the top ~28pt band
 * stays a draggable title region. We reserve that band in the layout so nothing
 * is drawn under the buttons. */
void mac_use_native_titlebar(void *win_ptr, int enable) {
    if (!win_ptr) return;
    NSWindow *win = (__bridge NSWindow *)win_ptr;
    if (enable) {
        win.titlebarAppearsTransparent = YES;
        win.titleVisibility = NSWindowTitleHidden;
        win.styleMask |= NSWindowStyleMaskFullSizeContentView;
    } else {
        win.titlebarAppearsTransparent = NO;
        win.titleVisibility = NSWindowTitleVisible;
        win.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    }
}

/* The double-click-on-title-bar action, honouring the System Settings choice
 * (Desktop & Dock → "Double-click a window's title bar to" — Zoom / Minimize /
 * Do Nothing). Our GL view now covers the title region and eats those clicks,
 * so we reproduce the standard behaviours ourselves from the GLFW handler. */
void mac_window_titlebar_doubleclick(void *win_ptr) {
    if (!win_ptr) return;
    NSWindow *win = (__bridge NSWindow *)win_ptr;
    NSString *action =
        [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleActionOnDoubleClick"];
    if (!action) action = @"Maximize"; /* macOS default when unset */
    if ([action isEqualToString:@"Minimize"]) [win miniaturize:nil];
    else if ([action isEqualToString:@"Maximize"]) [win zoom:nil];
    /* "None" → do nothing */
}

/* Right-/control-click on the title bar: show the document path menu, the way a
 * Cmd-click on a real title does. Needs a represented file URL (set in
 * mac_use_native_titlebar) — no-ops for unsaved/scratch windows. */
void mac_window_titlebar_menu(void *win_ptr) {
    if (!win_ptr) return;
    NSWindow *win = (__bridge NSWindow *)win_ptr;
    NSButton *icon = [win standardWindowButton:NSWindowDocumentIconButton];
    if (icon) [icon performClick:nil];
}

/* Point the window at the active file so the Cmd-/right-click title path menu
 * and window-proxy behaviours work. A NULL/empty path clears it (scratch). The
 * proxy icon stays hidden (titleVisibility is hidden) but the menu still works. */
void mac_window_set_file(void *win_ptr, const char *path) {
    if (!win_ptr) return;
    NSWindow *win = (__bridge NSWindow *)win_ptr;
    if (path && path[0]) {
        win.representedURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    } else {
        win.representedURL = nil;
    }
}

/* Begin a native window drag using the in-flight mouse-down event. Called while
 * AppKit is dispatching the click, so -[NSApp currentEvent] is that mouseDown;
 * performWindowDragWithEvent: then runs the OS drag loop until mouse-up. */
void mac_window_drag(void *win_ptr) {
    if (!win_ptr) return;
    NSWindow *win = (__bridge NSWindow *)win_ptr;
    NSEvent *ev = [NSApp currentEvent];
    if (ev) [win performWindowDragWithEvent:ev];
}

static void add_menu_item(NSMenu *menu, NSString *title, NSInteger tag,
                          WaveMenuTarget *target) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
                                                  action:@selector(choose:)
                                           keyEquivalent:@""];
    item.tag = tag;
    item.target = target;
    [menu addItem:item];
}

int mac_sidebar_context_menu(void *win_ptr, const char *target_name,
                             int is_dir, int has_target, int has_clipboard) {
    if (!win_ptr) return SIDEBAR_MENU_NONE;
    NSWindow *win = (__bridge NSWindow *)win_ptr;
    WaveMenuTarget *target = [WaveMenuTarget new];
    NSString *title = target_name && target_name[0]
        ? [NSString stringWithUTF8String:target_name]
        : @"Sidebar";
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];

    if (!has_target) {
        add_menu_item(menu, @"Open File...", SIDEBAR_MENU_OPEN_FILE, target);
        add_menu_item(menu, @"Open Folder...", SIDEBAR_MENU_OPEN_FOLDER, target);
        add_menu_item(menu, @"New File...", SIDEBAR_MENU_NEW_FILE, target);
        add_menu_item(menu, @"New Folder...", SIDEBAR_MENU_NEW_FOLDER, target);
    } else if (is_dir) {
        add_menu_item(menu, @"New File...", SIDEBAR_MENU_NEW_FILE, target);
        add_menu_item(menu, @"New Folder...", SIDEBAR_MENU_NEW_FOLDER, target);
    } else {
        add_menu_item(menu, @"Open File", SIDEBAR_MENU_OPEN_FILE, target);
        add_menu_item(menu, @"New File...", SIDEBAR_MENU_NEW_FILE, target);
        add_menu_item(menu, @"New Folder...", SIDEBAR_MENU_NEW_FOLDER, target);
    }

    if (has_clipboard) add_menu_item(menu, @"Paste", SIDEBAR_MENU_PASTE, target);
    if (has_target) {
        [menu addItem:[NSMenuItem separatorItem]];
        add_menu_item(menu, @"Copy", SIDEBAR_MENU_COPY, target);
        add_menu_item(menu, @"Cut", SIDEBAR_MENU_CUT, target);
        add_menu_item(menu, @"Delete", SIDEBAR_MENU_DELETE, target);
    }

    NSPoint p = [win mouseLocationOutsideOfEventStream];
    [menu popUpMenuPositioningItem:nil atLocation:p inView:win.contentView];
    return target.selection;
}

int mac_open_panel(void *win_ptr, int folders, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = folders ? YES : NO;
    panel.canChooseFiles = folders ? NO : YES;
    panel.allowsMultipleSelection = NO;
    panel.canCreateDirectories = folders ? YES : NO;
    NSWindow *win = win_ptr ? (__bridge NSWindow *)win_ptr : nil;
    NSModalResponse response = win ? [panel runModal] : [panel runModal];
    if (response != NSModalResponseOK || panel.URLs.count < 1) return 0;
    snprintf(out, cap, "%s", panel.URL.path.UTF8String);
    return 1;
}

int mac_confirm_delete(void *win_ptr, const char *path) {
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Delete?";
    NSString *detail = path && path[0]
        ? [NSString stringWithFormat:@"Delete %@ from disk?", [NSString stringWithUTF8String:path]]
        : @"Delete this item from disk?";
    alert.informativeText = detail;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"Delete"];
    [alert addButtonWithTitle:@"Cancel"];
    NSWindow *win = win_ptr ? (__bridge NSWindow *)win_ptr : nil;
    NSModalResponse response = win ? [alert runModal] : [alert runModal];
    return response == NSAlertFirstButtonReturn;
}
