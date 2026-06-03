#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/types.h>

#include "prefix.h"

namespace prefix = muplar::runtime::prefix;

static NSString* NSStringFromStdString(const std::string& value)
{
    return [NSString stringWithUTF8String:value.c_str()];
}

static NSString* NSStringFromPath(const std::filesystem::path& path)
{
    return path.empty() ? @"-" : NSStringFromStdString(path.string());
}

static std::string StdStringFromNSString(NSString* value)
{
    return value ? std::string(value.UTF8String) : std::string();
}

static std::filesystem::path ChildPathForName(const std::string& parent,
                                              const std::string& name)
{
    return std::filesystem::path(parent) / name;
}

static bool PathExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static NSImage* SymbolImage(NSString* name)
{
    if (@available(macOS 11.0, *)) {
        return [NSImage imageWithSystemSymbolName:name
                         accessibilityDescription:nil];
    }
    return nil;
}

static NSString* RuntimeDisplayName(const prefix::PrefixLayout& layout)
{
    NSString* kind = @"Android";
    switch (layout.kind) {
    case prefix::PrefixKind::Android:
        kind = @"Android";
        break;
    case prefix::PrefixKind::Linux:
        kind = @"Linux";
        break;
    case prefix::PrefixKind::Wine:
        kind = @"Windows";
        break;
    }

    NSString* arch = layout.arch == prefix::GuestArch::X86_64 ? @"x64" : @"ARM64";
    return [NSString stringWithFormat:@"%@ %@", kind, arch];
}

static NSString* CanonicalArchForUI(NSString* arch)
{
    if ([arch caseInsensitiveCompare:@"aarch64"] == NSOrderedSame ||
        [arch caseInsensitiveCompare:@"arm64"] == NSOrderedSame ||
        [arch caseInsensitiveCompare:@"ARM64"] == NSOrderedSame) {
        return @"ARM64";
    }
    if ([arch caseInsensitiveCompare:@"x86_64"] == NSOrderedSame ||
        [arch caseInsensitiveCompare:@"x64"] == NSOrderedSame) {
        return @"x64";
    }
    return arch;
}

static NSString* InternalArchFromUI(NSString* arch)
{
    if ([arch caseInsensitiveCompare:@"ARM64"] == NSOrderedSame ||
        [arch caseInsensitiveCompare:@"arm64"] == NSOrderedSame ||
        [arch caseInsensitiveCompare:@"aarch64"] == NSOrderedSame) {
        return @"aarch64";
    }
    if ([arch caseInsensitiveCompare:@"x64"] == NSOrderedSame ||
        [arch caseInsensitiveCompare:@"x86_64"] == NSOrderedSame) {
        return @"x86_64";
    }
    return arch;
}

static void StrokeLine(NSPoint a, NSPoint b, CGFloat width)
{
    NSBezierPath* path = [NSBezierPath bezierPath];
    path.lineWidth = width;
    [path moveToPoint:a];
    [path lineToPoint:b];
    [path stroke];
}

static NSImage* RuntimeIconImage(const prefix::PrefixLayout& layout)
{
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(32, 32)];
    [image lockFocus];

    switch (layout.kind) {
    case prefix::PrefixKind::Android: {
        NSColor* green = [NSColor colorWithCalibratedRed:0.18 green:0.68 blue:0.32 alpha:1.0];
        [green setFill];
        [green setStroke];

        NSBezierPath* head =
            [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(8, 17, 16, 9)
                                            xRadius:4
                                            yRadius:4];
        [head fill];
        NSBezierPath* body =
            [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(7, 8, 18, 12)
                                            xRadius:3
                                            yRadius:3];
        [body fill];
        StrokeLine(NSMakePoint(11, 26), NSMakePoint(8, 30), 1.6);
        StrokeLine(NSMakePoint(21, 26), NSMakePoint(24, 30), 1.6);
        StrokeLine(NSMakePoint(5, 11), NSMakePoint(5, 18), 2.0);
        StrokeLine(NSMakePoint(27, 11), NSMakePoint(27, 18), 2.0);
        [[NSColor whiteColor] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(12, 21, 2, 2)] fill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(20, 21, 2, 2)] fill];
        break;
    }
    case prefix::PrefixKind::Linux: {
        NSColor* shell = [NSColor colorWithCalibratedWhite:0.12 alpha:1.0];
        NSColor* prompt = [NSColor colorWithCalibratedRed:0.35 green:0.85 blue:0.45 alpha:1.0];
        [shell setFill];
        NSBezierPath* terminal =
            [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(5, 7, 22, 18)
                                            xRadius:4
                                            yRadius:4];
        [terminal fill];
        [prompt setStroke];
        StrokeLine(NSMakePoint(10, 18), NSMakePoint(13, 16), 1.6);
        StrokeLine(NSMakePoint(13, 16), NSMakePoint(10, 14), 1.6);
        StrokeLine(NSMakePoint(16, 13), NSMakePoint(22, 13), 1.6);
        break;
    }
    case prefix::PrefixKind::Wine: {
        NSColor* blue = [NSColor colorWithCalibratedRed:0.12 green:0.42 blue:0.85 alpha:1.0];
        [blue setFill];
        NSRect panes[] = {
            NSMakeRect(6, 17, 9, 9),
            NSMakeRect(17, 17, 9, 9),
            NSMakeRect(6, 6, 9, 9),
            NSMakeRect(17, 6, 9, 9),
        };
        for (NSRect pane : panes) {
            NSBezierPath* path =
                [NSBezierPath bezierPathWithRoundedRect:pane xRadius:1.5 yRadius:1.5];
            [path fill];
        }
        break;
    }
    }

    [image unlockFocus];
    return image;
}

static NSImage* InstancePreviewImage(const prefix::PrefixLayout& layout)
{
    return RuntimeIconImage(layout);
}

@interface LocationPickerButton : NSButton
@property(nonatomic, weak) NSTextField* locationField;
@end

