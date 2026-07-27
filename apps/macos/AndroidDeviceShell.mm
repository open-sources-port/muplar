#import "AndroidDeviceShell.h"

#include "android_keycodes.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

@class AndroidDeviceShell;

@interface AndroidDeviceFrameView : NSImageView
@property(nonatomic, weak) AndroidDeviceShell* deviceShell;
@end

@interface AndroidDeviceShell ()
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) AndroidDeviceFrameView* frameView;
@property(nonatomic, strong) NSTextField* titleLabel;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic, strong) NSProgressIndicator* spinner;
@property(nonatomic, strong) NSStackView* tabStripStack;
@property(nonatomic, strong) NSLayoutConstraint* tabStripHeightConstraint;
@property(nonatomic, strong) NSMutableArray<NSDictionary<NSString*, NSString*>*>* tabs;
@property(nonatomic, copy) NSString* activeTabIdentifier;
@property(nonatomic, copy) NSString* frameSocketPath;
@property(nonatomic, assign) int frameServerFd;
@property(nonatomic, assign) int frameClientFd;
@property(nonatomic, assign) NSUInteger framePixelWidth;
@property(nonatomic, assign) NSUInteger framePixelHeight;
- (void)sendPointerEvent:(NSEvent*)event action:(int32_t)action;
- (void)sendKeyEvent:(NSEvent*)event action:(int32_t)action;
@end

static NSString* const AndroidDeviceLauncherTabIdentifier = @"launcher";
static const uint32_t AndroidDeviceFrameMagic = 0x4d485231;  // MHR1
static const uint32_t AndroidDeviceInputMagic = 0x4d484931;  // MHI1

struct AndroidDeviceFrameHeader {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stridePixels;
    uint64_t bytes;
};

struct AndroidDeviceInputPacket {
    uint32_t magic;
    int32_t type;
    int32_t action;
    int32_t source;
    int32_t deviceId;
    int32_t keyCode;
    float x;
    float y;
};

static BOOL AndroidDeviceReadAll(int fd, void* data, size_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(data);
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(fd, bytes + offset, size - offset);
        if (count <= 0)
            return NO;
        offset += static_cast<size_t>(count);
    }
    return YES;
}

static BOOL AndroidDeviceWriteAll(int fd, const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = write(fd, bytes + offset, size - offset);
        if (count <= 0)
            return NO;
        offset += static_cast<size_t>(count);
    }
    return YES;
}

static NSImage* AndroidDeviceSymbolImage(NSString* name)
{
    if (@available(macOS 11.0, *)) {
        return [NSImage imageWithSystemSymbolName:name
                         accessibilityDescription:nil];
    }
    return nil;
}

static NSButton* AndroidDeviceIconButton(NSString* symbol,
                                         NSString* tooltip,
                                         id target,
                                         SEL action)
{
    NSButton* button = [NSButton buttonWithImage:AndroidDeviceSymbolImage(symbol)
                                         target:target
                                         action:action];
    button.bezelStyle = NSBezelStyleTexturedRounded;
    button.toolTip = tooltip;
    button.translatesAutoresizingMaskIntoConstraints = NO;
    [button.widthAnchor constraintEqualToConstant:34.0].active = YES;
    [button.heightAnchor constraintEqualToConstant:30.0].active = YES;
    return button;
}

static NSString* AndroidDeviceTabTitle(NSDictionary<NSString*, NSString*>* tab)
{
    NSString* title = tab[@"title"];
    return title.length > 0 ? title : @"App";
}

