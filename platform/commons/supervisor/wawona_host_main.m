#include <signal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#import <AppKit/AppKit.h>

#import "WWNCompositorBridge.h"
#import "WWNSettings.h"

@interface MuplarWawonaAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation MuplarWawonaAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;

  NSScreen *screen = [NSScreen mainScreen];
  CGFloat scale = screen ? screen.backingScaleFactor : 1.0;
  CGFloat screenW = screen ? screen.frame.size.width : 1024.0;
  CGFloat screenH = screen ? screen.frame.size.height : 768.0;
  uint32_t outputW = (uint32_t)fmin(1024.0, screenW * 0.75);
  uint32_t outputH = (uint32_t)fmin(768.0, screenH * 0.75);

  WWNCompositorBridge *compositor = [WWNCompositorBridge sharedBridge];
  [compositor setOutputWidth:outputW height:outputH scale:(float)scale];
  [compositor setForceSSD:WWNSettings_GetForceServerSideDecorations()];

  if (![compositor startWithSocketName:@"wayland-0"]) {
    fprintf(stderr, "[WawonaHost] failed to start compositor\n");
    [NSApp terminate:nil];
    return;
  }

  setenv("WAYLAND_DISPLAY", [[compositor socketName] UTF8String], 1);
  fprintf(stderr,
          "[WawonaHost] compositor ready: XDG_RUNTIME_DIR=%s "
          "WAYLAND_DISPLAY=%s\n",
          getenv("XDG_RUNTIME_DIR") ?: "",
          getenv("WAYLAND_DISPLAY") ?: "");
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [[WWNCompositorBridge sharedBridge] stop];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
  (void)sender;
  return NO;
}

@end

static dispatch_source_t g_sigterm_source;
static dispatch_source_t g_sigint_source;

static void install_signal_source(int signo) {
  signal(signo, SIG_IGN);
  dispatch_source_t source =
      dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
                             (uintptr_t)signo,
                             0,
                             dispatch_get_main_queue());
  dispatch_source_set_event_handler(source, ^{
    [NSApp terminate:nil];
  });
  dispatch_resume(source);
  if (signo == SIGTERM) {
    g_sigterm_source = source;
  } else if (signo == SIGINT) {
    g_sigint_source = source;
  }
}

int main(int argc, char *argv[]) {
  @autoreleasepool {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--version") == 0 ||
          strcmp(argv[i], "-v") == 0) {
        printf("Wawona host for Muplar\n");
        return 0;
      }
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Set process name so the Dock label reads "Foot" instead of "wawona"
    [[NSProcessInfo processInfo] setProcessName:@"Foot"];

    // Programmatically set Dock icon to the macOS Terminal app icon
    @autoreleasepool {
        NSImage *icon = nil;
        NSWorkspace *ws = [NSWorkspace sharedWorkspace];
        NSURL *url = [ws URLForApplicationWithBundleIdentifier:@"com.apple.Terminal"];
        if (url) {
            icon = [ws iconForFile:[url path]];
        }
        if (!icon) {
            NSString *path = [ws absolutePathForAppBundleWithIdentifier:@"com.apple.Terminal"];
            if (path) {
                icon = [ws iconForFile:path];
            }
        }
        if (icon) {
            [NSApp setApplicationIconImage:icon];
        }
    }

    MuplarWawonaAppDelegate *delegate =
        [[MuplarWawonaAppDelegate alloc] init];
    [NSApp setDelegate:delegate];

    install_signal_source(SIGTERM);
    install_signal_source(SIGINT);

    [NSApp run];
    return 0;
  }
}
