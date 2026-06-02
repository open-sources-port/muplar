#import <AppKit/AppKit.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

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
@end

@implementation PrefixManagerAppDelegate {
    NSWindow* _window;
    NSTableView* _tableView;
    NSTextField* _statusLabel;
    NSButton* _cloneButton;
    NSButton* _deleteButton;
    NSButton* _openRootButton;
    NSTextField* _autoNameField;
    NSPopUpButton* _autoNameKindPopup;
    NSPopUpButton* _autoNameArchPopup;
    NSString* _autoGeneratedName;
    BOOL _autoNameEdited;
    std::vector<prefix::PrefixLayout> _prefixes;
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
    NSString* normalizedType = type.lowercaseString;
    if ([normalizedType isEqualToString:@"wine"])
        normalizedType = @"windows";
    return [NSString stringWithFormat:@"%@-%@",
                                      normalizedType,
                                      CanonicalArchForUI(arch)];
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
    (void)sender;
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
    if ([identifier isEqualToString:@"state"])
        return @"Stopped";
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
    [kindPopup addItemsWithTitles:@[@"android", @"linux", @"windows"]];
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
        std::string kind = StdStringFromNSString(kindPopup.titleOfSelectedItem);
        if (kind == "windows")
            kind = "wine";
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