static NSView* AndroidDeviceTabChipView(NSString* title,
                                        NSString* identifier,
                                        BOOL selected,
                                        BOOL closable,
                                        NSInteger index,
                                        id target,
                                        SEL selectAction,
                                        SEL closeAction)
{
    NSView* chip = [[NSView alloc] init];
    chip.identifier = identifier;
    chip.wantsLayer = YES;
    chip.layer.cornerRadius = 6.0;
    chip.layer.backgroundColor = selected
        ? [NSColor controlAccentColor].CGColor
        : [NSColor colorWithCalibratedWhite:1.0 alpha:0.07].CGColor;
    chip.translatesAutoresizingMaskIntoConstraints = NO;
    [chip.heightAnchor constraintEqualToConstant:26.0].active = YES;
    [chip.widthAnchor constraintGreaterThanOrEqualToConstant:64.0].active = YES;
    [chip setContentHuggingPriority:NSLayoutPriorityDefaultHigh
                      forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSButton* titleButton = [NSButton buttonWithTitle:title
                                                target:target
                                                action:selectAction];
    titleButton.tag = index;
    titleButton.bordered = NO;
    titleButton.font = [NSFont systemFontOfSize:12.0];
    titleButton.lineBreakMode = NSLineBreakByTruncatingTail;
    titleButton.translatesAutoresizingMaskIntoConstraints = NO;
    [titleButton.widthAnchor constraintLessThanOrEqualToConstant:110.0].active = YES;

    NSStackView* inner = [[NSStackView alloc] init];
    inner.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    inner.alignment = NSLayoutAttributeCenterY;
    inner.spacing = 2.0;
    inner.edgeInsets = NSEdgeInsetsMake(4, 10, 4, closable ? 4 : 10);
    inner.translatesAutoresizingMaskIntoConstraints = NO;
    [inner addArrangedSubview:titleButton];

    if (closable) {
        NSButton* closeButton =
            [NSButton buttonWithImage:AndroidDeviceSymbolImage(@"xmark.circle.fill")
                                target:target
                                action:closeAction];
        closeButton.tag = index;
        closeButton.bordered = NO;
        closeButton.toolTip = @"Close Tab";
        closeButton.translatesAutoresizingMaskIntoConstraints = NO;
        [closeButton.widthAnchor constraintEqualToConstant:16.0].active = YES;
        [closeButton.heightAnchor constraintEqualToConstant:16.0].active = YES;
        [inner addArrangedSubview:closeButton];
    }

    [chip addSubview:inner];
    [NSLayoutConstraint activateConstraints:@[
        [inner.leadingAnchor constraintEqualToAnchor:chip.leadingAnchor],
        [inner.trailingAnchor constraintEqualToAnchor:chip.trailingAnchor],
        [inner.topAnchor constraintEqualToAnchor:chip.topAnchor],
        [inner.bottomAnchor constraintEqualToAnchor:chip.bottomAnchor],
    ]];
    return chip;
}

@implementation AndroidDeviceFrameView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)becomeFirstResponder
{
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeFirstResponder:self];
    [self.deviceShell sendPointerEvent:event action:0];
}

- (void)mouseDragged:(NSEvent*)event
{
    [self.deviceShell sendPointerEvent:event action:2];
}

- (void)mouseUp:(NSEvent*)event
{
    [self.deviceShell sendPointerEvent:event action:1];
}

- (void)keyDown:(NSEvent*)event
{
    [self.deviceShell sendKeyEvent:event action:0];
}

- (void)keyUp:(NSEvent*)event
{
    [self.deviceShell sendKeyEvent:event action:1];
}

@end

@implementation AndroidDeviceShell

