#import "AndroidDeviceShell.h"

@interface AndroidDeviceShell ()
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTextField* titleLabel;
@property(nonatomic, strong) NSTextField* statusLabel;
@property(nonatomic, strong) NSProgressIndicator* spinner;
@property(nonatomic, strong) NSSegmentedControl* tabControl;
@property(nonatomic, strong) NSMutableArray<NSDictionary<NSString*, NSString*>*>* tabs;
@property(nonatomic, copy) NSString* activeTabIdentifier;
@end

@implementation AndroidDeviceShell

static NSString* const AndroidDeviceLauncherTabIdentifier = @"launcher";

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

- (instancetype)initWithPrefixName:(NSString*)prefixName
{
    self = [super init];
    if (!self)
        return nil;

    _tabs = [NSMutableArray array];
    _activeTabIdentifier = AndroidDeviceLauncherTabIdentifier;

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
    [toolbar addArrangedSubview:AndroidDeviceIconButton(
                                    @"xmark.circle", @"Close Active Tab", self,
                                    @selector(deviceCloseTab:))];

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

    _tabControl = [[NSSegmentedControl alloc] init];
    _tabControl.segmentStyle = NSSegmentStyleSeparated;
    _tabControl.trackingMode = NSSegmentSwitchTrackingSelectOne;
    _tabControl.target = self;
    _tabControl.action = @selector(deviceTabChanged:);
    _tabControl.translatesAutoresizingMaskIntoConstraints = NO;
    [root addArrangedSubview:_tabControl];
    [_tabControl.widthAnchor constraintEqualToConstant:460.0].active = YES;
    [_tabControl.heightAnchor constraintEqualToConstant:30.0].active = YES;

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
    self.tabControl.segmentCount = self.tabs.count;
    NSInteger selectedSegment = -1;
    for (NSUInteger i = 0; i < self.tabs.count; ++i) {
        NSDictionary<NSString*, NSString*>* tab = self.tabs[i];
        [self.tabControl setLabel:AndroidDeviceTabTitle(tab) forSegment:i];
        [self.tabControl setWidth:0.0 forSegment:i];
        if ([tab[@"identifier"] isEqualToString:self.activeTabIdentifier])
            selectedSegment = (NSInteger)i;
    }
    self.tabControl.selectedSegment = selectedSegment;
}

- (void)updateActiveTabLabels
{
    NSInteger index = [self indexOfTabWithIdentifier:self.activeTabIdentifier];
    NSString* title = index == NSNotFound
        ? @"Launcher"
        : AndroidDeviceTabTitle(self.tabs[(NSUInteger)index]);
    self.titleLabel.stringValue = title;
}

- (void)focusOrCreateTabWithIdentifier:(NSString*)identifier title:(NSString*)title
{
    NSString* tabIdentifier = identifier.length > 0
        ? identifier
        : AndroidDeviceLauncherTabIdentifier;
    NSString* tabTitle = title.length > 0 ? title : @"App";

    if ([self indexOfTabWithIdentifier:tabIdentifier] == NSNotFound) {
        [self.tabs addObject:@{
            @"identifier": tabIdentifier,
            @"title": tabTitle,
        }];
    }
    self.activeTabIdentifier = tabIdentifier;
    [self updateTabControl];
    [self updateActiveTabLabels];
    if (self.tabFocusHandler)
        self.tabFocusHandler(tabIdentifier);
}

- (void)focusLauncherTab
{
    [self focusOrCreateTabWithIdentifier:AndroidDeviceLauncherTabIdentifier
                                   title:@"Launcher"];
}

- (void)showLaunchingApp:(NSString*)appName
{
    (void)appName;
    [self updateActiveTabLabels];
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
    [self updateTabControl];
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

- (void)deviceCloseTab:(id)sender
{
    (void)sender;
    if ([self.activeTabIdentifier isEqualToString:AndroidDeviceLauncherTabIdentifier]) {
        if (self.actionHandler)
            self.actionHandler(@"close-tab", self.activeTabIdentifier);
        return;
    }
    NSString* closedIdentifier = self.activeTabIdentifier;
    NSInteger index = [self indexOfTabWithIdentifier:self.activeTabIdentifier];
    if (index == NSNotFound) {
        [self focusLauncherTab];
        return;
    }
    [self.tabs removeObjectAtIndex:(NSUInteger)index];
    NSUInteger fallback = index > 0 ? (NSUInteger)index - 1 : 0;
    self.activeTabIdentifier = self.tabs.count > 0
        ? self.tabs[fallback][@"identifier"]
        : AndroidDeviceLauncherTabIdentifier;
    [self updateTabControl];
    [self updateActiveTabLabels];
    if (self.tabCloseHandler)
        self.tabCloseHandler(closedIdentifier);
    if (self.tabFocusHandler)
        self.tabFocusHandler(self.activeTabIdentifier);
}
- (void)deviceClose:(id)sender
{
    (void)sender;
    [self.window performClose:nil];
}

- (void)deviceTabChanged:(id)sender
{
    (void)sender;
    NSInteger selected = self.tabControl.selectedSegment;
    if (selected < 0 || (NSUInteger)selected >= self.tabs.count)
        return;
    self.activeTabIdentifier = self.tabs[(NSUInteger)selected][@"identifier"];
    [self updateActiveTabLabels];
    if (self.tabFocusHandler)
        self.tabFocusHandler(self.activeTabIdentifier);
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
    if (self.closeHandler)
        self.closeHandler();
}

@end