@implementation LocationPickerButton
@end

@interface PrefixManagerAppDelegate
    : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
- (void)reloadPrefixesSelectingName:(const std::string&)name;
- (void)configureArchPopup:(NSPopUpButton*)archPopup forType:(NSString*)type;
- (void)pollWindowForPrefixName:(NSString*)prefixName existingPids:(NSSet<NSNumber*>*)existingPids checkCount:(int)checkCount;
- (NSSet<NSNumber*>*)getWineWindowPIDs;
@end

@implementation PrefixManagerAppDelegate {
    NSWindow* _window;
    NSTableView* _tableView;
    NSTextField* _statusLabel;
    NSButton* _cloneButton;
    NSButton* _deleteButton;
    NSButton* _openRootButton;
    NSButton* _startStopButton;  // Start / Stop for Wine instances
    NSProgressIndicator* _progressIndicator;
    NSTextField* _autoNameField;
    NSPopUpButton* _autoNameKindPopup;
    NSPopUpButton* _autoNameArchPopup;
    NSString* _autoGeneratedName;
    BOOL _autoNameEdited;
    std::vector<prefix::PrefixLayout> _prefixes;
    NSMutableSet<NSString*>* _startingPrefixes;
    NSMutableSet<NSString*>* _stoppingPrefixes;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _startingPrefixes = [NSMutableSet set];
        _stoppingPrefixes = [NSMutableSet set];
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [self buildWindow];
    [self reloadPrefixes:nil];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}

- (NSButton*)toolbarButtonWithTitle:(NSString*)title
                             symbol:(NSString*)symbol
                             action:(SEL)action
{
    NSButton* button = [NSButton buttonWithTitle:title target:self action:action];
    button.bezelStyle = NSBezelStyleRounded;
    button.image = SymbolImage(symbol);
    button.imagePosition = NSImageLeading;
    return button;
}

- (NSTextField*)label:(NSString*)text
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
    label.textColor = NSColor.secondaryLabelColor;
    return label;
}

- (NSString*)defaultInstanceNameForType:(NSString*)type arch:(NSString*)arch
{
    NSString* normalizedType = type ?: @"Android";

    if ([normalizedType rangeOfString:@"wine" options:NSCaseInsensitiveSearch].location != NSNotFound ||
        [normalizedType rangeOfString:@"windows" options:NSCaseInsensitiveSearch].location != NSNotFound) {
        normalizedType = @"Windows";
    } else if ([normalizedType caseInsensitiveCompare:@"android"] == NSOrderedSame) {
        normalizedType = @"Android";
    } else if ([normalizedType caseInsensitiveCompare:@"linux"] == NSOrderedSame) {
        normalizedType = @"Linux";
    }

    return [NSString stringWithFormat:@"%@-%@", normalizedType, CanonicalArchForUI(arch)];
}

- (void)updateAutoInstanceName
{
    if (!_autoNameField || !_autoNameKindPopup || !_autoNameArchPopup ||
        _autoNameEdited) {
        return;
    }

    _autoGeneratedName =
        [self defaultInstanceNameForType:_autoNameKindPopup.titleOfSelectedItem
                                    arch:_autoNameArchPopup.titleOfSelectedItem];
    _autoNameField.stringValue = _autoGeneratedName;
}

- (void)autoNameChoiceChanged:(id)sender
{
    if (sender == _autoNameKindPopup) {
        [self configureArchPopup:_autoNameArchPopup
                         forType:_autoNameKindPopup.titleOfSelectedItem];
    }
    if (_autoNameField && _autoGeneratedName &&
        [_autoNameField.stringValue isEqualToString:_autoGeneratedName]) {
        _autoNameEdited = NO;
    }
    [self updateAutoInstanceName];
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    if (notification.object != _autoNameField)
        return;

    _autoNameEdited =
        _autoGeneratedName &&
        ![_autoNameField.stringValue isEqualToString:_autoGeneratedName];
}

- (void)trackAutoNameField:(NSTextField*)nameField
                 kindPopup:(NSPopUpButton*)kindPopup
                 archPopup:(NSPopUpButton*)archPopup
{
    _autoNameField = nameField;
    _autoNameKindPopup = kindPopup;
    _autoNameArchPopup = archPopup;
    _autoGeneratedName = nil;
    _autoNameEdited = NO;

    nameField.delegate = self;
    kindPopup.target = self;
    kindPopup.action = @selector(autoNameChoiceChanged:);
    archPopup.target = self;
    archPopup.action = @selector(autoNameChoiceChanged:);

    [self configureArchPopup:archPopup forType:kindPopup.titleOfSelectedItem];
    [self updateAutoInstanceName];
}

- (void)clearAutoNameTracking
{
    _autoNameField = nil;
    _autoNameKindPopup = nil;
    _autoNameArchPopup = nil;
    _autoGeneratedName = nil;
    _autoNameEdited = NO;
}

- (void)configureArchPopup:(NSPopUpButton*)archPopup forType:(NSString*)type
{
    if (!archPopup)
        return;

    NSString* selected = archPopup.titleOfSelectedItem ?: @"ARM64";
    NSString* normalizedType = type ? type.lowercaseString : @"android";
    BOOL isWine = [normalizedType rangeOfString:@"wine" options:NSCaseInsensitiveSearch].location != NSNotFound ||
              [normalizedType rangeOfString:@"windows" options:NSCaseInsensitiveSearch].location != NSNotFound;

    NSArray<NSString*>* choices;
    if ([normalizedType isEqualToString:@"android"]) {
        choices = @[@"ARM64"];
     } else if (isWine) {
         // Windows support is only x64 for now, and we don't want to show a choice that isn't supported
        choices = @[@"x64"];
    } else {
        // linux and any future types
        choices = @[@"ARM64", @"x64"];
    }

    [archPopup removeAllItems];
    [archPopup addItemsWithTitles:choices];
    if ([normalizedType isEqualToString:@"android"]) {
        [archPopup selectItemWithTitle:@"ARM64"];
    } else if ([selected isEqualToString:@"ARM64"]) {
        [archPopup selectItemWithTitle:@"x64"];
    } else if ([choices containsObject:selected]) {
        [archPopup selectItemWithTitle:selected];
    } else {
        [archPopup selectItemWithTitle:@"x64"];
    }
}