- (instancetype)initWithPrefixName:(NSString*)prefixName
{
    self = [super init];
    if (!self)
        return nil;

    _tabs = [NSMutableArray array];
    _activeTabIdentifier = AndroidDeviceLauncherTabIdentifier;
    _frameServerFd = -1;
    _frameClientFd = -1;

    CGFloat devicePixelWidth = 1440.0;
    CGFloat devicePixelHeight = 3200.0;
    (void) devicePixelWidth;
    (void) devicePixelHeight;
    CGFloat screenWidth = 460.0;
    CGFloat screenHeight = 800.0;
    CGFloat toolbarHeight = 56.0;
    CGFloat tabStripHeight = 42.0;
    CGFloat outerPadding = 18.0;
    NSRect frame =
        NSMakeRect(0, 0, screenWidth + outerPadding * 2,
                   screenHeight + toolbarHeight + tabStripHeight +
                       outerPadding * 2);

    _window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                       defer:NO];
    _window.title = @"Android Device";
    _window.releasedWhenClosed = NO;
    _window.delegate = self;
    _window.minSize = frame.size;
    _window.maxSize = frame.size;
    _window.appearance =
        [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

    NSView* content = _window.contentView;
    content.wantsLayer = YES;
    content.layer.backgroundColor =
        [NSColor colorWithCalibratedWhite:0.04 alpha:1.0].CGColor;

    NSStackView* root = [[NSStackView alloc] init];
    root.orientation = NSUserInterfaceLayoutOrientationVertical;
    root.spacing = 12.0;
    root.edgeInsets = NSEdgeInsetsMake(14, 14, 14, 14);
    root.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:root];

    [NSLayoutConstraint activateConstraints:@[
        [root.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [root.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [root.topAnchor constraintEqualToAnchor:content.topAnchor],
        [root.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
    ]];

    NSStackView* toolbar = [[NSStackView alloc] init];
    toolbar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    toolbar.alignment = NSLayoutAttributeCenterY;
    toolbar.spacing = 8.0;
    toolbar.translatesAutoresizingMaskIntoConstraints = NO;

    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"chevron.left", @"Back", self,
                                    @selector(deviceBack:))];
    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"circle", @"Home", self,
                                    @selector(deviceHome:))];
    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"rectangle.stack", @"Recents", self,
                                    @selector(deviceRecents:))];

    NSView* spacer = [[NSView alloc] init];
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationHorizontal];
    [toolbar addArrangedSubview:spacer];

    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"square.and.arrow.down", @"Install APK",
                                    self, @selector(deviceInstall:))];
    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"gearshape", @"Settings", self,
                                    @selector(deviceSettings:))];
    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"power", @"Turn Off", self,
                                    @selector(deviceClose:))];
    [root addArrangedSubview:toolbar];

    _tabStripStack = [[NSStackView alloc] init];
    _tabStripStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    _tabStripStack.alignment = NSLayoutAttributeCenterY;
    _tabStripStack.spacing = 6.0;
    _tabStripStack.translatesAutoresizingMaskIntoConstraints = NO;
    [root addArrangedSubview:_tabStripStack];
    [_tabStripStack.widthAnchor constraintEqualToConstant:460.0].active = YES;
    _tabStripHeightConstraint =
        [_tabStripStack.heightAnchor constraintEqualToConstant:0.0];
    _tabStripHeightConstraint.active = YES;

    NSView* deviceFrame = [[NSView alloc] init];
    deviceFrame.wantsLayer = YES;
    deviceFrame.layer.cornerRadius = 28.0;
    deviceFrame.layer.borderWidth = 1.0;
    deviceFrame.layer.borderColor =
        [NSColor colorWithCalibratedWhite:0.22 alpha:1.0].CGColor;
    deviceFrame.layer.backgroundColor = NSColor.blackColor.CGColor;
    deviceFrame.translatesAutoresizingMaskIntoConstraints = NO;
    [root addArrangedSubview:deviceFrame];
    [deviceFrame.widthAnchor constraintEqualToConstant:screenWidth].active = YES;
    [deviceFrame.heightAnchor constraintEqualToConstant:screenHeight].active = YES;

    NSView* screen = [[NSView alloc] init];
    screen.wantsLayer = YES;
    screen.layer.cornerRadius = 20.0;
    screen.layer.backgroundColor =
        [NSColor colorWithCalibratedWhite:0.10 alpha:1.0].CGColor;
    screen.translatesAutoresizingMaskIntoConstraints = NO;
    [deviceFrame addSubview:screen];

    _frameView = [[AndroidDeviceFrameView alloc] init];
    _frameView.deviceShell = self;
    _frameView.imageScaling = NSImageScaleAxesIndependently;
    _frameView.translatesAutoresizingMaskIntoConstraints = NO;
    [screen addSubview:_frameView];

    _titleLabel = [NSTextField labelWithString:prefixName ?: @"Android"];
    _titleLabel.font = [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];
    _titleLabel.textColor = NSColor.whiteColor;
    _titleLabel.alignment = NSTextAlignmentCenter;
    _titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [screen addSubview:_titleLabel];

    _statusLabel = [NSTextField wrappingLabelWithString:@"Ready"];
    _statusLabel.font = [NSFont systemFontOfSize:13 weight:NSFontWeightRegular];
    _statusLabel.textColor = NSColor.secondaryLabelColor;
    _statusLabel.alignment = NSTextAlignmentCenter;
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [screen addSubview:_statusLabel];

    _spinner = [[NSProgressIndicator alloc] init];
    _spinner.style = NSProgressIndicatorStyleSpinning;
    _spinner.controlSize = NSControlSizeRegular;
    _spinner.displayedWhenStopped = NO;
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;
    [screen addSubview:_spinner];

    [NSLayoutConstraint activateConstraints:@[
        [screen.leadingAnchor constraintEqualToAnchor:deviceFrame.leadingAnchor constant:10.0],
        [screen.trailingAnchor constraintEqualToAnchor:deviceFrame.trailingAnchor constant:-10.0],
        [screen.topAnchor constraintEqualToAnchor:deviceFrame.topAnchor constant:10.0],
        [screen.bottomAnchor constraintEqualToAnchor:deviceFrame.bottomAnchor constant:-10.0],

        [_frameView.leadingAnchor constraintEqualToAnchor:screen.leadingAnchor],
        [_frameView.trailingAnchor constraintEqualToAnchor:screen.trailingAnchor],
        [_frameView.topAnchor constraintEqualToAnchor:screen.topAnchor],
        [_frameView.bottomAnchor constraintEqualToAnchor:screen.bottomAnchor],

        [_titleLabel.leadingAnchor constraintEqualToAnchor:screen.leadingAnchor constant:24.0],
        [_titleLabel.trailingAnchor constraintEqualToAnchor:screen.trailingAnchor constant:-24.0],
        [_titleLabel.centerYAnchor constraintEqualToAnchor:screen.centerYAnchor constant:-32.0],

        [_statusLabel.leadingAnchor constraintEqualToAnchor:screen.leadingAnchor constant:32.0],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:screen.trailingAnchor constant:-32.0],
        [_statusLabel.topAnchor constraintEqualToAnchor:_titleLabel.bottomAnchor constant:10.0],

        [_spinner.centerXAnchor constraintEqualToAnchor:screen.centerXAnchor],
        [_spinner.topAnchor constraintEqualToAnchor:_statusLabel.bottomAnchor constant:18.0],
    ]];

    [_window center];
    [self focusLauncherTab];
    return self;
}

