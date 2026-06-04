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

@interface MuplarAppShortcut : NSObject
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* path;
@property (nonatomic, copy) NSString* unixPath;
@property (nonatomic, assign) BOOL isManual;
@property (nonatomic, assign) BOOL isLnk;
@end

@implementation MuplarAppShortcut
@end

static NSString* ReadLnkString(std::ifstream& f, uint32_t flags)
{
    uint16_t len = 0;
    if (!f.read((char*)&len, 2)) return nil;
    if (flags & 0x80) { // unicode
        std::vector<uint16_t> buf(len);
        if (!f.read((char*)buf.data(), len * 2)) return nil;
        return [NSString stringWithCharacters:(const unichar*)buf.data() length:len];
    } else { // ascii
        std::vector<char> buf(len);
        if (!f.read(buf.data(), len)) return nil;
        return [[NSString alloc] initWithBytes:buf.data() length:len encoding:NSASCIIStringEncoding];
    }
}

static NSString* ParseLnkFile(const std::filesystem::path& lnkPath)
{
    std::ifstream f(lnkPath, std::ios::binary);
    if (!f) return nil;
    
    char header[76];
    if (!f.read(header, 76)) return nil;
    
    if (*(uint32_t*)header != 0x0000004C) return nil;
    
    uint32_t flags = *(uint32_t*)(header + 20);
    
    if (flags & 0x01) { // HasLinkTargetIDList
        uint16_t idListSize = 0;
        if (!f.read((char*)&idListSize, 2)) return nil;
        f.seekg(idListSize, std::ios::cur);
    }
    
    NSString* localPath = nil;
    if (flags & 0x02) { // HasLinkInfo
        uint32_t linkInfoSize = 0;
        std::streampos linkInfoStart = f.tellg();
        if (!f.read((char*)&linkInfoSize, 4)) return nil;
        
        if (linkInfoSize >= 28) {
            std::vector<char> buf(linkInfoSize);
            *(uint32_t*)buf.data() = linkInfoSize;
            if (f.read(buf.data() + 4, linkInfoSize - 4)) {
                uint32_t localBasePathOffset = *(uint32_t*)(buf.data() + 16);
                if (localBasePathOffset > 0 && localBasePathOffset < linkInfoSize) {
                    localPath = [NSString stringWithUTF8String:(buf.data() + localBasePathOffset)];
                }
            }
        }
        f.seekg(linkInfoStart + (std::streamoff)linkInfoSize);
    }
    
    if (localPath) return localPath;
    
    if (flags & 0x04) ReadLnkString(f, flags);
    if (flags & 0x08) {
        NSString* relPath = ReadLnkString(f, flags);
        if (relPath) return relPath;
    }
    
    return nil;
}

@interface PrefixManagerAppDelegate
    : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
- (void)reloadPrefixesSelectingName:(const std::string&)name;
- (void)configureArchPopup:(NSPopUpButton*)archPopup forType:(NSString*)type;
@end

