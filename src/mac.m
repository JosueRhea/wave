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
