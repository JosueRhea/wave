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
#import <objc/runtime.h>

#include "updater.h"

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
static void (*g_check_updates_cb)(void);

typedef void (*MacUpdateCallback)(int state, const char *version,
                                  const char *detail, double progress);

enum {
    UPDATE_STATE_CHECKING = 1,
    UPDATE_STATE_CURRENT = 2,
    UPDATE_STATE_AVAILABLE = 3,
    UPDATE_STATE_DOWNLOADING = 4,
    UPDATE_STATE_ERROR = 5,
    UPDATE_STATE_DOWNLOADED = 6
};

@interface WaveAppMenuTarget : NSObject
- (void)openFile:(id)sender;
- (void)openFolder:(id)sender;
- (void)checkUpdates:(id)sender;
@end

@interface WaveDownloadDelegate : NSObject <NSURLSessionDownloadDelegate>
@property(nonatomic, copy) NSString *version;
@property(nonatomic) MacUpdateCallback callback;
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
- (void)checkUpdates:(id)sender {
    (void)sender;
    if (g_check_updates_cb) g_check_updates_cb();
}
@end

@implementation WaveDownloadDelegate
- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)downloadTask
      didWriteData:(int64_t)bytesWritten
 totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
    (void)downloadTask; (void)bytesWritten;
    double progress = 0.0;
    if (totalBytesExpectedToWrite > 0)
        progress = (double)totalBytesWritten / (double)totalBytesExpectedToWrite;
    MacUpdateCallback cb = self.callback;
    NSString *version = self.version ?: @"";
    dispatch_async(dispatch_get_main_queue(), ^{
        if (cb) cb(UPDATE_STATE_DOWNLOADING, version.UTF8String, "", progress);
    });
    (void)session;
}

- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)downloadTask
didFinishDownloadingToURL:(NSURL *)location {
    (void)downloadTask;
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *downloads = [fm URLForDirectory:NSDownloadsDirectory
                                  inDomain:NSUserDomainMask
                         appropriateForURL:nil
                                    create:YES
                                     error:nil];
    NSString *filename = [NSString stringWithFormat:@"Wave-%@-macos.dmg",
                          self.version ?: @"update"];
    NSURL *dest = [downloads URLByAppendingPathComponent:filename];
    [fm removeItemAtURL:dest error:nil];
    NSError *error = nil;
    BOOL ok = [fm moveItemAtURL:location toURL:dest error:&error];
    MacUpdateCallback cb = self.callback;
    NSString *version = self.version ?: @"";
    if (!ok) {
        NSString *msg = error.localizedDescription ?: @"could not save update";
        dispatch_async(dispatch_get_main_queue(), ^{
            if (cb) cb(UPDATE_STATE_ERROR, version.UTF8String, msg.UTF8String, 0.0);
        });
        [session finishTasksAndInvalidate];
        return;
    }
    [[NSWorkspace sharedWorkspace] openURL:dest];
    dispatch_async(dispatch_get_main_queue(), ^{
        if (cb) cb(UPDATE_STATE_DOWNLOADED, version.UTF8String,
                   dest.path.lastPathComponent.UTF8String, 1.0);
    });
    [session finishTasksAndInvalidate];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error {
    (void)task;
    if (error) {
        MacUpdateCallback cb = self.callback;
        NSString *version = self.version ?: @"";
        NSString *msg = error.localizedDescription ?: @"download failed";
        dispatch_async(dispatch_get_main_queue(), ^{
            if (cb) cb(UPDATE_STATE_ERROR, version.UTF8String, msg.UTF8String, 0.0);
        });
    }
    (void)session;
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

static NSMenu *ensure_app_menu(void) {
    NSMenu *main = ensure_main_menu();
    if (main.numberOfItems > 0) {
        NSMenuItem *item = [main itemAtIndex:0];
        if (item.submenu) return item.submenu;
    }
    NSMenuItem *app_item = [NSMenuItem new];
    [main insertItem:app_item atIndex:0];
    NSMenu *app_menu = [[NSMenu alloc] initWithTitle:@"Wave"];
    app_item.submenu = app_menu;
    return app_menu;
}

static void upsert_menu_item(NSMenu *menu, NSString *title, SEL action,
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

void mac_install_app_menu(void (*open_file)(void), void (*open_folder)(void),
                          void (*check_updates)(void)) {
    g_open_file_cb = open_file;
    g_open_folder_cb = open_folder;
    g_check_updates_cb = check_updates;
    NSMenu *app = ensure_app_menu();
    upsert_menu_item(app, @"Check for Updates...", @selector(checkUpdates:), @"",
                     0);
    NSMenu *file = ensure_file_menu();
    upsert_menu_item(file, @"Open File...", @selector(openFile:), @"o",
                     NSEventModifierFlagCommand);
    upsert_menu_item(file, @"Open Folder...", @selector(openFolder:), @"o",
                     NSEventModifierFlagCommand | NSEventModifierFlagShift);
    if ([file numberOfItems] > 0) [file addItem:[NSMenuItem separatorItem]];
    upsert_menu_item(file, @"Check for Updates...", @selector(checkUpdates:), @"",
                     0);
}

static void updater_emit(MacUpdateCallback cb, int state, NSString *version,
                         NSString *detail, double progress) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (cb) cb(state, version.UTF8String, detail.UTF8String, progress);
    });
}