@implementation PrefixManagerAppDelegate {
    NSWindow* _window;
    NSTableView* _tableView;
    NSTextField* _statusLabel;
    
    NSView* _detailContainer;
    NSTextField* _noSelectionLabel;
    NSTextField* _detailHeaderTitle;
    NSTextField* _detailHeaderInfo;
    NSTextField* _detailHeaderLocation;
    
    NSButton* _cloneButton;
    NSButton* _deleteButton;
    NSButton* _openRootButton;
    NSButton* _addAppButton;
    
    NSTableView* _appsTableView;
    NSMutableArray<MuplarAppShortcut*>* _appsList;
    NSMutableDictionary<NSString*, NSTask*>* _runningTasks;
    NSMutableSet<NSString*>* _launchingAppPaths;

    NSTextField* _autoNameField;
    NSPopUpButton* _autoNameKindPopup;
    NSPopUpButton* _autoNameArchPopup;
    NSString* _autoGeneratedName;
    BOOL _autoNameEdited;
    std::vector<prefix::PrefixLayout> _prefixes;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _appsList = [NSMutableArray array];
        _runningTasks = [NSMutableDictionary dictionary];
        _launchingAppPaths = [NSMutableSet set];
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
    _addAppButton =
        [self toolbarButtonWithTitle:@"Add App" symbol:@"plus.circle" action:@selector(addApplication:)];
    _addAppButton.enabled = NO;

    [toolbar addArrangedSubview:createButton];
    [toolbar addArrangedSubview:_cloneButton];
    [toolbar addArrangedSubview:_deleteButton];
    [toolbar addArrangedSubview:refreshButton];
    [toolbar addArrangedSubview:_openRootButton];
    [toolbar addArrangedSubview:_addAppButton];

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

    // Force toolbar, separator and body to fill root horizontally (full window width)
    [NSLayoutConstraint activateConstraints:@[
        [toolbar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [toolbar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [separator.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [separator.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [body.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [body.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
    ]];

    // Sidebar Pane (Left)
    NSView* sidebar = [[NSView alloc] init];
    sidebar.translatesAutoresizingMaskIntoConstraints = NO;
    [body addSubview:sidebar];

    // Vertical Divider Line
    NSBox* vSeparator = [[NSBox alloc] init];
    vSeparator.boxType = NSBoxSeparator;
    vSeparator.translatesAutoresizingMaskIntoConstraints = NO;
    [body addSubview:vSeparator];

    // Detail Pane (Right)
    NSView* detailPane = [[NSView alloc] init];
    detailPane.translatesAutoresizingMaskIntoConstraints = NO;
    [body addSubview:detailPane];

    [NSLayoutConstraint activateConstraints:@[
        [sidebar.leadingAnchor constraintEqualToAnchor:body.leadingAnchor],
        [sidebar.topAnchor constraintEqualToAnchor:body.topAnchor],
        [sidebar.bottomAnchor constraintEqualToAnchor:body.bottomAnchor],
        [sidebar.widthAnchor constraintEqualToConstant:260.0],

        [vSeparator.leadingAnchor constraintEqualToAnchor:sidebar.trailingAnchor],
        [vSeparator.topAnchor constraintEqualToAnchor:body.topAnchor],
        [vSeparator.bottomAnchor constraintEqualToAnchor:body.bottomAnchor],
        [vSeparator.widthAnchor constraintEqualToConstant:1.0],

        [detailPane.leadingAnchor constraintEqualToAnchor:vSeparator.trailingAnchor],
        [detailPane.trailingAnchor constraintEqualToAnchor:body.trailingAnchor],
        [detailPane.topAnchor constraintEqualToAnchor:body.topAnchor],
        [detailPane.bottomAnchor constraintEqualToAnchor:body.bottomAnchor],
    ]];

    // Sidebar Content
    NSTextField* listTitle = [NSTextField labelWithString:@"Instances"];
    listTitle.font = [NSFont systemFontOfSize:18 weight:NSFontWeightSemibold];
    listTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:listTitle];

    _tableView = [[NSTableView alloc] init];
    _tableView.delegate = self;
    _tableView.dataSource = self;
    _tableView.usesAlternatingRowBackgroundColors = YES;
    _tableView.rowHeight = 48.0;
    _tableView.allowsMultipleSelection = NO;
    _tableView.allowsEmptySelection = NO;
    _tableView.target = self;
    _tableView.action = @selector(tableSelectionAction:);
    _tableView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    _tableView.headerView = nil; // Clean, premium sidebar look without header

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"instance"];
    column.minWidth = 200.0;
    column.resizingMask = NSTableColumnAutoresizingMask | NSTableColumnUserResizingMask;
    [_tableView addTableColumn:column];

    NSScrollView* tableScroll = [[NSScrollView alloc] init];
    tableScroll.documentView = _tableView;
    tableScroll.hasVerticalScroller = YES;
    tableScroll.hasHorizontalScroller = NO;
    tableScroll.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:tableScroll];

    [NSLayoutConstraint activateConstraints:@[
        [listTitle.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor constant:14.0],
        [listTitle.topAnchor constraintEqualToAnchor:sidebar.topAnchor constant:14.0],
        [listTitle.trailingAnchor constraintLessThanOrEqualToAnchor:sidebar.trailingAnchor constant:-14.0],

        [tableScroll.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor constant:14.0],
        [tableScroll.trailingAnchor constraintEqualToAnchor:sidebar.trailingAnchor constant:-14.0],
        [tableScroll.topAnchor constraintEqualToAnchor:listTitle.bottomAnchor constant:10.0],
        [tableScroll.bottomAnchor constraintEqualToAnchor:sidebar.bottomAnchor constant:-14.0],
    ]];

    // Detail Pane Content
    _noSelectionLabel = [NSTextField labelWithString:@"Select an instance to view applications."];
    _noSelectionLabel.font = [NSFont systemFontOfSize:14];
    _noSelectionLabel.textColor = NSColor.secondaryLabelColor;
    _noSelectionLabel.alignment = NSTextAlignmentCenter;
    _noSelectionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [detailPane addSubview:_noSelectionLabel];

    _detailContainer = [[NSView alloc] init];
    _detailContainer.translatesAutoresizingMaskIntoConstraints = NO;
    _detailContainer.hidden = YES;
    [detailPane addSubview:_detailContainer];

    [NSLayoutConstraint activateConstraints:@[
        [_noSelectionLabel.centerXAnchor constraintEqualToAnchor:detailPane.centerXAnchor],
        [_noSelectionLabel.centerYAnchor constraintEqualToAnchor:detailPane.centerYAnchor],

        [_detailContainer.leadingAnchor constraintEqualToAnchor:detailPane.leadingAnchor constant:18.0],
        [_detailContainer.trailingAnchor constraintEqualToAnchor:detailPane.trailingAnchor constant:-18.0],
        [_detailContainer.topAnchor constraintEqualToAnchor:detailPane.topAnchor constant:18.0],
        [_detailContainer.bottomAnchor constraintEqualToAnchor:detailPane.bottomAnchor constant:-18.0],
    ]];

    // Detail Container Subviews
    _detailHeaderTitle = [NSTextField labelWithString:@""];
    _detailHeaderTitle.font = [NSFont systemFontOfSize:22 weight:NSFontWeightBold];
    _detailHeaderTitle.translatesAutoresizingMaskIntoConstraints = NO;

    _detailHeaderInfo = [NSTextField labelWithString:@""];
    _detailHeaderInfo.font = [NSFont systemFontOfSize:13];
    _detailHeaderInfo.textColor = NSColor.secondaryLabelColor;
    _detailHeaderInfo.translatesAutoresizingMaskIntoConstraints = NO;

    _detailHeaderLocation = [NSTextField labelWithString:@""];
    _detailHeaderLocation.font = [NSFont systemFontOfSize:11];
    _detailHeaderLocation.textColor = NSColor.secondaryLabelColor;
    _detailHeaderLocation.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _detailHeaderLocation.translatesAutoresizingMaskIntoConstraints = NO;

    NSBox* headerSeparator = [[NSBox alloc] init];
    headerSeparator.boxType = NSBoxSeparator;
    headerSeparator.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField* appsTitle = [NSTextField labelWithString:@"Applications"];
    appsTitle.font = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    appsTitle.translatesAutoresizingMaskIntoConstraints = NO;

    _appsTableView = [[NSTableView alloc] init];
    _appsTableView.delegate = self;
    _appsTableView.dataSource = self;
    _appsTableView.usesAlternatingRowBackgroundColors = YES;
    _appsTableView.rowHeight = 44.0;
    _appsTableView.allowsMultipleSelection = NO;
    _appsTableView.allowsEmptySelection = YES;
    _appsTableView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;

    NSArray<NSArray<NSString*>*>* appsColumns = @[
        @[@"icon", @"", @"32"],
        @[@"name", @"Name", @"150"],
        @[@"path", @"Path", @"320"],
        @[@"status", @"Status", @"100"],
        @[@"action", @"Actions", @"140"],
    ];
    for (NSArray<NSString*>* spec in appsColumns) {
        NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:spec[0]];
        column.title = spec[1];
        column.width = spec[2].doubleValue;
        column.minWidth = [spec[0] isEqualToString:@"icon"] ? 32.0 : 60.0;
        if ([spec[0] isEqualToString:@"icon"]) {
            column.maxWidth = 32.0;
            column.resizingMask = NSTableColumnNoResizing;
        } else if ([spec[0] isEqualToString:@"action"]) {
            column.resizingMask = NSTableColumnNoResizing;
        } else if ([spec[0] isEqualToString:@"status"]) {
            column.maxWidth = 100.0;
            column.resizingMask = NSTableColumnNoResizing;
        } else if ([spec[0] isEqualToString:@"path"]) {
            column.resizingMask = NSTableColumnAutoresizingMask | NSTableColumnUserResizingMask;
        } else {
            column.resizingMask = NSTableColumnUserResizingMask;
        }
        [_appsTableView addTableColumn:column];
    }

    NSScrollView* appsTableScroll = [[NSScrollView alloc] init];
    appsTableScroll.documentView = _appsTableView;
    appsTableScroll.hasVerticalScroller = YES;
    appsTableScroll.hasHorizontalScroller = YES;
    appsTableScroll.translatesAutoresizingMaskIntoConstraints = NO;

    [_detailContainer addSubview:_detailHeaderTitle];
    [_detailContainer addSubview:_detailHeaderInfo];
    [_detailContainer addSubview:_detailHeaderLocation];
    [_detailContainer addSubview:headerSeparator];
    [_detailContainer addSubview:appsTitle];
    [_detailContainer addSubview:appsTableScroll];

    [NSLayoutConstraint activateConstraints:@[
        [_detailHeaderTitle.leadingAnchor constraintEqualToAnchor:_detailContainer.leadingAnchor],
        [_detailHeaderTitle.trailingAnchor constraintEqualToAnchor:_detailContainer.trailingAnchor],
        [_detailHeaderTitle.topAnchor constraintEqualToAnchor:_detailContainer.topAnchor],

        [_detailHeaderInfo.leadingAnchor constraintEqualToAnchor:_detailContainer.leadingAnchor],
        [_detailHeaderInfo.trailingAnchor constraintEqualToAnchor:_detailContainer.trailingAnchor],
        [_detailHeaderInfo.topAnchor constraintEqualToAnchor:_detailHeaderTitle.bottomAnchor constant:4.0],

        [_detailHeaderLocation.leadingAnchor constraintEqualToAnchor:_detailContainer.leadingAnchor],
        [_detailHeaderLocation.trailingAnchor constraintEqualToAnchor:_detailContainer.trailingAnchor],
        [_detailHeaderLocation.topAnchor constraintEqualToAnchor:_detailHeaderInfo.bottomAnchor constant:4.0],

        [headerSeparator.leadingAnchor constraintEqualToAnchor:_detailContainer.leadingAnchor],
        [headerSeparator.trailingAnchor constraintEqualToAnchor:_detailContainer.trailingAnchor],
        [headerSeparator.topAnchor constraintEqualToAnchor:_detailHeaderLocation.bottomAnchor constant:12.0],
        [headerSeparator.heightAnchor constraintEqualToConstant:1.0],

        [appsTitle.leadingAnchor constraintEqualToAnchor:_detailContainer.leadingAnchor],
        [appsTitle.topAnchor constraintEqualToAnchor:headerSeparator.bottomAnchor constant:12.0],

        [appsTableScroll.leadingAnchor constraintEqualToAnchor:_detailContainer.leadingAnchor],
        [appsTableScroll.trailingAnchor constraintEqualToAnchor:_detailContainer.trailingAnchor],
        [appsTableScroll.topAnchor constraintEqualToAnchor:appsTitle.bottomAnchor constant:8.0],
        [appsTableScroll.bottomAnchor constraintEqualToAnchor:_detailContainer.bottomAnchor],
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
        auto state = prefix::query_prefix_state(item);
        return (state == prefix::PrefixState::Running) ? @"Running" : @"Stopped";
    }
    return @"";
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    if (tableView == _tableView) {
        return static_cast<NSInteger>(_prefixes.size());
    } else if (tableView == _appsTableView) {
        return static_cast<NSInteger>(_appsList.count);
    }
    return 0;
}

- (NSView*)tableView:(NSTableView*)tableView
 viewForTableColumn:(NSTableColumn*)tableColumn
                row:(NSInteger)row
{
    if (tableView == _tableView) {
        NSTableCellView* cell = [tableView makeViewWithIdentifier:@"instance" owner:self];
        if (!cell) {
            cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 230, 48)];
            cell.identifier = @"instance";
            
            NSImageView* imageView = [[NSImageView alloc] init];
            imageView.translatesAutoresizingMaskIntoConstraints = NO;
            imageView.imageScaling = NSImageScaleProportionallyUpOrDown;
            cell.imageView = imageView;
            [cell addSubview:imageView];
            
            NSStackView* textStack = [[NSStackView alloc] init];
            textStack.orientation = NSUserInterfaceLayoutOrientationVertical;
            textStack.alignment = NSLayoutAttributeLeading;
            textStack.spacing = 2.0;
            textStack.translatesAutoresizingMaskIntoConstraints = NO;
            [cell addSubview:textStack];
            
            NSTextField* titleLabel = [NSTextField labelWithString:@""];
            titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
            titleLabel.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
            titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
            cell.textField = titleLabel;
            [textStack addArrangedSubview:titleLabel];
            
            NSTextField* subtitleLabel = [NSTextField labelWithString:@""];
            subtitleLabel.translatesAutoresizingMaskIntoConstraints = NO;
            subtitleLabel.font = [NSFont systemFontOfSize:11];
            subtitleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
            subtitleLabel.tag = 100;
            [textStack addArrangedSubview:subtitleLabel];
            
            [NSLayoutConstraint activateConstraints:@[
                [imageView.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:6],
                [imageView.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                [imageView.widthAnchor constraintEqualToConstant:28.0],
                [imageView.heightAnchor constraintEqualToConstant:28.0],
                
                [textStack.leadingAnchor constraintEqualToAnchor:imageView.trailingAnchor constant:10],
                [textStack.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-6],
                [textStack.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
            ]];
        }
        
        const auto& item = _prefixes[static_cast<size_t>(row)];
        cell.imageView.image = InstancePreviewImage(item);
        cell.textField.stringValue = NSStringFromStdString(item.name);
        
        NSTextField* subtitleLabel = [cell viewWithTag:100];
        if (subtitleLabel) {
            auto state = prefix::query_prefix_state(item);
            NSString* statusStr = (state == prefix::PrefixState::Running) ? @"Running" : @"Stopped";
            NSString* osStr = RuntimeDisplayName(item);
            
            NSMutableAttributedString* attrStr = [[NSMutableAttributedString alloc] initWithString:[NSString stringWithFormat:@"%@  •  %@", osStr, statusStr]];
            NSRange statusRange = [attrStr.string rangeOfString:statusStr];
            if (statusRange.location != NSNotFound) {
                if (state == prefix::PrefixState::Running) {
                    [attrStr addAttribute:NSForegroundColorAttributeName
                                    value:[NSColor colorWithSRGBRed:0.15 green:0.67 blue:0.24 alpha:1.0]
                                    range:statusRange];
                    [attrStr addAttribute:NSFontAttributeName
                                    value:[NSFont systemFontOfSize:11 weight:NSFontWeightMedium]
                                    range:statusRange];
                } else {
                    [attrStr addAttribute:NSForegroundColorAttributeName
                                    value:[NSColor secondaryLabelColor]
                                    range:statusRange];
                }
            }
            
            NSRange baseRange = NSMakeRange(0, attrStr.length);
            if (statusRange.location != NSNotFound) {
                [attrStr addAttribute:NSForegroundColorAttributeName value:[NSColor secondaryLabelColor] range:NSMakeRange(0, statusRange.location)];
            } else {
                [attrStr addAttribute:NSForegroundColorAttributeName value:[NSColor secondaryLabelColor] range:baseRange];
            }
            
            subtitleLabel.attributedStringValue = attrStr;
        }
        
        return cell;
    }

    if (tableView == _appsTableView) {
        MuplarAppShortcut* app = _appsList[row];

        if ([tableColumn.identifier isEqualToString:@"icon"]) {
            NSTableCellView* cell = [tableView makeViewWithIdentifier:@"icon" owner:self];
            if (!cell) {
                cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 32, 44)];
                cell.identifier = @"icon";
                NSImageView* imageView = [[NSImageView alloc] init];
                imageView.translatesAutoresizingMaskIntoConstraints = NO;
                imageView.imageScaling = NSImageScaleProportionallyUpOrDown;
                cell.imageView = imageView;
                [cell addSubview:imageView];
                [NSLayoutConstraint activateConstraints:@[
                    [imageView.centerXAnchor constraintEqualToAnchor:cell.centerXAnchor],
                    [imageView.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                    [imageView.widthAnchor constraintEqualToConstant:20.0],
                    [imageView.heightAnchor constraintEqualToConstant:20.0],
                ]];
            }
            NSString* iconName = @"cpu";
            if (app.isLnk) {
                iconName = @"link";
            } else if ([app.path isEqualToString:@"explorer"]) {
                iconName = @"macwindow";
            } else if ([app.path isEqualToString:@"winecfg"]) {
                iconName = @"gearshape";
            } else if ([app.path isEqualToString:@"regedit"]) {
                iconName = @"slider.horizontal.3";
            } else if ([app.path isEqualToString:@"cmd"]) {
                iconName = @"terminal";
            } else if ([app.path isEqualToString:@"control"]) {
                iconName = @"slider.horizontal.below.rectangle";
            } else if ([app.path isEqualToString:@"taskmgr"]) {
                iconName = @"chart.bar";
            }
            cell.imageView.image = [NSImage imageWithSystemSymbolName:iconName accessibilityDescription:nil];
            return cell;
        }

        if ([tableColumn.identifier isEqualToString:@"name"]) {
            NSTableCellView* cell = [tableView makeViewWithIdentifier:@"name" owner:self];
            if (!cell) {
                cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 150, 44)];
                cell.identifier = @"name";
                NSTextField* text = [NSTextField labelWithString:@""];
                text.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
                text.translatesAutoresizingMaskIntoConstraints = NO;
                text.lineBreakMode = NSLineBreakByTruncatingTail;
                cell.textField = text;
                [cell addSubview:text];
                [NSLayoutConstraint activateConstraints:@[
                    [text.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:8],
                    [text.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-8],
                    [text.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                ]];
            }
            cell.textField.stringValue = app.name;
            return cell;
        }

        if ([tableColumn.identifier isEqualToString:@"path"]) {
            NSTableCellView* cell = [tableView makeViewWithIdentifier:@"path" owner:self];
            if (!cell) {
                cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 320, 44)];
                cell.identifier = @"path";
                NSTextField* text = [NSTextField labelWithString:@""];
                text.textColor = NSColor.secondaryLabelColor;
                text.font = [NSFont systemFontOfSize:11];
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
            cell.textField.stringValue = app.path;
            return cell;
        }

        if ([tableColumn.identifier isEqualToString:@"status"]) {
            NSTableCellView* cell = [tableView makeViewWithIdentifier:@"status" owner:self];
            if (!cell) {
                cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 100, 44)];
                cell.identifier = @"status";
                
                NSTextField* text = [NSTextField labelWithString:@""];
                text.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
                text.translatesAutoresizingMaskIntoConstraints = NO;
                text.lineBreakMode = NSLineBreakByTruncatingTail;
                cell.textField = text;
                [cell addSubview:text];
                
                NSProgressIndicator* spinner = [[NSProgressIndicator alloc] init];
                spinner.style = NSProgressIndicatorStyleSpinning;
                spinner.controlSize = NSControlSizeSmall;
                spinner.displayedWhenStopped = NO;
                spinner.translatesAutoresizingMaskIntoConstraints = NO;
                [cell addSubview:spinner];
                
                [NSLayoutConstraint activateConstraints:@[
                    [text.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:8],
                    [text.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                    
                    [spinner.leadingAnchor constraintEqualToAnchor:text.trailingAnchor constant:6],
                    [spinner.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                    [spinner.widthAnchor constraintEqualToConstant:14.0],
                    [spinner.heightAnchor constraintEqualToConstant:14.0],
                ]];
            }
            
            NSString* status = [self statusForApp:app];
            NSProgressIndicator* spinner = nil;
            for (NSView* subview in cell.subviews) {
                if ([subview isKindOfClass:NSProgressIndicator.class]) {
                    spinner = (NSProgressIndicator*)subview;
                    break;
                }
            }
            
            if ([status isEqualToString:@"Launching..."]) {
                cell.textField.stringValue = @"Launching";
                cell.textField.textColor = [NSColor systemOrangeColor];
                if (spinner) {
                    [spinner startAnimation:nil];
                    spinner.hidden = NO;
                }
            } else if ([status isEqualToString:@"Running"]) {
                cell.textField.stringValue = @"Running";
                cell.textField.textColor = [NSColor colorWithSRGBRed:0.15 green:0.67 blue:0.24 alpha:1.0];
                if (spinner) {
                    [spinner stopAnimation:nil];
                    spinner.hidden = YES;
                }
            } else {
                cell.textField.stringValue = @"Stopped";
                cell.textField.textColor = [NSColor secondaryLabelColor];
                if (spinner) {
                    [spinner stopAnimation:nil];
                    spinner.hidden = YES;
                }
            }
            return cell;
        }

        if ([tableColumn.identifier isEqualToString:@"action"]) {
            NSTableCellView* cell = [tableView makeViewWithIdentifier:@"action" owner:self];
            if (!cell) {
                cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 140, 44)];
                cell.identifier = @"action";

                NSButton* launchBtn = [NSButton buttonWithTitle:@"Launch" target:self action:@selector(launchAppClicked:)];
                launchBtn.bezelStyle = NSBezelStyleRounded;
                launchBtn.translatesAutoresizingMaskIntoConstraints = NO;
                [cell addSubview:launchBtn];

                NSButton* removeBtn = [NSButton buttonWithTitle:@"" image:[NSImage imageWithSystemSymbolName:@"trash" accessibilityDescription:nil] target:self action:@selector(removeAppClicked:)];
                removeBtn.bezelStyle = NSBezelStyleRounded;
                removeBtn.translatesAutoresizingMaskIntoConstraints = NO;
                [cell addSubview:removeBtn];

                [NSLayoutConstraint activateConstraints:@[
                    [launchBtn.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
                    [launchBtn.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                    [launchBtn.widthAnchor constraintEqualToConstant:75],

                    [removeBtn.leadingAnchor constraintEqualToAnchor:launchBtn.trailingAnchor constant:8],
                    [removeBtn.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
                    [removeBtn.widthAnchor constraintEqualToConstant:32],
                    [removeBtn.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
                ]];
            }

            NSButton* launchBtn = nil;
            NSButton* removeBtn = nil;
            for (NSView* subview in cell.subviews) {
                if ([subview isKindOfClass:NSButton.class]) {
                    NSButton* btn = (NSButton*)subview;
                    btn.tag = row;
                    if (btn.action == @selector(removeAppClicked:)) {
                        removeBtn = btn;
                    } else if (btn.action == @selector(launchAppClicked:)) {
                        launchBtn = btn;
                    }
                }
            }

            if (removeBtn) {
                removeBtn.hidden = !app.isManual;
            }

            if (launchBtn) {
                NSString* status = [self statusForApp:app];
                if ([status isEqualToString:@"Launching..."]) {
                    launchBtn.enabled = NO;
                    launchBtn.title = @"Launch";
                } else if ([status isEqualToString:@"Running"]) {
                    launchBtn.enabled = YES;
                    launchBtn.title = @"Stop";
                } else {
                    launchBtn.enabled = YES;
                    launchBtn.title = @"Launch";
                }
            }

            return cell;
        }
    }
    return nil;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    [self updateSelectionState];
    [self loadAppsForSelectedPrefix];
}

- (void)tableSelectionAction:(id)sender
{
    (void)sender;
    [self updateSelectionState];
    [self loadAppsForSelectedPrefix];
}

- (void)updateSelectionState
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    BOOL enabled = selected != nullptr;
    _cloneButton.enabled = enabled;
    _deleteButton.enabled = enabled;
    _openRootButton.enabled = enabled;
    _addAppButton.enabled = enabled;
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
        [self loadAppsForSelectedPrefix];
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

- (NSString*)unixToWindowsPath:(NSString*)unixPath forPrefix:(prefix::PrefixLayout*)selected
{
    NSString* driveC = [NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"];
    if ([unixPath hasPrefix:driveC]) {
        NSString* rel = [unixPath substringFromIndex:driveC.length];
        if ([rel hasPrefix:@"/"]) rel = [rel substringFromIndex:1];
        NSString* win = [@"C:\\" stringByAppendingString:[rel stringByReplacingOccurrencesOfString:@"/" withString:@"\\"]];
        return win;
    }
    return unixPath;
}

- (void)addShortcutWithName:(NSString*)name
                       path:(NSString*)path
                   unixPath:(NSString*)unixPath
                   isManual:(BOOL)isManual
                      isLnk:(BOOL)isLnk
{
    NSString* normalizedPath = [path stringByReplacingOccurrencesOfString:@"/" withString:@"\\"];
    
    for (MuplarAppShortcut* existing in _appsList) {
        if ([existing.path caseInsensitiveCompare:normalizedPath] == NSOrderedSame) {
            return;
        }
    }
    
    MuplarAppShortcut* app = [[MuplarAppShortcut alloc] init];
    app.name = name;
    app.path = normalizedPath;
    app.unixPath = unixPath;
    app.isManual = isManual;
    app.isLnk = isLnk;
    [_appsList addObject:app];
}

- (void)loadManualApps:(prefix::PrefixLayout*)selected
{
    NSString* path = [NSStringFromPath(selected->root) stringByAppendingPathComponent:@"muplar_apps.json"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) return;
    
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data) return;
    
    NSError* error = nil;
    NSArray* list = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (error || ![list isKindOfClass:NSArray.class]) return;
    
    for (NSDictionary* dict in list) {
        if ([dict isKindOfClass:NSDictionary.class]) {
            NSString* name = dict[@"name"];
            NSString* relPath = dict[@"path"];
            if (name && relPath) {
                NSString* winPath = [@"C:\\" stringByAppendingString:[relPath stringByReplacingOccurrencesOfString:@"/" withString:@"\\"]];
                NSString* unixPath = [[NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"] stringByAppendingPathComponent:relPath];
                [self addShortcutWithName:name path:winPath unixPath:unixPath isManual:YES isLnk:NO];
            }
        }
    }
}

- (void)saveManualApps:(prefix::PrefixLayout*)selected
{
    NSString* jsonPath = [NSStringFromPath(selected->root) stringByAppendingPathComponent:@"muplar_apps.json"];
    NSMutableArray* list = [NSMutableArray array];
    
    for (MuplarAppShortcut* app in _appsList) {
        if (app.isManual) {
            NSString* relPath = app.path;
            if ([relPath hasPrefix:@"C:\\"] || [relPath hasPrefix:@"c:\\"]) {
                relPath = [relPath substringFromIndex:3];
            }
            relPath = [relPath stringByReplacingOccurrencesOfString:@"\\" withString:@"/"];
            [list addObject:@{
                @"name": app.name,
                @"path": relPath
            }];
        }
    }
    
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:list options:NSJSONWritingPrettyPrinted error:&error];
    if (data && !error) {
        [data writeToFile:jsonPath atomically:YES];
    }
}

- (void)removeManualApp:(MuplarAppShortcut*)app
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) return;
    
    [_appsList removeObject:app];
    [self saveManualApps:selected];
    [_appsTableView reloadData];
}

- (void)scanWineApps:(prefix::PrefixLayout*)selected
{
    std::filesystem::path driveC = selected->rootfs / "drive_c";
    if (!PathExists(driveC)) return;

    [self addShortcutWithName:@"File Explorer" path:@"explorer" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"System Configuration" path:@"winecfg" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Registry Editor" path:@"regedit" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Command Prompt" path:@"cmd" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Control Panel" path:@"control" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Task Manager" path:@"taskmgr" unixPath:@"" isManual:NO isLnk:NO];

    [self loadManualApps:selected];

    std::filesystem::path usersDir = driveC / "users";
    if (PathExists(usersDir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(usersDir, ec)) {
            if (entry.is_directory()) {
                std::filesystem::path desktop = entry.path() / "Desktop";
                if (PathExists(desktop)) {
                    [self scanDirForShortcuts:desktop isLnk:YES];
                }
            }
        }
    }

    std::filesystem::path startMenu = driveC / "ProgramData" / "Microsoft" / "Windows" / "Start Menu" / "Programs";
    if (PathExists(startMenu)) {
        [self scanDirForShortcuts:startMenu isLnk:YES];
    }
}

- (void)scanLinuxApps:(prefix::PrefixLayout*)selected
{
    std::filesystem::path appsDir = selected->rootfs / "usr" / "share" / "applications";
    if (PathExists(appsDir)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(appsDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".desktop") {
                NSString* name = NSStringFromPath(entry.path().stem());
                NSString* unixPath = NSStringFromPath(entry.path());
                [self addShortcutWithName:name path:name unixPath:unixPath isManual:NO isLnk:NO];
            }
        }
    }
}