- (NSView*)detailRowWithLabel:(NSString*)label value:(NSView*)value
{
    NSStackView* row = [NSStackView stackViewWithViews:@[[self label:label], value]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeFirstBaseline;
    row.spacing = 10.0;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [row.views[0].widthAnchor constraintEqualToConstant:78.0].active = YES;
    return row;
}

- (NSView*)locationPickerWithField:(NSTextField*)field
{
    field.placeholderString = @"Default parent folder";
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationHorizontal];
    [field setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                    forOrientation:NSLayoutConstraintOrientationHorizontal];

    LocationPickerButton* choose = [[LocationPickerButton alloc] init];
    choose.title = @"Choose...";
    choose.target = self;
    choose.action = @selector(chooseLocation:);
    choose.bezelStyle = NSBezelStyleRounded;
    choose.locationField = field;

    NSStackView* row = [NSStackView stackViewWithViews:@[field, choose]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 8.0;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [field.widthAnchor constraintGreaterThanOrEqualToConstant:320.0].active = YES;
    return row;
}

- (void)chooseLocation:(id)sender
{
    NSTextField* field = [sender isKindOfClass:LocationPickerButton.class]
        ? ((LocationPickerButton*)sender).locationField
        : nil;
    if (!field)
        return;

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Choose";
    panel.title = @"Parent Folder";
    if (field.stringValue.length > 0)
        panel.directoryURL = [NSURL fileURLWithPath:field.stringValue];

    if ([panel runModal] == NSModalResponseOK && panel.URL)
        field.stringValue = panel.URL.path;
}

- (NSView*)dialogAccessoryWithGrid:(NSGridView*)grid
                             width:(CGFloat)width
                            height:(CGFloat)height
{
    NSView* form = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    [form addSubview:grid];
    [NSLayoutConstraint activateConstraints:@[
        [grid.leadingAnchor constraintEqualToAnchor:form.leadingAnchor],
        [grid.trailingAnchor constraintEqualToAnchor:form.trailingAnchor],
        [grid.topAnchor constraintEqualToAnchor:form.topAnchor],
        [grid.bottomAnchor constraintEqualToAnchor:form.bottomAnchor],
    ]];
    [form.widthAnchor constraintEqualToConstant:width].active = YES;
    [form.heightAnchor constraintEqualToConstant:height].active = YES;
    return form;
}

- (void)buildWindow
{
    _window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1120, 680)
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    _window.title = @"Muplar Instance Manager";
    _window.minSize = NSMakeSize(980, 560);
    [_window center];

    NSView* content = _window.contentView;
    NSStackView* root = [[NSStackView alloc] init];
    root.orientation = NSUserInterfaceLayoutOrientationVertical;
    root.spacing = 0.0;
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
    toolbar.edgeInsets = NSEdgeInsetsMake(12, 14, 12, 14);
    toolbar.spacing = 8.0;
    toolbar.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton* createButton =
        [self toolbarButtonWithTitle:@"New" symbol:@"plus" action:@selector(createPrefix:)];
    _cloneButton =
        [self toolbarButtonWithTitle:@"Clone" symbol:@"doc.on.doc" action:@selector(clonePrefix:)];
    _deleteButton =
        [self toolbarButtonWithTitle:@"Delete" symbol:@"trash" action:@selector(deletePrefix:)];
    NSButton* refreshButton =
        [self toolbarButtonWithTitle:@"Refresh" symbol:@"arrow.clockwise" action:@selector(reloadPrefixes:)];
    _openRootButton =
        [self toolbarButtonWithTitle:@"Open Root" symbol:@"folder" action:@selector(openSelectedRoot:)];

    [toolbar addArrangedSubview:createButton];
    [toolbar addArrangedSubview:_cloneButton];
    [toolbar addArrangedSubview:_deleteButton];
    [toolbar addArrangedSubview:refreshButton];
    [toolbar addArrangedSubview:_openRootButton];

    _startStopButton =
        [self toolbarButtonWithTitle:@"Start"
                              symbol:@"play.fill"
                              action:@selector(startInstance:)];
    _startStopButton.enabled = NO;
    [toolbar addArrangedSubview:_startStopButton];

    _progressIndicator = [[NSProgressIndicator alloc] init];
    _progressIndicator.style = NSProgressIndicatorStyleSpinning;
    _progressIndicator.controlSize = NSControlSizeSmall;
    _progressIndicator.displayedWhenStopped = NO;
    [_progressIndicator.widthAnchor constraintEqualToConstant:16.0].active = YES;
    [_progressIndicator.heightAnchor constraintEqualToConstant:16.0].active = YES;
    [toolbar addArrangedSubview:_progressIndicator];

    NSView* spacer = [[NSView alloc] init];
    [toolbar addArrangedSubview:spacer];
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow
                       forOrientation:NSLayoutConstraintOrientationHorizontal];
    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = NSColor.secondaryLabelColor;
    _statusLabel.alignment = NSTextAlignmentRight;
    _statusLabel.lineBreakMode = NSLineBreakByClipping;
    [_statusLabel.widthAnchor constraintGreaterThanOrEqualToConstant:150.0].active = YES;
    [toolbar addArrangedSubview:_statusLabel];
    [root addArrangedSubview:toolbar];

    NSBox* separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    [root addArrangedSubview:separator];

    NSView* body = [[NSView alloc] init];
    body.translatesAutoresizingMaskIntoConstraints = NO;
    [root addArrangedSubview:body];
    [body.heightAnchor constraintGreaterThanOrEqualToConstant:420.0].active = YES;

    NSView* listPane = [[NSView alloc] init];
    listPane.translatesAutoresizingMaskIntoConstraints = NO;
    [listPane setContentHuggingPriority:NSLayoutPriorityDefaultLow
                         forOrientation:NSLayoutConstraintOrientationHorizontal];
    [listPane setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                      forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSTextField* listTitle = [NSTextField labelWithString:@"Instances"];
    listTitle.font = [NSFont systemFontOfSize:18 weight:NSFontWeightSemibold];
    listTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [listPane addSubview:listTitle];

    _tableView = [[NSTableView alloc] init];
    _tableView.delegate = self;
    _tableView.dataSource = self;
    _tableView.usesAlternatingRowBackgroundColors = YES;
    _tableView.rowHeight = 48.0;
    _tableView.allowsMultipleSelection = NO;
    _tableView.allowsEmptySelection = NO;
    _tableView.target = self;
    _tableView.action = @selector(tableSelectionAction:);
    _tableView.columnAutoresizingStyle = NSTableViewSequentialColumnAutoresizingStyle;

    NSArray<NSArray<NSString*>*>* columns = @[
        @[@"preview", @"OS", @"64"],
        @[@"name", @"Instance", @"240"],
        @[@"state", @"Status", @"100"],
        @[@"runtime", @"Runtime", @"150"],
        @[@"runner", @"Runner", @"110"],
        @[@"root", @"Location", @"420"],
    ];
    for (NSArray<NSString*>* spec in columns) {
        NSTableColumn* column =
            [[NSTableColumn alloc] initWithIdentifier:spec[0]];
        column.title = spec[1];
        column.width = spec[2].doubleValue;
        column.minWidth = [spec[0] isEqualToString:@"preview"] ? 64.0 : 70.0;
        if ([spec[0] isEqualToString:@"preview"])
            column.maxWidth = 64.0;
        [_tableView addTableColumn:column];
    }

    NSScrollView* tableScroll = [[NSScrollView alloc] init];
    tableScroll.documentView = _tableView;
    tableScroll.hasVerticalScroller = YES;
    tableScroll.hasHorizontalScroller = YES;
    tableScroll.translatesAutoresizingMaskIntoConstraints = NO;
    [tableScroll setContentHuggingPriority:NSLayoutPriorityDefaultLow
                            forOrientation:NSLayoutConstraintOrientationHorizontal];
    [tableScroll setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                         forOrientation:NSLayoutConstraintOrientationHorizontal];
    [listPane addSubview:tableScroll];

    [body addSubview:listPane];
    [NSLayoutConstraint activateConstraints:@[
        [listPane.leadingAnchor constraintEqualToAnchor:body.leadingAnchor],
        [listPane.trailingAnchor constraintEqualToAnchor:body.trailingAnchor],
        [listPane.topAnchor constraintEqualToAnchor:body.topAnchor],
        [listPane.bottomAnchor constraintEqualToAnchor:body.bottomAnchor],

        [listTitle.leadingAnchor constraintEqualToAnchor:listPane.leadingAnchor constant:18.0],
        [listTitle.topAnchor constraintEqualToAnchor:listPane.topAnchor constant:18.0],
        [listTitle.trailingAnchor constraintLessThanOrEqualToAnchor:listPane.trailingAnchor constant:-18.0],

        [tableScroll.leadingAnchor constraintEqualToAnchor:listPane.leadingAnchor constant:18.0],
        [tableScroll.trailingAnchor constraintEqualToAnchor:listPane.trailingAnchor constant:-18.0],
        [tableScroll.topAnchor constraintEqualToAnchor:listTitle.bottomAnchor constant:12.0],
        [tableScroll.bottomAnchor constraintEqualToAnchor:listPane.bottomAnchor constant:-18.0],
        [tableScroll.widthAnchor constraintGreaterThanOrEqualToConstant:560.0],
    ]];

    [self updateSelectionState];
}

