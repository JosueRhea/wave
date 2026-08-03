/* updater_mac.m — the macOS half of the auto-updater: ask GitHub for the latest
 * release, download its .dmg, and swap the running Wave.app for the one inside.
 *
 * This lives apart from mac.m, and in the *core* object set rather than the
 * GLFW-only one, because both front-ends need it. Until 0.1.17 all of this sat
 * in mac.m, which is linked only into the GLFW binary — so when Wave.app started
 * shipping the GPUI front-end the updater silently went away with it, and every
 * install from that release was stranded on manual downloads. Nothing here
 * touches GLFW or OpenGL; it is Foundation + one AppKit call to quit, so it
 * links into either front-end unchanged.
 *
 * The version comparison itself is in updater.c (pure C, tested); this file is
 * only the I/O around it.
 */
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <unistd.h>

#include "updater.h"

@interface WaveDownloadDelegate : NSObject <NSURLSessionDownloadDelegate>
@property(nonatomic, copy) NSString *version;
@property(nonatomic) WaveUpdateCallback callback;
@end

/* The shipped version is whatever Info.plist says; `fallback` is the value
 * compiled in, which is what a non-bundled dev build runs with. */
static NSString *current_bundle_version(const char *fallback) {
    NSString *version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if ([version isKindOfClass:[NSString class]] && version.length > 0) return version;
    return fallback ? [NSString stringWithUTF8String:fallback] : @"0.0.0";
}

/* Hand the swap to a detached shell script: it waits for this process to exit
 * (a running .app cannot replace itself), ditto's the new bundle into place, and
 * relaunches. The script deletes itself and the .dmg on the way out. */
static BOOL wave_apply_update_from_dmg(NSString *dmgPath, NSString *version,
                                       WaveUpdateCallback callback) {
    NSString *appPath = [NSBundle mainBundle].bundlePath;
    if (![appPath hasSuffix:@".app"] || dmgPath.length == 0) return NO;

    NSString *scriptPath = [NSTemporaryDirectory()
        stringByAppendingPathComponent:[NSString stringWithFormat:@"wave-update-%@.sh",
                                        [[NSUUID UUID] UUIDString]]];
    NSString *script =
        @"#!/bin/sh\n"
        "set -eu\n"
        "APP=\"$1\"\n"
        "DMG=\"$2\"\n"
        "PID=\"$3\"\n"
        "MOUNT=$(mktemp -d /tmp/wave-update-mount.XXXXXX)\n"
        "cleanup() { hdiutil detach \"$MOUNT\" -quiet >/dev/null 2>&1 || true; rm -rf \"$MOUNT\"; rm -f \"$DMG\" \"$0\"; }\n"
        "trap cleanup EXIT\n"
        "hdiutil attach \"$DMG\" -nobrowse -quiet -mountpoint \"$MOUNT\"\n"
        "NEWAPP=\"$MOUNT/Wave.app\"\n"
        "test -d \"$NEWAPP\"\n"
        "while kill -0 \"$PID\" >/dev/null 2>&1; do sleep 0.2; done\n"
        "TMP=\"${APP}.updating\"\n"
        "rm -rf \"$TMP\"\n"
        "/usr/bin/ditto \"$NEWAPP\" \"$TMP\"\n"
        "rm -rf \"$APP\"\n"
        "mv \"$TMP\" \"$APP\"\n"
        "open -n \"$APP\"\n";

    NSError *error = nil;
    if (![script writeToFile:scriptPath atomically:YES
                    encoding:NSUTF8StringEncoding error:&error]) {
        (void)callback;
        return NO;
    }

    NSTask *task = [NSTask new];
    task.launchPath = @"/bin/sh";
    task.arguments = @[scriptPath, appPath, dmgPath,
                       [NSString stringWithFormat:@"%d", getpid()]];
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    @try {
        [task launch];
    } @catch (NSException *exception) {
        (void)exception;
        return NO;
    }

    /* Give the front-end a frame to paint "installing" before we go away. */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.8 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
    return YES;
}

@implementation WaveDownloadDelegate
- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)downloadTask
      didWriteData:(int64_t)bytesWritten
 totalBytesWritten:(int64_t)totalBytesWritten
totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
    (void)downloadTask; (void)bytesWritten;
    double progress = 0.0;
    if (totalBytesExpectedToWrite > 0)
        progress = (double)totalBytesWritten / (double)totalBytesExpectedToWrite;
    WaveUpdateCallback cb = self.callback;
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
    NSURL *cache = [fm URLForDirectory:NSCachesDirectory
                              inDomain:NSUserDomainMask
                     appropriateForURL:nil
                                create:YES
                                 error:nil];
    NSURL *updates = [cache URLByAppendingPathComponent:@"com.gzenit.wave/Updates"
                                           isDirectory:YES];
    [fm createDirectoryAtURL:updates withIntermediateDirectories:YES
                  attributes:nil error:nil];
    NSString *filename = [NSString stringWithFormat:@"Wave-%@-macos.dmg",
                          self.version ?: @"update"];
    NSURL *dest = [updates URLByAppendingPathComponent:filename];
    [fm removeItemAtURL:dest error:nil];
    NSError *error = nil;
    BOOL ok = [fm moveItemAtURL:location toURL:dest error:&error];
    WaveUpdateCallback cb = self.callback;
    NSString *version = self.version ?: @"";
    if (!ok) {
        NSString *msg = error.localizedDescription ?: @"could not save update";
        dispatch_async(dispatch_get_main_queue(), ^{
            if (cb) cb(UPDATE_STATE_ERROR, version.UTF8String, msg.UTF8String, 0.0);
        });
        [session finishTasksAndInvalidate];
        return;
    }
    if (!wave_apply_update_from_dmg(dest.path, version, cb)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (cb) cb(UPDATE_STATE_ERROR, version.UTF8String,
                       "could not start installer", 0.0);
        });
        [session finishTasksAndInvalidate];
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        if (cb) cb(UPDATE_STATE_DOWNLOADED, version.UTF8String,
                   "restarting Wave", 1.0);
    });
    [session finishTasksAndInvalidate];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error {
    (void)task;
    if (error) {
        WaveUpdateCallback cb = self.callback;
        NSString *version = self.version ?: @"";
        NSString *msg = error.localizedDescription ?: @"download failed";
        dispatch_async(dispatch_get_main_queue(), ^{
            if (cb) cb(UPDATE_STATE_ERROR, version.UTF8String, msg.UTF8String, 0.0);
        });
    }
    (void)session;
}
@end

/* Every state change reaches the front-end on the main thread, so a front-end
 * can touch its own state from the callback without locking. */
static void updater_emit(WaveUpdateCallback cb, int state, NSString *version,
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
                                  WaveUpdateCallback callback) {
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

void wave_check_for_updates(const char *current_version, int manual,
                            WaveUpdateCallback callback) {
    NSString *current = current_bundle_version(current_version);
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
        updater_emit(callback, UPDATE_STATE_AVAILABLE, tag, current, 0.0);
        start_update_download(asset_url, tag, callback);
    }];
    [task resume];
}
