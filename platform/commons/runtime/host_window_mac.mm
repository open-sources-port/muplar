#include "host_window.h"

#include "android_keycodes.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

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
    int frame_fd = -1;
    bool embedded = false;
    std::vector<uint8_t> input_buffer;
    std::vector<HostInputEvent> pending_input;
    std::string software_frame_path;
    time_t software_frame_mtime_sec = 0;
    long software_frame_mtime_nsec = 0;
    off_t software_frame_size = 0;
};

struct HostFrameHeader {
    uint32_t magic = 0x4d485231;  // MHR1
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_pixels = 0;
    uint64_t bytes = 0;
};

struct HostInputPacket {
    uint32_t magic = 0;
    int32_t type = 0;
    int32_t action = 0;
    int32_t source = 0;
    int32_t device_id = 0;
    int32_t key_code = 0;
    float x = 0.0f;
    float y = 0.0f;
};

static constexpr uint32_t kHostInputMagic = 0x4d484931;  // MHI1

static bool write_all(int fd, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    size_t offset = 0;
    while (offset < size) {
        ssize_t written = write(fd, bytes + offset, size - offset);
        if (written <= 0)
            return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

static int connect_frame_socket(const char *path)
{
    if (!path || !*path)
        return -1;
    if (std::strlen(path) >= sizeof(sockaddr_un::sun_path))
        return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
        0) {
        close(fd);
        return -1;
    }
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
    return fd;
}

static void drain_embedded_input(HostWindow::Impl *impl)
{
    if (!impl || impl->frame_fd < 0)
        return;

    for (;;) {
        uint8_t buffer[256];
        ssize_t count =
            recv(impl->frame_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (count == 0) {
            close(impl->frame_fd);
            impl->frame_fd = -1;
            return;
        }
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close(impl->frame_fd);
            impl->frame_fd = -1;
            return;
        }
        impl->input_buffer.insert(impl->input_buffer.end(), buffer,
                                  buffer + count);
    }

    while (impl->input_buffer.size() >= sizeof(HostInputPacket)) {
        HostInputPacket packet{};
        std::memcpy(&packet, impl->input_buffer.data(), sizeof(packet));
        impl->input_buffer.erase(
            impl->input_buffer.begin(),
            impl->input_buffer.begin() + sizeof(HostInputPacket));
        if (packet.magic != kHostInputMagic)
            continue;
        HostInputEvent event;
        event.type = packet.type;
        event.action = packet.action;
        event.source = packet.source;
        event.device_id = packet.device_id;
        event.key_code = packet.key_code;
        event.x = packet.x;
        event.y = packet.y;
        impl->pending_input.push_back(event);
    }
}

static bool read_all_fd(int fd, void *data, size_t size)
{
    auto *bytes = static_cast<uint8_t *>(data);
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(fd, bytes + offset, size - offset);
        if (count <= 0)
            return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}

static void poll_software_frame(HostWindow::Impl *impl)
{
    if (!impl || impl->frame_fd < 0 || impl->software_frame_path.empty())
        return;
    struct stat st{};
    if (stat(impl->software_frame_path.c_str(), &st) != 0)
        return;
#ifdef __APPLE__
    long nsec = st.st_mtimespec.tv_nsec;
#else
    long nsec = st.st_mtim.tv_nsec;
#endif
    if (st.st_size == impl->software_frame_size &&
        st.st_mtime == impl->software_frame_mtime_sec &&
        nsec == impl->software_frame_mtime_nsec) {
        return;
    }
    int fd = open(impl->software_frame_path.c_str(), O_RDONLY);
    if (fd < 0)
        return;
    HostFrameHeader header;
    bool ok = read_all_fd(fd, &header, sizeof(header));
    if (ok) {
        ok = header.magic == 0x4d485231 && header.width > 0 &&
             header.height > 0 && header.stride_pixels >= header.width &&
             header.bytes == static_cast<uint64_t>(header.width) *
                                 static_cast<uint64_t>(header.height) * 4 &&
             header.bytes <= 64ULL * 1024ULL * 1024ULL &&
             st.st_size >= static_cast<off_t>(sizeof(header) + header.bytes);
    }
    std::vector<uint8_t> pixels(ok ? static_cast<size_t>(header.bytes) : 0);
    if (ok && !pixels.empty())
        ok = read_all_fd(fd, pixels.data(), pixels.size());
    close(fd);
    if (!ok)
        return;
    if (!write_all(impl->frame_fd, &header, sizeof(header)) ||
        !write_all(impl->frame_fd, pixels.data(), pixels.size())) {
        close(impl->frame_fd);
        impl->frame_fd = -1;
        return;
    }
    impl->software_frame_size = st.st_size;
    impl->software_frame_mtime_sec = st.st_mtime;
    impl->software_frame_mtime_nsec = nsec;
}

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
            out.key_code = android_key_code_from_mac_key([event keyCode]);
            if (out.key_code == 0)
                return;
            break;
        case NSEventTypeKeyUp:
            out.type = 1;
            out.action = 1;  // AKEY_EVENT_ACTION_UP
            out.source = 0x101;
            out.key_code = android_key_code_from_mac_key([event keyCode]);
            if (out.key_code == 0)
                return;
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
        impl_->frame_fd = connect_frame_socket(
            std::getenv("MUPLAR_HOST_WINDOW_FRAME_SOCKET"));
        if (impl_->frame_fd >= 0) {
            impl_->embedded = true;
            const char *software_frame_path =
                std::getenv("MUPLAR_HOST_WINDOW_SOFTWARE_FRAME_PATH");
            if (software_frame_path && *software_frame_path)
                impl_->software_frame_path = software_frame_path;
            return;
        }

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
            if (impl_->frame_fd >= 0)
                close(impl_->frame_fd);
            if (!impl_->embedded && impl_->window && !impl_->delegate.closed)
                [impl_->window close];
            delete impl_;
            impl_ = nullptr;
        }
    }
}

bool HostWindow::valid() const
{
    return impl_ && ((impl_->embedded && impl_->frame_fd >= 0) ||
                     (impl_->window && impl_->image_view));
}

bool HostWindow::closed() const
{
    if (!valid())
        return true;
    if (impl_->embedded)
        return impl_->frame_fd < 0;
    return impl_->delegate.closed;
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

    if (impl_->embedded) {
        std::vector<uint8_t> packed(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) * 4);
        const int src_stride = stride_pixels * 4;
        for (int y = 0; y < height; ++y) {
            const uint8_t *src_row = pixels + (height - 1 - y) * src_stride;
            std::memcpy(packed.data() + static_cast<size_t>(y) * width * 4,
                        src_row, static_cast<size_t>(width) * 4);
        }
        HostFrameHeader header;
        header.width = static_cast<uint32_t>(width);
        header.height = static_cast<uint32_t>(height);
        header.stride_pixels = static_cast<uint32_t>(width);
        header.bytes = packed.size();
        if (!write_all(impl_->frame_fd, &header, sizeof(header)) ||
            !write_all(impl_->frame_fd, packed.data(), packed.size())) {
            close(impl_->frame_fd);
            impl_->frame_fd = -1;
        }
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
    if (impl_->embedded) {
        drain_embedded_input(impl_);
        poll_software_frame(impl_);
        return;
    }
    pump_appkit_once(impl_);
}

std::vector<HostInputEvent> HostWindow::take_input_events()
{
    std::vector<HostInputEvent> out;
    if (!valid())
        return out;
    if (impl_->embedded)
        drain_embedded_input(impl_);
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