- (void)scanAndroidApps:(prefix::PrefixLayout*)selected
{
    (void)selected;
    [self addShortcutWithName:@"Android Settings" path:@"com.android.settings" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Browser" path:@"com.android.browser" unixPath:@"" isManual:NO isLnk:NO];
}

- (void)scanDirForShortcuts:(const std::filesystem::path&)dir isLnk:(BOOL)isLnk
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == ".lnk") {
                NSString* unixPath = NSStringFromPath(entry.path());
                NSString* targetPath = [self resolveLnkTarget:entry.path()];
                NSString* name = [NSStringFromPath(entry.path().stem()) stringByDeletingPathExtension];
                
                if (targetPath && targetPath.length > 0) {
                    [self addShortcutWithName:name path:targetPath unixPath:unixPath isManual:NO isLnk:YES];
                }
            } else if (ext == ".exe" && !isLnk) {
                NSString* unixPath = NSStringFromPath(entry.path());
                NSString* name = NSStringFromPath(entry.path().stem());
                NSString* winPath = [self unixToWindowsPath:unixPath forPrefix:[self selectedPrefix]];
                [self addShortcutWithName:name path:winPath unixPath:unixPath isManual:NO isLnk:NO];
            }
        }
    }
}

- (NSString*)resolveLnkTarget:(const std::filesystem::path&)path
{
    @try {
        return ParseLnkFile(path);
    } @catch (...) {
        return nil;
    }
}