- (void)dealloc
{
    if (_frameClientFd >= 0)
        close(_frameClientFd);
    if (_frameServerFd >= 0)
        close(_frameServerFd);
    if (_frameSocketPath.length > 0)
        unlink(_frameSocketPath.fileSystemRepresentation);
}

- (NSInteger)indexOfTabWithIdentifier:(NSString*)identifier
{
    if (identifier.length == 0)
        return NSNotFound;
    for (NSUInteger i = 0; i < self.tabs.count; ++i) {
        if ([self.tabs[i][@"identifier"] isEqualToString:identifier])
            return (NSInteger)i;
    }
    return NSNotFound;
}

- (void)updateTabControl
{
    BOOL showTabs = self.tabs.count > 1;
    self.tabStripStack.hidden = !showTabs;
    self.tabStripHeightConstraint.constant = showTabs ? 30.0 : 0.0;

    for (NSView* chip in self.tabStripStack.arrangedSubviews.copy) {
        [self.tabStripStack removeArrangedSubview:chip];
        [chip removeFromSuperview];
    }
    for (NSUInteger i = 0; i < self.tabs.count; ++i) {
        NSDictionary<NSString*, NSString*>* tab = self.tabs[i];
        NSString* identifier = tab[@"identifier"];
        BOOL selected = [identifier isEqualToString:self.activeTabIdentifier];
        BOOL closable =
            ![identifier isEqualToString:AndroidDeviceLauncherTabIdentifier];
        NSView* chip = AndroidDeviceTabChipView(
            AndroidDeviceTabTitle(tab), identifier, selected, closable,
            (NSInteger)i, self, @selector(tabChipTapped:),
            @selector(tabChipCloseTapped:));
        [self.tabStripStack addArrangedSubview:chip];
    }
    NSView* trailingSpacer = [[NSView alloc] init];
    [trailingSpacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
                                forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.tabStripStack addArrangedSubview:trailingSpacer];
}