static NSURL *release_dmg_asset_url(NSDictionary *release) {
    NSArray *assets = release[@"assets"];
    if (![assets isKindOfClass:[NSArray class]]) return nil;
    for (NSDictionary *asset in assets) {
        if (![asset isKindOfClass:[NSDictionary class]]) continue;
        NSString *name = asset[@"name"];
        NSString *url = asset[@"browser_download_url"];
        if (![name isKindOfClass:[NSString class]] ||
            ![url isKindOfClass:[NSString class]]) continue;
        if ([name hasSuffix:@".dmg"] && [name containsString:@"macos"])
            return [NSURL URLWithString:url];
    }
    return nil;
}

static void start_update_download(NSURL *url, NSString *version,
                                  MacUpdateCallback callback) {
    WaveDownloadDelegate *delegate = [WaveDownloadDelegate new];
    delegate.version = version;
    delegate.callback = callback;
    NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
    config.HTTPAdditionalHeaders = @{@"User-Agent": @"Wave-Updater"};
    NSURLSession *session = [NSURLSession sessionWithConfiguration:config
                                                          delegate:delegate
                                                     delegateQueue:nil];
    NSURLSessionDownloadTask *task = [session downloadTaskWithURL:url];
    objc_setAssociatedObject(task, "wave-download-delegate", delegate,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [task resume];
}

void mac_check_for_updates(const char *current_version, int manual,
                           MacUpdateCallback callback) {
    NSString *current = current_version
        ? [NSString stringWithUTF8String:current_version] : @"0.0.0";
    if (manual) updater_emit(callback, UPDATE_STATE_CHECKING, current, @"", 0.0);

    NSURL *url = [NSURL URLWithString:@"https://api.github.com/repos/JosueRhea/wave/releases/latest"];
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    req.HTTPMethod = @"GET";
    [req setValue:@"Wave-Updater" forHTTPHeaderField:@"User-Agent"];
    [req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

    NSURLSessionDataTask *task = [[NSURLSession sharedSession]
        dataTaskWithRequest:req
          completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        if (error) {
            if (manual) updater_emit(callback, UPDATE_STATE_ERROR, current,
                                     error.localizedDescription ?: @"request failed", 0.0);
            return;
        }
        NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
        if (![http isKindOfClass:[NSHTTPURLResponse class]] ||
            http.statusCode < 200 || http.statusCode >= 300) {
            if (manual) {
                NSString *msg = [NSString stringWithFormat:@"GitHub returned %ld",
                                 (long)http.statusCode];
                updater_emit(callback, UPDATE_STATE_ERROR, current, msg, 0.0);
            }
            return;
        }
        NSError *json_error = nil;
        NSDictionary *release = [NSJSONSerialization JSONObjectWithData:data
                                                                 options:0
                                                                   error:&json_error];
        if (![release isKindOfClass:[NSDictionary class]]) {
            if (manual) updater_emit(callback, UPDATE_STATE_ERROR, current,
                                     json_error.localizedDescription ?: @"invalid release data", 0.0);
            return;
        }
        NSString *tag = release[@"tag_name"];
        if (![tag isKindOfClass:[NSString class]] || tag.length == 0) {
            if (manual) updater_emit(callback, UPDATE_STATE_ERROR, current,
                                     @"release has no tag", 0.0);
            return;
        }
        if (!wave_version_is_newer(tag.UTF8String, current.UTF8String)) {
            if (manual) updater_emit(callback, UPDATE_STATE_CURRENT, current, @"", 0.0);
            return;
        }

        NSURL *asset_url = release_dmg_asset_url(release);
        if (!asset_url) {
            updater_emit(callback, UPDATE_STATE_ERROR, tag,
                         @"release has no macOS DMG", 0.0);
            return;
        }
        updater_emit(callback, UPDATE_STATE_AVAILABLE, tag, @"", 0.0);
        start_update_download(asset_url, tag, callback);
    }];
    [task resume];
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
