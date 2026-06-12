#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#import <AppKit/AppKit.h>
#import "WWNCompositorBridge.h"

static WWNCompositorBridge *g_bridge = nil;

@interface MuplarWawonaHostDelegate : NSObject <NSApplicationDelegate>
- (void)setupMenu;
@end

@implementation MuplarWawonaHostDelegate

- (void)setupMenu
{
    NSMenu *menubar = [[NSMenu alloc] init];
    NSString *appName = @"Wawona";

    // -- App Menu --
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    NSMenu *appMenu = [[NSMenu alloc] init];

    [appMenu addItem:[[NSMenuItem alloc]
                         initWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
                                action:@selector(hide:)
                         keyEquivalent:@"h"]];

    NSMenuItem *hideOthers =
        [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                   action:@selector(hideOtherApplications:)
                            keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                             NSEventModifierFlagOption];
    [appMenu addItem:hideOthers];

    [appMenu addItem:[[NSMenuItem alloc]
                         initWithTitle:@"Show All"
                                action:@selector(unhideAllApplications:)
                         keyEquivalent:@""]];
    [appMenu addItem:[NSMenuItem separatorItem]];

    [appMenu addItem:[[NSMenuItem alloc]
                         initWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
                                action:@selector(terminate:)
                         keyEquivalent:@"q"]];
    [appMenuItem setSubmenu:appMenu];
    [menubar addItem:appMenuItem];

    // -- Window Menu --
    NSMenuItem *windowMenuItem = [[NSMenuItem alloc] init];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu
        addItem:[[NSMenuItem alloc] initWithTitle:@"Minimize"
                                           action:@selector(performMiniaturize:)
                                    keyEquivalent:@"m"]];
    [windowMenu
        addItem:[[NSMenuItem alloc] initWithTitle:@"Zoom"
                                           action:@selector(performZoom:)
                                    keyEquivalent:@""]];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu
        addItem:[[NSMenuItem alloc] initWithTitle:@"Bring All to Front"
                                           action:@selector(arrangeInFront:)
                                    keyEquivalent:@""]];
    [windowMenuItem setSubmenu:windowMenu];
    [menubar addItem:windowMenuItem];
    [NSApp setWindowsMenu:windowMenu];

    [NSApp setMainMenu:menubar];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [self setupMenu];

    NSScreen *screen = [NSScreen mainScreen];
    CGFloat scale = screen ? screen.backingScaleFactor : 1.0;

    g_bridge = [WWNCompositorBridge sharedBridge];
    [g_bridge setOutputWidth:1024 height:768 scale:(float)scale];
    [g_bridge setForceSSD:NO];

    if (![g_bridge startWithSocketName:@"wayland-0"]) {
        fprintf(stderr, "[wawona-host] failed to start Wayland compositor\n");
        [NSApp terminate:nil];
        return;
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *display = getenv("WAYLAND_DISPLAY");
    fprintf(stderr, "[wawona-host] running XDG_RUNTIME_DIR=%s WAYLAND_DISPLAY=%s\n",
            runtime_dir ? runtime_dir : "(unset)",
            display ? display : "(unset)");
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
    (void)notification;
    if (g_bridge) {
        [g_bridge stop];
        g_bridge = nil;
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return NO;
}

@end

static void terminate_from_signal(int sig)
{
    _exit(128 + sig);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    @autoreleasepool {
        [[NSProcessInfo processInfo] setProcessName:@"Wawona"];
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);

        signal(SIGTERM, terminate_from_signal);
        signal(SIGINT, terminate_from_signal);
        signal(SIGPIPE, SIG_IGN);

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // Programmatically set Dock icon to the macOS Terminal app icon
        @autoreleasepool {
            NSImage *icon = nil;
            NSWorkspace *ws = [NSWorkspace sharedWorkspace];
            
            // 1. Try modern macOS Terminal app path
            NSString *terminalPath = @"/System/Applications/Utilities/Terminal.app";
            if ([[NSFileManager defaultManager] fileExistsAtPath:terminalPath]) {
                icon = [ws iconForFile:terminalPath];
            }
            
            // 2. Try legacy macOS Terminal app path
            if (!icon) {
                terminalPath = @"/Applications/Utilities/Terminal.app";
                if ([[NSFileManager defaultManager] fileExistsAtPath:terminalPath]) {
                    icon = [ws iconForFile:terminalPath];
                }
            }
            
            // 3. Try bundle identifier lookup as fallback
            if (!icon) {
                NSURL *url = [ws URLForApplicationWithBundleIdentifier:@"com.apple.Terminal"];
                if (url) {
                    icon = [ws iconForFile:[url path]];
                }
            }
            
            // 4. Try standard application icon
            if (!icon) {
                icon = [NSImage imageNamed:@"NSApplicationIcon"];
            }
            
            // 5. Apply the icon
            if (icon) {
                [NSApp setApplicationIconImage:icon];
            }
        }

        MuplarWawonaHostDelegate *delegate = [[MuplarWawonaHostDelegate alloc] init];
        [NSApp setDelegate:delegate];
        [NSApp run];
    }

    return 0;
}