- (void)showError:(NSString*)message
{
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = message;
    [alert runModal];
}

- (NSInteger)selectedIndex
{
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || static_cast<size_t>(row) >= _prefixes.size())
        return -1;
    return row;
}

- (prefix::PrefixLayout*)selectedPrefix
{
    NSInteger row = [self selectedIndex];
    if (row < 0)
        return nullptr;
    return &_prefixes[static_cast<size_t>(row)];
}

- (NSString*)tableValueForColumn:(NSString*)identifier
                             row:(NSInteger)row
{
    const auto& item = _prefixes[static_cast<size_t>(row)];
    if ([identifier isEqualToString:@"name"])
        return NSStringFromStdString(item.name);
    if ([identifier isEqualToString:@"state"]) {
        NSString* name = NSStringFromStdString(item.name);
        if ([_startingPrefixes containsObject:name]) {
            return @"Starting...";
        }
        if ([_stoppingPrefixes containsObject:name]) {
            return @"Stopping...";
        }
        auto state = prefix::query_prefix_state(item);
        return (state == prefix::PrefixState::Running) ? @"Running" : @"Stopped";
    }
    if ([identifier isEqualToString:@"runtime"])
        return RuntimeDisplayName(item);
    if ([identifier isEqualToString:@"runner"])
        return NSStringFromStdString(item.runner);
    if ([identifier isEqualToString:@"root"])
        return NSStringFromPath(item.root);
    return @"";
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return static_cast<NSInteger>(_prefixes.size());
}