- (void)updateTabSelectionHighlight
{
    for (NSView* chip in self.tabStripStack.arrangedSubviews) {
        BOOL selected = [chip.identifier isEqualToString:self.activeTabIdentifier];
        chip.layer.backgroundColor = selected
            ? [NSColor controlAccentColor].CGColor
            : [NSColor colorWithCalibratedWhite:1.0 alpha:0.07].CGColor;
    }
}

- (void)tabChipTapped:(NSButton*)sender
{
    NSInteger index = sender.tag;
    if (index < 0 || (NSUInteger)index >= self.tabs.count)
        return;
    self.activeTabIdentifier = self.tabs[(NSUInteger)index][@"identifier"];
    [self updateTabSelectionHighlight];
    [self updateActiveTabLabels];
    if (self.tabFocusHandler)
        self.tabFocusHandler(self.activeTabIdentifier);
}

- (void)tabChipCloseTapped:(NSButton*)sender
{
    NSInteger index = sender.tag;
    if (index < 0 || (NSUInteger)index >= self.tabs.count)
        return;
    [self closeTabWithIdentifier:self.tabs[(NSUInteger)index][@"identifier"]];
}

- (void)updateActiveTabLabels
{
    NSInteger index = [self indexOfTabWithIdentifier:self.activeTabIdentifier];
    NSString* title = index == NSNotFound
        ? @"Home"
        : AndroidDeviceTabTitle(self.tabs[(NSUInteger)index]);
    self.titleLabel.stringValue = title;
}

- (void)focusOrCreateTabWithIdentifier:(NSString*)identifier title:(NSString*)title
{
    NSString* tabIdentifier = identifier.length > 0
        ? identifier
        : AndroidDeviceLauncherTabIdentifier;
    NSString* tabTitle = title.length > 0 ? title : @"App";

    BOOL isNewTab = [self indexOfTabWithIdentifier:tabIdentifier] == NSNotFound;
    if (isNewTab) {
        [self.tabs addObject:@{
            @"identifier": tabIdentifier,
            @"title": tabTitle,
        }];
    }
    self.activeTabIdentifier = tabIdentifier;
    if (isNewTab)
        [self updateTabControl];
    else
        [self updateTabSelectionHighlight];
    [self updateActiveTabLabels];
    if (self.tabFocusHandler)
        self.tabFocusHandler(tabIdentifier);
}

- (void)focusLauncherTab
{
    [self focusOrCreateTabWithIdentifier:AndroidDeviceLauncherTabIdentifier
                                   title:@"Home"];
}

- (void)startFrameServerAtPath:(NSString*)path
{
    if (path.length == 0)
        return;
    if (self.frameServerFd >= 0 && [self.frameSocketPath isEqualToString:path])
        return;
    if (self.frameServerFd >= 0) {
        close(self.frameServerFd);
        self.frameServerFd = -1;
    }
    if (self.frameSocketPath.length > 0)
        unlink(self.frameSocketPath.fileSystemRepresentation);

    [[NSFileManager defaultManager]
        createDirectoryAtPath:[path stringByDeletingLastPathComponent]
  withIntermediateDirectories:YES
                   attributes:nil
                        error:nil];
    unlink(path.fileSystemRepresentation);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    strlcpy(address.sun_path, path.fileSystemRepresentation,
            sizeof(address.sun_path));
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(fd, 1) < 0) {
        close(fd);
        unlink(path.fileSystemRepresentation);
        return;
    }

    self.frameSocketPath = [path copy];
    self.frameServerFd = fd;
    __weak AndroidDeviceShell* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        int client = accept(fd, nullptr, nullptr);
        if (client < 0)
            return;
#ifdef SO_NOSIGPIPE
        int noSigpipe = 1;
        setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe,
                   sizeof(noSigpipe));