- (void)loadAppsForSelectedPrefix
{
    [_appsList removeAllObjects];
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) {
        _detailContainer.hidden = YES;
        _noSelectionLabel.hidden = NO;
        [_appsTableView reloadData];
        return;
    }

    _detailContainer.hidden = NO;
    _noSelectionLabel.hidden = YES;

    _detailHeaderTitle.stringValue = NSStringFromStdString(selected->name);
    auto state = prefix::query_prefix_state(*selected);
    NSString* statusStr = (state == prefix::PrefixState::Running) ? @"Running" : @"Stopped";
    _detailHeaderInfo.stringValue = [NSString stringWithFormat:@"OS: %@ | Status: %@",
                                     RuntimeDisplayName(*selected),
                                     statusStr];
    _detailHeaderLocation.stringValue = [NSString stringWithFormat:@"Location: %@", NSStringFromPath(selected->root)];

    if (selected->kind == prefix::PrefixKind::Wine) {
        [self scanWineApps:selected];
    } else if (selected->kind == prefix::PrefixKind::Linux) {
        [self scanLinuxApps:selected];
    } else {
        [self scanAndroidApps:selected];
    }

    [_appsTableView reloadData];
}

- (void)addApplication:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) return;
    
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[@"exe"];
    panel.prompt = @"Add";
    panel.title = @"Select Windows Executable (.exe)";
    
    NSString* driveC = [NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"];
    panel.directoryURL = [NSURL fileURLWithPath:driveC];
    
    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        NSString* unixPath = panel.URL.path;
        if (![unixPath hasPrefix:driveC]) {
            [self showError:@"Executable must be inside the instance C: drive."];
            return;
        }
        
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Add Application";
        alert.informativeText = @"Enter a friendly name for this application shortcut:";
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        
        NSString* defaultName = [[unixPath lastPathComponent] stringByDeletingPathExtension];
        NSTextField* input = [NSTextField textFieldWithString:defaultName];
        alert.accessoryView = [self dialogAccessoryWithGrid:[NSGridView gridViewWithViews:@[@[[self label:@"Name"], input]]] width:300 height:24];
        alert.window.initialFirstResponder = input;
        
        if ([alert runModal] == NSAlertFirstButtonReturn) {
            NSString* name = input.stringValue;
            if (name.length == 0) name = defaultName;
            
            NSString* winPath = [self unixToWindowsPath:unixPath forPrefix:selected];
            [self addShortcutWithName:name path:winPath unixPath:unixPath isManual:YES isLnk:NO];
            [self saveManualApps:selected];
            [_appsTableView reloadData];
        }
    }
}