- (NSView*)tableView:(NSTableView*)tableView
 viewForTableColumn:(NSTableColumn*)tableColumn
                 row:(NSInteger)row
{
    if ([tableColumn.identifier isEqualToString:@"preview"]) {
        NSTableCellView* cell =
            [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
        if (!cell) {
            cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 64, 48)];
            cell.identifier = tableColumn.identifier;
            NSImageView* imageView = [[NSImageView alloc] init];
            imageView.translatesAutoresizingMaskIntoConstraints = NO;
            imageView.imageScaling = NSImageScaleProportionallyUpOrDown;
            cell.imageView = imageView;
            [cell addSubview:imageView];
            [NSLayoutConstraint activateConstraints:@[
                [imageView.centerXAnchor constraintEqualToAnchor:cell.centerXAnchor],
                [imageView.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                [imageView.widthAnchor constraintEqualToConstant:36.0],
                [imageView.heightAnchor constraintEqualToConstant:36.0],
            ]];
        }
        cell.imageView.image =
            InstancePreviewImage(_prefixes[static_cast<size_t>(row)]);
        return cell;
    }

    NSTableCellView* cell =
        [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];
    if (!cell) {
        cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 120, 28)];
        cell.identifier = tableColumn.identifier;
        NSTextField* text = [NSTextField labelWithString:@""];
        text.translatesAutoresizingMaskIntoConstraints = NO;
        text.lineBreakMode = NSLineBreakByTruncatingMiddle;
        cell.textField = text;
        [cell addSubview:text];
        [NSLayoutConstraint activateConstraints:@[
            [text.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:8],
            [text.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-8],
            [text.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    }
    cell.textField.stringValue =
        [self tableValueForColumn:tableColumn.identifier row:row];
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    [self updateSelectionState];
}

- (void)tableSelectionAction:(id)sender
{
    (void)sender;
    [self updateSelectionState];
}

- (void)updateSelectionState
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    BOOL enabled = selected != nullptr;
    _cloneButton.enabled = enabled;
    _deleteButton.enabled = enabled;
    _openRootButton.enabled = enabled;

    // Start/Stop button: only available for Wine prefixes
    if (!selected || selected->kind != prefix::PrefixKind::Wine) {
        _startStopButton.enabled = NO;
        _startStopButton.title = @"Start";
        _startStopButton.image = SymbolImage(@"play.fill");
        [_progressIndicator stopAnimation:nil];
    } else {
        NSString* name = NSStringFromStdString(selected->name);
        if ([_startingPrefixes containsObject:name]) {
            _startStopButton.enabled = NO;
            _startStopButton.title = @"Starting...";
            _startStopButton.image = nil;
            [_progressIndicator startAnimation:nil];
        } else if ([_stoppingPrefixes containsObject:name]) {
            _startStopButton.enabled = NO;
            _startStopButton.title = @"Stopping...";
            _startStopButton.image = nil;
            [_progressIndicator startAnimation:nil];
        } else {
            [_progressIndicator stopAnimation:nil];
            _startStopButton.enabled = YES;
            auto state = prefix::query_prefix_state(*selected);
            if (state == prefix::PrefixState::Running) {
                _startStopButton.title = @"Stop";
                _startStopButton.image = SymbolImage(@"stop.fill");
                _startStopButton.action = @selector(stopInstance:);
            } else {
                _startStopButton.title = @"Start";
                _startStopButton.image = SymbolImage(@"play.fill");
                _startStopButton.action = @selector(startInstance:);
            }
        }
    }
}

- (void)reloadPrefixes:(id)sender
{
    (void)sender;
    std::string selectedName;
    if (prefix::PrefixLayout* selected = [self selectedPrefix])
        selectedName = selected->name;
    [self reloadPrefixesSelectingName:selectedName];
}

- (void)reloadPrefixesSelectingName:(const std::string&)selectedName
{

    try {
        _prefixes = prefix::list_prefixes();
        std::sort(_prefixes.begin(), _prefixes.end(),
                  [](const auto& a, const auto& b) { return a.name < b.name; });
        [_tableView reloadData];

        NSInteger selectedRow = -1;
        for (size_t i = 0; i < _prefixes.size(); ++i) {
            if (_prefixes[i].name == selectedName) {
                selectedRow = static_cast<NSInteger>(i);
                break;
            }
        }
        if (selectedRow < 0 && !_prefixes.empty())
            selectedRow = 0;
        if (selectedRow >= 0) {
            [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:selectedRow]
                    byExtendingSelection:NO];
            [_tableView scrollRowToVisible:selectedRow];
        }

        _statusLabel.stringValue =
            [NSString stringWithFormat:@"%lu instance%@",
                                       static_cast<unsigned long>(_prefixes.size()),
                                       _prefixes.size() == 1 ? @"" : @"s"];
        [self updateSelectionState];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateSelectionState];
        });
    } catch (const std::exception& e) {
        [self showError:NSStringFromStdString(e.what())];
    }
}

- (void)createPrefix:(id)sender
{
    (void)sender;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"New Instance";
    [alert addButtonWithTitle:@"Create"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField* nameField = [NSTextField textFieldWithString:@"android"];
    NSTextField* locationField = [NSTextField textFieldWithString:@""];
    NSPopUpButton* kindPopup = [[NSPopUpButton alloc] init];
    [kindPopup addItemsWithTitles:@[@"Android", @"Linux", @"Wine (Windows)"]];
    NSPopUpButton* archPopup = [[NSPopUpButton alloc] init];
    [archPopup addItemsWithTitles:@[@"ARM64", @"x64"]];
    NSTextField* sysrootField = [NSTextField textFieldWithString:@"build/sysroot"];
    [self trackAutoNameField:nameField kindPopup:kindPopup archPopup:archPopup];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[[self label:@"Name"], nameField],
        @[[self label:@"Type"], kindPopup],
        @[[self label:@"Arch"], archPopup],
        @[[self label:@"Parent"], [self locationPickerWithField:locationField]],
        @[[self label:@"Sysroot"], sysrootField],
    ]];
    grid.rowSpacing = 8.0;
    grid.columnSpacing = 12.0;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:1].width = 420.0;
    alert.accessoryView = [self dialogAccessoryWithGrid:grid
                                                  width:540.0
                                                 height:174.0];
    alert.window.initialFirstResponder = nameField;

    NSModalResponse response = [alert runModal];
    [self clearAutoNameTracking];
    if (response != NSAlertFirstButtonReturn)
        return;

    std::string name = StdStringFromNSString(nameField.stringValue);
    if (name.empty()) {
        [self showError:@"Name is required."];
        return;
    }

    try {
        std::string kind = StdStringFromNSString(kindPopup.titleOfSelectedItem.lowercaseString);
        if (kind.find("wine") != std::string::npos || kind.find("windows") != std::string::npos)
            kind = "wine";
        else if (kind == "android" || kind == "linux") {
            // already correct, lowercased
            std::transform(kind.begin(), kind.end(), kind.begin(), ::tolower);
        }
        std::string location = StdStringFromNSString(locationField.stringValue);
        std::filesystem::path targetRoot = location.empty()
            ? prefix::resolve_prefix_root(name)
            : ChildPathForName(location, name);
        if (PathExists(targetRoot)) {
            [self showError:
                [NSString stringWithFormat:@"Instance location already exists: %@",
                                           NSStringFromPath(targetRoot)]];
            return;
        }
        prefix::open_prefix_at_root(
            name,
            targetRoot,
            StdStringFromNSString(sysrootField.stringValue),
            true,
            prefix::parse_prefix_kind(kind),
            prefix::parse_guest_arch(
                StdStringFromNSString(
                    InternalArchFromUI(archPopup.titleOfSelectedItem))),
            "elfuse");
        [self reloadPrefixesSelectingName:name];
    } catch (const std::exception& e) {
        [self showError:NSStringFromStdString(e.what())];
    }
}