#endif
        dispatch_async(dispatch_get_main_queue(), ^{
            AndroidDeviceShell* strongSelf = weakSelf;
            if (!strongSelf) {
                close(client);
                return;
            }
            if (strongSelf.frameClientFd >= 0)
                close(strongSelf.frameClientFd);
            strongSelf.frameClientFd = client;
        });
        for (;;) {
            AndroidDeviceFrameHeader header{};
            if (!AndroidDeviceReadAll(client, &header, sizeof(header)))
                break;
            if (header.magic != AndroidDeviceFrameMagic || header.width == 0 ||
                header.height == 0 || header.bytes == 0 ||
                header.bytes > 64ULL * 1024ULL * 1024ULL) {
                break;
            }
            NSMutableData* frameData =
                [NSMutableData dataWithLength:(NSUInteger)header.bytes];
            if (!AndroidDeviceReadAll(client, frameData.mutableBytes,
                                      (size_t)header.bytes)) {
                break;
            }
            NSUInteger width = (NSUInteger)header.width;
            NSUInteger height = (NSUInteger)header.height;
            dispatch_async(dispatch_get_main_queue(), ^{
                AndroidDeviceShell* strongSelf = weakSelf;
                if (!strongSelf)
                    return;
                strongSelf.framePixelWidth = width;
                strongSelf.framePixelHeight = height;
                NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
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
                memcpy(rep.bitmapData, frameData.bytes, frameData.length);
                NSImage* image =
                    [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
                [image addRepresentation:rep];
                strongSelf.frameView.image = image;
                strongSelf.titleLabel.hidden = YES;
                strongSelf.statusLabel.hidden = YES;
                [strongSelf.spinner stopAnimation:nil];
            });
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            AndroidDeviceShell* strongSelf = weakSelf;
            if (strongSelf && strongSelf.frameClientFd == client)
                strongSelf.frameClientFd = -1;
        });
        close(client);
    });
}

- (void)sendPointerEvent:(NSEvent*)event action:(int32_t)action
{
    if (self.frameClientFd < 0 || !event)
        return;
    NSRect bounds = self.frameView.bounds;
    if (bounds.size.width <= 0 || bounds.size.height <= 0)
        return;
    NSPoint point = [self.frameView convertPoint:event.locationInWindow
                                       fromView:nil];
    if (point.x < 0.0 || point.y < 0.0 ||
        point.x > bounds.size.width || point.y > bounds.size.height) {
        return;
    }
    CGFloat scaleX = self.framePixelWidth > 0
        ? (CGFloat)self.framePixelWidth / bounds.size.width
        : 1.0;
    CGFloat scaleY = self.framePixelHeight > 0
        ? (CGFloat)self.framePixelHeight / bounds.size.height
        : 1.0;
    AndroidDeviceInputPacket packet{};
    packet.magic = AndroidDeviceInputMagic;
    packet.type = 2;
    packet.action = action;
    packet.source = 0x1002;
    packet.deviceId = 1;
    packet.x = (float)(point.x * scaleX);
    packet.y = (float)((bounds.size.height - point.y) * scaleY);
    if (self.inputHandler)
        self.inputHandler(self.activeTabIdentifier, packet.type, packet.action,
                          packet.source, packet.deviceId, packet.keyCode,
                          packet.x, packet.y);
    if (!AndroidDeviceWriteAll(self.frameClientFd, &packet, sizeof(packet))) {
        close(self.frameClientFd);
        self.frameClientFd = -1;
    }
}

- (void)sendKeyEvent:(NSEvent*)event action:(int32_t)action
{
    if (self.frameClientFd < 0 || !event)
        return;
    AndroidDeviceInputPacket packet{};
    packet.magic = AndroidDeviceInputMagic;
    packet.type = 1;
    packet.action = action;
    packet.source = 0x101;
    packet.deviceId = 1;
    packet.keyCode =
        muplar::runtime::android_key_code_from_mac_key(event.keyCode);
    if (packet.keyCode == 0)
        return;
    if (self.inputHandler)
        self.inputHandler(self.activeTabIdentifier, packet.type, packet.action,
                          packet.source, packet.deviceId, packet.keyCode,
                          packet.x, packet.y);
    if (!AndroidDeviceWriteAll(self.frameClientFd, &packet, sizeof(packet))) {
        close(self.frameClientFd);
        self.frameClientFd = -1;
    }
}

