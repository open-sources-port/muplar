#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#import <AppKit/AppKit.h>
#import "WWNCompositorBridge.h"

static WWNCompositorBridge *g_bridge = nil;

@interface MuplarWawonaHostDelegate : NSObject <NSApplicationDelegate>
@end

@implementation MuplarWawonaHostDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;

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
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);

        signal(SIGTERM, terminate_from_signal);
        signal(SIGINT, terminate_from_signal);
        signal(SIGPIPE, SIG_IGN);

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        MuplarWawonaHostDelegate *delegate = [[MuplarWawonaHostDelegate alloc] init];
        [NSApp setDelegate:delegate];
        [NSApp run];
    }

    return 0;
}