- (void)clonePrefix:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected)
        return;
    std::string sourceRoot = selected->root.string();
    std::string sourceName = selected->name;

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Clone Instance";
    [alert addButtonWithTitle:@"Clone"];
    [alert addButtonWithTitle:@"Cancel"];

    NSString* defaultName =
        [NSString stringWithFormat:@"%s-copy", sourceName.c_str()];
    NSTextField* nameField = [NSTextField textFieldWithString:defaultName];
    NSTextField* locationField = [NSTextField textFieldWithString:@""];
    NSButton* replace =
        [NSButton checkboxWithTitle:@"Replace existing" target:nil action:nil];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[[self label:@"Name"], nameField],
        @[[self label:@"Parent"], [self locationPickerWithField:locationField]],
        @[[self label:@""], replace],
    ]];
    grid.rowSpacing = 8.0;
    grid.columnSpacing = 12.0;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:1].width = 420.0;
    alert.accessoryView = [self dialogAccessoryWithGrid:grid
                                                  width:540.0
                                                 height:100.0];
    alert.window.initialFirstResponder = nameField;

    if ([alert runModal] != NSAlertFirstButtonReturn)
        return;

    try {
        std::string newName = StdStringFromNSString(nameField.stringValue);
        if (newName.empty()) {
            [self showError:@"Name is required."];
            return;
        }

        std::string location = StdStringFromNSString(locationField.stringValue);
        std::filesystem::path targetRoot = location.empty()
            ? prefix::resolve_prefix_root(newName)
            : ChildPathForName(location, newName);
        BOOL replaceExisting = replace.state == NSControlStateValueOn;
        if (PathExists(targetRoot) && !replaceExisting) {
            [self showError:
                [NSString stringWithFormat:@"Instance location already exists: %@",
                                           NSStringFromPath(targetRoot)]];
            return;
        }
        prefix::clone_prefix_to_root(sourceRoot, newName, targetRoot,
                                     replaceExisting);
        [self reloadPrefixesSelectingName:newName];
    } catch (const std::exception& e) {
        [self showError:NSStringFromStdString(e.what())];
    }
}

- (void)deletePrefix:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected)
        return;
    std::string selectedRoot = selected->root.string();
    std::string selectedName = selected->name;

    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText =
        [NSString stringWithFormat:@"Delete %@?",
                                   NSStringFromStdString(selectedName)];
    alert.informativeText = NSStringFromStdString(selectedRoot);
    [alert addButtonWithTitle:@"Delete"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn)
        return;

    try {
        prefix::delete_prefix(selectedRoot);
        [self reloadPrefixes:nil];
    } catch (const std::exception& e) {
        [self showError:NSStringFromStdString(e.what())];
    }
}

- (void)openSelectedRoot:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected)
        return;

    NSURL* url = [NSURL fileURLWithPath:NSStringFromPath(selected->root)];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

// ── Helper: locate wine64 for a prefix ─────────────────────────────────────

- (NSString*)wine64PathForPrefix:(const prefix::PrefixLayout&)layout
{
    // 1. Embedded in the app bundle: Contents/Frameworks/wine/bin/wine  (preferred)
    NSString* frameworksPath = [[NSBundle mainBundle] privateFrameworksPath];
    NSString* bundleWine = [[[frameworksPath stringByAppendingPathComponent:@"wine"]
                                             stringByAppendingPathComponent:@"bin"]
                                             stringByAppendingPathComponent:@"wine"];
    NSLog(@"[wine] checking bundle: %@", bundleWine);
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:bundleWine])
        return bundleWine;

    // 2. runtime_sysroot stored in the prefix.
    if (!layout.runtime_sysroot.empty()) {
        std::filesystem::path c = layout.runtime_sysroot / "bin" / "wine";
        NSLog(@"[wine] checking sysroot: %s", c.c_str());
        if (std::filesystem::is_regular_file(c))
            return NSStringFromStdString(c.string());
    }

    // 3. Dev build: build/bin/App.app -> build/bin -> build -> build/wine-prefix/bin/wine
    NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
    NSString* buildDir   = [[bundlePath stringByDeletingLastPathComponent]
                                        stringByDeletingLastPathComponent];
    NSString* devCandidate = [[[[buildDir stringByAppendingPathComponent:@"wine-prefix"]
                                          stringByAppendingPathComponent:@"bin"]
                                          stringByAppendingPathComponent:@"wine"]
                                          stringByStandardizingPath];
    NSLog(@"[wine] checking dev build: %@", devCandidate);
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:devCandidate])
        return devCandidate;

    // 4. System PATH.
    NSTask* which = [[NSTask alloc] init];
    which.launchPath = @"/usr/bin/which";
    which.arguments  = @[@"wine"];
    NSPipe* pipe = [NSPipe pipe];
    which.standardOutput = pipe;
    which.standardError  = [NSFileHandle fileHandleWithNullDevice];
    @try {
        [which launch];
        [which waitUntilExit];
        NSData*   data   = [[pipe fileHandleForReading] readDataToEndOfFile];
        NSString* result = [[NSString alloc] initWithData:data
                                                 encoding:NSUTF8StringEncoding];
        result = [result stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSLog(@"[wine] which: %@", result);
        if (result.length > 0 &&
            [[NSFileManager defaultManager] isExecutableFileAtPath:result])
            return result;
    } @catch (...) {}

    NSLog(@"[wine] not found. bundle=%@", [[NSBundle mainBundle] bundlePath]);
    return nil;
}