- (void)showLaunchingApp:(NSString*)appName
{
    (void)appName;
    [self updateActiveTabLabels];
    self.titleLabel.hidden = NO;
    self.statusLabel.hidden = NO;
    self.statusLabel.stringValue = @"Starting Android session...";
    [self.spinner startAnimation:nil];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)showRunningApp:(NSString*)appName
{
    (void)appName;
    [self updateActiveTabLabels];
    self.statusLabel.stringValue = @"Android is running.";
    [self.spinner stopAnimation:nil];
}

- (void)showStoppedWithMessage:(NSString*)message
{
    self.titleLabel.hidden = NO;
    self.statusLabel.hidden = NO;
    self.statusLabel.stringValue = message ?: @"Android session stopped.";
    [self.spinner stopAnimation:nil];
}

- (void)deviceBack:(id)sender
{
    (void)sender;
    if (self.actionHandler)
        self.actionHandler(@"back", self.activeTabIdentifier);
}

- (void)deviceHome:(id)sender
{
    (void)sender;
    [self focusLauncherTab];
    if (self.actionHandler)
        self.actionHandler(@"home", self.activeTabIdentifier);
}

- (void)deviceRecents:(id)sender
{
    (void)sender;
    if (self.tabs.count <= 1) {
        if (self.actionHandler)
            self.actionHandler(@"recents", self.activeTabIdentifier);
        return;
    }
    NSInteger index = [self indexOfTabWithIdentifier:self.activeTabIdentifier];
    NSUInteger next = index == NSNotFound
        ? 0
        : ((NSUInteger)index + 1) % self.tabs.count;
    self.activeTabIdentifier = self.tabs[next][@"identifier"];
    [self updateTabSelectionHighlight];
    [self updateActiveTabLabels];
    if (self.actionHandler)
        self.actionHandler(@"recents", self.activeTabIdentifier);
    if (self.tabFocusHandler)
        self.tabFocusHandler(self.activeTabIdentifier);
}

- (void)deviceInstall:(id)sender
{
    (void)sender;
    if (self.actionHandler)
        self.actionHandler(@"install-apk", self.activeTabIdentifier);
}

- (void)deviceSettings:(id)sender
{
    (void)sender;
    [self focusOrCreateTabWithIdentifier:@"settings" title:@"Settings"];
    if (self.actionHandler)
        self.actionHandler(@"settings", self.activeTabIdentifier);
}

- (void)closeTabWithIdentifier:(NSString*)identifier
{
    if (identifier.length == 0 ||
        [identifier isEqualToString:AndroidDeviceLauncherTabIdentifier]) {
        if (self.actionHandler)
            self.actionHandler(@"close-tab",
                               identifier ?: self.activeTabIdentifier);
        return;
    }
    NSInteger index = [self indexOfTabWithIdentifier:identifier];
    if (index == NSNotFound)
        return;
    BOOL wasActive = [self.activeTabIdentifier isEqualToString:identifier];
    [self.tabs removeObjectAtIndex:(NSUInteger)index];
    if (wasActive) {
        NSUInteger fallback = index > 0 ? (NSUInteger)index - 1 : 0;
        self.activeTabIdentifier = self.tabs.count > 0
            ? self.tabs[fallback][@"identifier"]
            : AndroidDeviceLauncherTabIdentifier;
    }
    [self updateTabControl];
    [self updateActiveTabLabels];
    if (self.tabCloseHandler)
        self.tabCloseHandler(identifier);
    if (wasActive && self.tabFocusHandler)
        self.tabFocusHandler(self.activeTabIdentifier);
}

- (void)deviceClose:(id)sender
{
    (void)sender;
    [self.window performClose:nil];
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Turn off Android device?";
    alert.informativeText =
        @"This will stop the running Android session for this prefix.";
    [alert addButtonWithTitle:@"Turn Off"];
    [alert addButtonWithTitle:@"Cancel"];
    return [alert runModal] == NSAlertFirstButtonReturn;
}

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (self.frameClientFd >= 0) {
        close(self.frameClientFd);
        self.frameClientFd = -1;
    }
    if (self.frameServerFd >= 0) {
        close(self.frameServerFd);
        self.frameServerFd = -1;
    }
    if (self.frameSocketPath.length > 0)
        unlink(self.frameSocketPath.fileSystemRepresentation);
    if (self.closeHandler)
        self.closeHandler();
}

@end
