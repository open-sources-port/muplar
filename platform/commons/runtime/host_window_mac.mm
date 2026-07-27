#include "host_window.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cstring>
#include <vector>

@interface MuplarWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) BOOL closed;
@end

@implementation MuplarWindowDelegate
- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    self.closed = YES;
}
@end

namespace muplar::runtime
{

struct HostWindow::Impl {
    NSWindow *window = nil;
    NSImageView *image_view = nil;
    MuplarWindowDelegate *delegate = nil;
    std::vector<HostInputEvent> pending_input;
};

static void enqueue_host_input(HostWindow::Impl *impl, NSEvent *event)
{
    if (!impl || !impl->window || !event || [event window] != impl->window)
        return;

    HostInputEvent out;
    out.device_id = 1;

    switch ([event type]) {
        case NSEventTypeLeftMouseDown:
            out.type = 2;         // AINPUT_EVENT_TYPE_MOTION
            out.action = 0;       // AMOTION_EVENT_ACTION_DOWN
            out.source = 0x1002;  // AINPUT_SOURCE_TOUCHSCREEN
            break;
        case NSEventTypeLeftMouseDragged:
            out.type = 2;
            out.action = 2;  // AMOTION_EVENT_ACTION_MOVE
            out.source = 0x1002;
            break;
        case NSEventTypeLeftMouseUp:
            out.type = 2;
            out.action = 1;  // AMOTION_EVENT_ACTION_UP
            out.source = 0x1002;
            break;
        case NSEventTypeKeyDown:
            out.type = 1;        // AINPUT_EVENT_TYPE_KEY
            out.action = 0;      // AKEY_EVENT_ACTION_DOWN
            out.source = 0x101;  // AINPUT_SOURCE_KEYBOARD
            out.key_code = static_cast<int32_t>([event keyCode]);
            break;
        case NSEventTypeKeyUp:
            out.type = 1;
            out.action = 1;  // AKEY_EVENT_ACTION_UP
            out.source = 0x101;
            out.key_code = static_cast<int32_t>([event keyCode]);
            break;
        default:
            return;
    }

    if (out.type == 2) {
        NSView *content = [impl->window contentView];
        if (!content)
            return;
        NSRect bounds = [content bounds];
        if (bounds.size.width <= 0 || bounds.size.height <= 0)
            return;
        NSPoint point = [content convertPoint:[event locationInWindow]
                                     fromView:nil];
        float x = static_cast<float>(point.x);
        float y = static_cast<float>(bounds.size.height - point.y);
        if (x < 0.0f || y < 0.0f || x > static_cast<float>(bounds.size.width) ||
            y > static_cast<float>(bounds.size.height)) {
            return;
        }
        out.x = x;
        out.y = y;
    }

    impl->pending_input.push_back(out);
}

static void pump_appkit_once(HostWindow::Impl *impl)
{
    @autoreleasepool {
        for (;;) {
            NSEvent *event = [NSApp
                nextEventMatchingMask:NSEventMaskAny
                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0]
                               inMode:NSDefaultRunLoopMode
                              dequeue:YES];
            if (!event)
                break;
            enqueue_host_input(impl, event);
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
    }
}

HostWindow::HostWindow(int width, int height)
{
    @autoreleasepool {
        if (![NSThread isMainThread])
            return;

        impl_ = new Impl();
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        NSRect frame =
            NSMakeRect(120, 120, std::max(width, 64), std::max(height, 64));
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskResizable |
                           NSWindowStyleMaskMiniaturizable;
        impl_->window =
            [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:style
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        impl_->delegate = [[MuplarWindowDelegate alloc] init];
        impl_->image_view = [[NSImageView alloc]
            initWithFrame:[[impl_->window contentView] bounds]];
        [impl_->image_view
            setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [impl_->image_view setImageScaling:NSImageScaleAxesIndependently];
        [impl_->window setTitle:@"Muplar Native Window"];
        [impl_->window setContentView:impl_->image_view];
        [impl_->window setDelegate:impl_->delegate];
        [impl_->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        pump_appkit_once(impl_);
    }
}

HostWindow::~HostWindow()
{
    @autoreleasepool {
        if (impl_) {
            if (impl_->window && !impl_->delegate.closed)
                [impl_->window close];
            delete impl_;
            impl_ = nullptr;
        }
    }
}

bool HostWindow::valid() const
{
    return impl_ && impl_->window && impl_->image_view;
}

bool HostWindow::closed() const
{
    return !valid() || impl_->delegate.closed;
}

void HostWindow::present_rgba(const uint8_t *pixels,
                              int width,
                              int height,
                              int stride_pixels)
{
    if (!valid() || closed() || !pixels || width <= 0 || height <= 0 ||
        stride_pixels < width) {
        return;
    }

    @autoreleasepool {
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:width * 4
                        bitsPerPixel:32];
        uint8_t *dst = [rep bitmapData];
        const int src_stride = stride_pixels * 4;
        for (int y = 0; y < height; ++y) {
            const uint8_t *src_row = pixels + (height - 1 - y) * src_stride;
            std::memcpy(dst + y * width * 4, src_row,
                        static_cast<size_t>(width) * 4);
        }

        NSImage *image =
            [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
        [image addRepresentation:rep];
        [impl_->image_view setImage:image];
        [impl_->window displayIfNeeded];
        pump_appkit_once(impl_);
    }
}

void HostWindow::pump_events()
{
    if (!valid())
        return;
    pump_appkit_once(impl_);
}

std::vector<HostInputEvent> HostWindow::take_input_events()
{
    std::vector<HostInputEvent> out;
    if (!valid())
        return out;
    out.swap(impl_->pending_input);
    return out;
}

void HostWindow::run_for_ms(int milliseconds)
{
    if (!valid() || milliseconds <= 0)
        return;
    NSDate *deadline =
        [NSDate dateWithTimeIntervalSinceNow:milliseconds / 1000.0];
    while (!closed() && [deadline timeIntervalSinceNow] > 0) {
        @autoreleasepool {
            NSEvent *event = [NSApp
                nextEventMatchingMask:NSEventMaskAny
                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                               inMode:NSDefaultRunLoopMode
                              dequeue:YES];
            if (event) {
                enqueue_host_input(impl_, event);
                [NSApp sendEvent:event];
            }
            [NSApp updateWindows];
        }
    }
}

void HostWindow::run_until_closed()
{
    if (!valid())
        return;
    while (!closed()) {
        @autoreleasepool {
            NSEvent *event = [NSApp
                nextEventMatchingMask:NSEventMaskAny
                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                               inMode:NSDefaultRunLoopMode
                              dequeue:YES];
            if (event) {
                enqueue_host_input(impl_, event);
                [NSApp sendEvent:event];
            }
            [NSApp updateWindows];
        }
    }
}

}  // namespace muplar::runtime