// ── Start a Wine prefix instance ────────────────────────────────────────────

- (void)startInstance:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected || selected->kind != prefix::PrefixKind::Wine)
        return;

    // Prevent double-start.
    if (prefix::query_prefix_state(*selected) == prefix::PrefixState::Running) {
        pid_t pid = prefix::read_prefix_pid(*selected);
        [self showError:[NSString stringWithFormat:
            @"Instance '%@' is already running (PID %d).",
            NSStringFromStdString(selected->name), (int)pid]];
        return;
    }

    NSString* prefixName = NSStringFromStdString(selected->name);
    [_startingPrefixes addObject:prefixName];
    [self updateSelectionState];

    // Locate wine binary.
    NSString* wineBin = [self wine64PathForPrefix:*selected];
    if (!wineBin) {
        [self showError:@"wine binary not found. Build Wine first or set the prefix sysroot."];
        return;
    }

    NSString* winePrefix = NSStringFromPath(selected->rootfs);
    std::filesystem::path pidFilePath = prefix::pid_file_path(*selected);
    NSString* pidDir  = NSStringFromStdString(pidFilePath.parent_path().string());
    NSString* pidFile = NSStringFromStdString(pidFilePath.string());

    // Ensure run/ directory exists.
    [[NSFileManager defaultManager] createDirectoryAtPath:pidDir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];

    // Log file in the prefix logs/ directory.
    NSString* logsDir = NSStringFromStdString(selected->logs_dir.string());
    [[NSFileManager defaultManager] createDirectoryAtPath:logsDir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString* logFile = [logsDir stringByAppendingPathComponent:@"muplar.log"];
    [[NSFileManager defaultManager] createFileAtPath:logFile contents:nil attributes:nil];
    NSFileHandle* logHandle = [NSFileHandle fileHandleForWritingAtPath:logFile];

    // Build environment.
    NSMutableDictionary* env = [NSProcessInfo.processInfo.environment mutableCopy];
    env[@"WINEPREFIX"] = winePrefix;
    // Prefer the bundle-embedded DLL path; fall back to path relative to wineBin.
    NSString* bundleDllPath = [[[[NSBundle mainBundle] privateFrameworksPath]
                                  stringByAppendingPathComponent:@"wine"]
                                  stringByAppendingPathComponent:@"lib/wine/x86_64-windows"];
    NSString* siblingDllPath = [[[wineBin stringByDeletingLastPathComponent]
                                          stringByDeletingLastPathComponent]
                                          stringByAppendingPathComponent:@"lib/wine/x86_64-windows"];
    NSString* wineLibDir = [[NSFileManager defaultManager] fileExistsAtPath:bundleDllPath]
                           ? bundleDllPath : siblingDllPath;
    if ([[NSFileManager defaultManager] fileExistsAtPath:wineLibDir])
        env[@"WINEDLLPATH"] = wineLibDir;

    // Set fallback library path so Wine can find FreeType (fonts) and other Homebrew dependencies on macOS.
    NSString* wineLibPath = [[[wineBin stringByDeletingLastPathComponent]
                                       stringByDeletingLastPathComponent]
                                       stringByAppendingPathComponent:@"lib"];
    NSString* existingFallback = env[@"DYLD_FALLBACK_LIBRARY_PATH"];
    NSString* newFallback = [NSString stringWithFormat:@"/usr/local/lib:/opt/homebrew/lib:%@%@",
                             wineLibPath,
                             existingFallback ? [NSString stringWithFormat:@":%@", existingFallback] : @""];
    env[@"DYLD_FALLBACK_LIBRARY_PATH"] = newFallback;
    [env removeObjectForKey:@"DYLD_LIBRARY_PATH"];

    env[@"WINEDEBUG"] = @"+err";

    NSString* header = [NSString stringWithFormat:
        @"=== wine start ===\nCOMMAND: \"%@\" explorer\nWINEPREFIX: %@\nENVIRONMENT: %@\n==================\n",
        wineBin, winePrefix, env];
    [logHandle writeData:[header dataUsingEncoding:NSUTF8StringEncoding]];

    // Launch wine explorer directly.
    NSTask* task = [[NSTask alloc] init];
    task.launchPath    = wineBin;
    // task.arguments     = @[@"explorer", @"/desktop=Muplar,1920x1080"];
    task.arguments     = @[@"explorer"];
    // task.arguments     = @[@"winecfg"];
    task.environment   = env;
    task.standardOutput = logHandle;
    task.standardError  = logHandle;

    task.terminationHandler = ^(NSTask* __unused t) {
        NSString* footer = [NSString stringWithFormat:@"\n=== wine exited (status=%d) ===\n",
                            t.terminationStatus];
        [logHandle writeData:[footer dataUsingEncoding:NSUTF8StringEncoding]];
        [logHandle closeFile];
        [[NSFileManager defaultManager] removeItemAtPath:pidFile error:nil];
        NSLog(@"[wine] terminationHandler fired for prefixName: %@", prefixName);
        dispatch_async(dispatch_get_main_queue(), ^{
            NSLog(@"[wine] clearing starting/stopping states for prefixName: %@", prefixName);
            NSLog(@"[wine] _startingPrefixes before: %@", self->_startingPrefixes);
            [self->_startingPrefixes removeObject:prefixName];
            [self->_stoppingPrefixes removeObject:prefixName];
            NSLog(@"[wine] _startingPrefixes after: %@", self->_startingPrefixes);
            [self reloadPrefixes:nil];
        });
    };

    // Collect existing Wine window PIDs before starting.
    NSSet<NSNumber*>* existingPids = [self getWineWindowPIDs];

    @try {
        NSLog(@"[wine] launching task for prefixName: %@", prefixName);
        [task launch];
    } @catch (NSException* ex) {
        [self showError:[NSString stringWithFormat:@"Failed to start Wine: %@\nLog: %@",
                         ex.reason, logFile]];
        [logHandle closeFile];
        return;
    }

    // Write PID file.
    NSString* pidContent = [NSString stringWithFormat:@"%d\n", task.processIdentifier];
    [pidContent writeToFile:pidFile atomically:YES encoding:NSUTF8StringEncoding error:nil];

    // Start polling window to detect when explorer window shows up.
    [self pollWindowForPrefixName:prefixName existingPids:existingPids checkCount:0];

    // Show log path in status bar.
    _statusLabel.stringValue = [NSString stringWithFormat:@"Log: %@", logFile];
    [self reloadPrefixes:nil];
}