- (NSString*)appKeyForApp:(MuplarAppShortcut*)app
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) return app.path;
    return [NSString stringWithFormat:@"%s:%@", selected->name.c_str(), app.path];
}

- (NSString*)statusForApp:(MuplarAppShortcut*)app
{
    NSString* key = [self appKeyForApp:app];
    if ([_launchingAppPaths containsObject:key]) {
        return @"Launching...";
    }
    NSTask* task = _runningTasks[key];
    if (task && task.isRunning) {
        return @"Running";
    }
    return @"Stopped";
}

- (void)launchShortcut:(MuplarAppShortcut*)app
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) return;

    if (selected->kind == prefix::PrefixKind::Wine) {
        [self launchWineApp:app prefix:selected];
    } else {
        [self showError:[NSString stringWithFormat:@"Launching for %@ instances is not supported yet.", RuntimeDisplayName(*selected)]];
    }
}

- (void)launchWineApp:(MuplarAppShortcut*)app prefix:(prefix::PrefixLayout*)selected
{
    NSString* wineBin = [self wine64PathForPrefix:*selected];
    if (!wineBin) {
        [self showError:@"wine binary not found. Build Wine first or set the prefix sysroot."];
        return;
    }

    NSString* winePrefix = NSStringFromPath(selected->rootfs);

    NSMutableDictionary* env = [NSProcessInfo.processInfo.environment mutableCopy];
    env[@"WINEPREFIX"] = winePrefix;
    
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

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = wineBin;
    
    if (app.isLnk) {
        task.arguments = @[@"start", app.path];
    } else {
        task.arguments = @[app.path];
    }
    
    task.environment = env;
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];

    NSString* key = [self appKeyForApp:app];
    [_launchingAppPaths addObject:key];
    [_appsTableView reloadData];

    std::filesystem::path pidPath = prefix::pid_file_path(*selected);
    NSString* pidFile = NSStringFromPath(pidPath);
    [[NSFileManager defaultManager] createDirectoryAtPath:[pidFile stringByDeletingLastPathComponent]
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];

    task.terminationHandler = ^(NSTask* __unused t) {
        [[NSFileManager defaultManager] removeItemAtPath:pidFile error:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_launchingAppPaths removeObject:key];
            [self->_runningTasks removeObjectForKey:key];
            [self loadAppsForSelectedPrefix];
            [self reloadPrefixes:nil];
        });
    };

    @try {
        NSLog(@"[wine] launching app: %@", app.name);
        [task launch];
        
        _runningTasks[key] = task;

        // Write PID file
        NSString* pidContent = [NSString stringWithFormat:@"%d\n", task.processIdentifier];
        [pidContent writeToFile:pidFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
        
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1500 * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{
            [self->_launchingAppPaths removeObject:key];
            [self loadAppsForSelectedPrefix];
            [self reloadPrefixes:nil];
        });
    } @catch (NSException* ex) {
        [self->_launchingAppPaths removeObject:key];
        [_appsTableView reloadData];
        [self showError:[NSString stringWithFormat:@"Failed to launch app: %@", ex.reason]];
    }
}

- (void)launchAppClicked:(id)sender
{
    NSInteger row = ((NSControl*)sender).tag;
    if (row < 0 || row >= (NSInteger)_appsList.count) return;
    MuplarAppShortcut* app = _appsList[row];
    
    NSString* status = [self statusForApp:app];
    if ([status isEqualToString:@"Running"]) {
        NSString* key = [self appKeyForApp:app];
        NSTask* task = _runningTasks[key];
        if (task && task.isRunning) {
            [task terminate];
        }
    } else if ([status isEqualToString:@"Stopped"]) {
        [self launchShortcut:app];
    }
}

- (void)removeAppClicked:(id)sender
{
    NSInteger row = ((NSControl*)sender).tag;
    if (row < 0 || row >= (NSInteger)_appsList.count) return;
    MuplarAppShortcut* app = _appsList[row];
    [self removeManualApp:app];
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