// ── Stop a running Wine prefix instance ─────────────────────────────────────

- (void)stopInstance:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected)
        return;

    pid_t pid = prefix::read_prefix_pid(*selected);
    if (pid <= 0) {
        [self showError:@"Could not read PID — instance may have already stopped."];
        [self reloadPrefixes:nil];
        return;
    }

    // Confirm.
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithFormat:@"Stop '%@'?",
                         NSStringFromStdString(selected->name)];
    alert.informativeText = [NSString stringWithFormat:
        @"This will shut down the Wine session (PID %d).", (int)pid];
    [alert addButtonWithTitle:@"Stop"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn)
        return;

    NSString* prefixName = NSStringFromStdString(selected->name);
    [_stoppingPrefixes addObject:prefixName];
    [self updateSelectionState];

    // Use `wineserver -k` for a clean Wine shutdown.
    // This terminates all Wine processes for the current WINEPREFIX,
    // which unblocks `wineserver --wait` in our shell wrapper so the
    // NSTask exits cleanly and the terminationHandler fires.
    NSString* wineBin = [self wine64PathForPrefix:*selected];
    if (wineBin) {
        NSString* wineServer = [[wineBin stringByDeletingLastPathComponent]
                                stringByAppendingPathComponent:@"wineserver"];
        NSString* winePrefix = NSStringFromPath(selected->rootfs);

        NSTask* kill = [[NSTask alloc] init];
        kill.launchPath  = wineServer;
        kill.arguments   = @[@"-k"];
        NSMutableDictionary* env = [NSProcessInfo.processInfo.environment mutableCopy];
        env[@"WINEPREFIX"] = winePrefix;
        kill.environment = env;
        kill.standardOutput = [NSFileHandle fileHandleWithNullDevice];
        kill.standardError  = [NSFileHandle fileHandleWithNullDevice];
        @try { [kill launch]; [kill waitUntilExit]; } @catch (...) {}
    } else {
        // Fallback: SIGTERM the shell wrapper.
        ::kill(pid, SIGTERM);
    }
}

- (NSSet<NSNumber*>*)getWineWindowPIDs
{
    NSMutableSet<NSNumber*>* pids = [NSMutableSet set];
    CFArrayRef windowList = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (windowList) {
        for (NSDictionary* entry in (__bridge NSArray*)windowList) {
            NSString* ownerName = entry[(id)kCGWindowOwnerName];
            if (ownerName && [ownerName caseInsensitiveCompare:@"wine"] == NSOrderedSame) {
                NSNumber* ownerPID = entry[(id)kCGWindowOwnerPID];
                if (ownerPID) {
                    [pids addObject:ownerPID];
                }
            }
        }
        CFRelease(windowList);
    }
    return pids;
}

- (void)pollWindowForPrefixName:(NSString*)prefixName existingPids:(NSSet<NSNumber*>*)existingPids checkCount:(int)checkCount
{
    // If the process is no longer starting, stop polling.
    if (![_startingPrefixes containsObject:prefixName]) {
        return;
    }

    // Limit the polling to 60 seconds (600 ticks of 100ms).
    if (checkCount > 600) {
        [_startingPrefixes removeObject:prefixName];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self reloadPrefixes:nil];
        });
        return;
    }

    BOOL windowFound = NO;
    CFArrayRef windowList = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (windowList) {
        for (NSDictionary* entry in (__bridge NSArray*)windowList) {
            NSString* ownerName = entry[(id)kCGWindowOwnerName];
            if (ownerName && [ownerName caseInsensitiveCompare:@"wine"] == NSOrderedSame) {
                NSNumber* ownerPID = entry[(id)kCGWindowOwnerPID];
                if (ownerPID && ![existingPids containsObject:ownerPID]) {
                    NSNumber* layer = entry[(id)kCGWindowLayer];
                    NSDictionary* bounds = entry[(id)kCGWindowBounds];
                    if (layer && layer.intValue == 0 && bounds) {
                        windowFound = YES;
                        break;
                    }
                }
            }
        }
        CFRelease(windowList);
    }

    if (windowFound) {
        NSLog(@"[wine] new window found for prefixName: %@", prefixName);
        [_startingPrefixes removeObject:prefixName];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self reloadPrefixes:nil];
        });
    } else {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(100 * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{
            [self pollWindowForPrefixName:prefixName existingPids:existingPids checkCount:checkCount + 1];
        });
    }
}

@end

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        PrefixManagerAppDelegate* delegate =
            [[PrefixManagerAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
