#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "apk_envelope.h"
#include <dispatch/dispatch.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "prefix.h"

extern "C" {
#include "debug/log.h"
}
#include "distro_profile.h"
#include "muplard_client.h"
#include "supervisor_service.h"
#import "AndroidDeviceShell.h"
#import "WWNCompositorBridge.h"
#import "WWNWindow.h"

namespace prefix = muplar::runtime::prefix;
namespace services = muplar::services;
namespace supervisor = muplar::supervisor;

static NSString* NSStringFromStdString(const std::string& value)
{
    return [NSString stringWithUTF8String:value.c_str()];
}

static NSString* NSStringFromPath(const std::filesystem::path& path)
{
    return path.empty() ? @"-" : NSStringFromStdString(path.string());
}

static NSString* ShellSingleQuote(NSString* value)
{
    NSString* safeValue = value ?: @"";
    NSString* escaped =
        [safeValue stringByReplacingOccurrencesOfString:@"'"
                                             withString:@"'\\''"];
    return [NSString stringWithFormat:@"'%@'", escaped];
}

static NSString* HostDisplayEnvironmentExportScript()
{
    NSDictionary<NSString*, NSString*>* env =
        NSProcessInfo.processInfo.environment;
    NSArray<NSString*>* names = @[
        @"WAYLAND_DISPLAY",
        @"XDG_RUNTIME_DIR",
        @"DBUS_SESSION_BUS_ADDRESS",
        @"XDG_SESSION_TYPE",
        @"GDK_BACKEND",
        @"QT_QPA_PLATFORM",
        @"SDL_VIDEODRIVER",
        @"CLUTTER_BACKEND",
        @"EGL_PLATFORM",
        @"LIBGL_ALWAYS_INDIRECT",
    ];

    NSMutableString* script = [NSMutableString string];
    for (NSString* name in names) {
        NSString* value = env[name];
        if (value.length == 0)
            continue;
        [script appendFormat:@"export %@=%@\n",
                             name,
                             ShellSingleQuote(value)];
    }
    return script;
}

static NSString* DefaultMuplarWaylandRuntimeDir()
{
    return [NSString stringWithFormat:@"/tmp/muplar-wayland-%lu",
                                      static_cast<unsigned long>(getuid())];
}

static NSString* DefaultMuplarWaylandDisplayName()
{
    return @"wayland-0";
}

static BOOL UnixSocketAcceptsConnection(NSString* path)
{
    if (path.length == 0)
        return NO;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return NO;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    const char* socketPath = path.fileSystemRepresentation;
    size_t socketPathLen = strlen(socketPath);
    if (socketPathLen >= sizeof(addr.sun_path)) {
        close(fd);
        return NO;
    }
    memcpy(addr.sun_path, socketPath, socketPathLen + 1);

    BOOL ok = connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                      sizeof(addr)) == 0;
    close(fd);
    return ok;
}

static NSSet<NSNumber*>* VisibleMuplarWaylandWindowNumbers()
{
    NSMutableSet<NSNumber*>* windowNumbers = [NSMutableSet set];
    for (NSWindow* window in NSApp.windows) {
        if ([window isKindOfClass:WWNWindow.class] && window.isVisible &&
            !window.isMiniaturized && window.windowNumber > 0) {
            [windowNumbers addObject:@(window.windowNumber)];
        }
    }
    return windowNumbers;
}

static void EnsureWineSocketDirPrivate()
{
    NSString* path = [NSString stringWithFormat:@"/tmp/.wine-%lu",
                                                static_cast<unsigned long>(getuid())];
    NSFileManager* fm = NSFileManager.defaultManager;
    [fm createDirectoryAtPath:path
  withIntermediateDirectories:YES
                   attributes:nil
                        error:nil];
    chmod(path.UTF8String, 0700);
}

static void StopAndroidMuplard(const prefix::PrefixLayout& layout)
{
    std::filesystem::path runDir = layout.root / "run";
    std::filesystem::path pidFile = runDir / "muplard.pid";
    std::filesystem::path socketPath = runDir / "muplard.sock";
    if (std::filesystem::is_regular_file(pidFile)) {
        std::ifstream ifs(pidFile);
        pid_t pid = 0;
        if (ifs >> pid && pid > 1) {
            kill(pid, SIGTERM);
        }
    }
    std::error_code ec;
    std::filesystem::remove(socketPath, ec);
    std::filesystem::remove(pidFile, ec);
}

static NSString* SanitizeWindowsCompatibilityLogText(NSString* text)
{
    if (!text)
        return nil;

    NSMutableString* sanitized = [text mutableCopy];
    NSArray<NSArray<NSString*>*>* replacements = @[
        @[@"WINEPREFIX", @"MUPLAR_WINDOWS_ROOT"],
        @[@"WINEDEBUG", @"MUPLAR_WINDOWS_DEBUG"],
        @[@"wineserver", @"Muplar Windows Compatibility service"],
        @[@"wineboot", @"Muplar Windows Compatibility setup"],
        @[@"wine64", @"Muplar Windows Compatibility runtime"],
        @[@".wine-", @".muplar-windows-compat-"],
        @[@"wine", @"Muplar Windows Compatibility"],
    ];
    for (NSArray<NSString*>* pair in replacements) {
        NSString* needle = pair[0];
        NSString* replacement = pair[1];
        [sanitized replaceOccurrencesOfString:needle
                                   withString:replacement
                                      options:NSCaseInsensitiveSearch
                                        range:NSMakeRange(0, sanitized.length)];
    }
    return sanitized;
}

static NSData* SanitizeWindowsCompatibilityLogData(NSData* data)
{
    NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    NSString* sanitized = SanitizeWindowsCompatibilityLogText(text);
    return sanitized ? [sanitized dataUsingEncoding:NSUTF8StringEncoding] : data;
}

static constexpr unsigned long long kMaxLogFileBytes = 5ULL * 1024ULL * 1024ULL;
static constexpr NSInteger kMaxLogHistoryFiles = 5;

static unsigned long long LogFileSize(NSString* path)
{
    NSDictionary<NSFileAttributeKey, id>* attributes =
        [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];
    NSNumber* size = attributes[NSFileSize];
    return size ? size.unsignedLongLongValue : 0;
}

static void RotateLogFileIfNeeded(NSString* logPath)
{
    NSFileManager* fm = NSFileManager.defaultManager;
    if (LogFileSize(logPath) < kMaxLogFileBytes)
        return;

    NSString* oldest =
        [NSString stringWithFormat:@"%@.%ld", logPath, (long)kMaxLogHistoryFiles];
    [fm removeItemAtPath:oldest error:nil];

    for (NSInteger index = kMaxLogHistoryFiles - 1; index >= 1; --index) {
        NSString* from = [NSString stringWithFormat:@"%@.%ld", logPath, (long)index];
        NSString* to = [NSString stringWithFormat:@"%@.%ld", logPath, (long)(index + 1)];
        if ([fm fileExistsAtPath:from])
            [fm moveItemAtPath:from toPath:to error:nil];
    }

    NSString* first = [logPath stringByAppendingString:@".1"];
    if ([fm fileExistsAtPath:logPath])
        [fm moveItemAtPath:logPath toPath:first error:nil];
    [fm createFileAtPath:logPath contents:[NSData data] attributes:nil];
}

static NSFileHandle* OpenRotatingLogForAppend(NSString* logPath)
{
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* logDir = [logPath stringByDeletingLastPathComponent];
    [fm createDirectoryAtPath:logDir
  withIntermediateDirectories:YES
                   attributes:nil
                        error:nil];
    RotateLogFileIfNeeded(logPath);
    if (![fm fileExistsAtPath:logPath])
        [fm createFileAtPath:logPath contents:[NSData data] attributes:nil];

    NSFileHandle* handle = [NSFileHandle fileHandleForWritingAtPath:logPath];
    [handle seekToEndOfFile];
    return handle;
}

static NSData* LogRolloverMarkerData()
{
    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    NSString* timestamp = [formatter stringFromDate:[NSDate date]];
    NSString* marker =
        [NSString stringWithFormat:@"\n=== [%@] Log continued after rollover ===\n",
                                   timestamp];
    return [marker dataUsingEncoding:NSUTF8StringEncoding];
}

static void ApplyDefaultLinuxDisplayEnvironment(
    NSMutableDictionary<NSString*, NSString*>* env)
{
    if ((env[@"XDG_RUNTIME_DIR"] ?: @"").length == 0)
        env[@"XDG_RUNTIME_DIR"] = DefaultMuplarWaylandRuntimeDir();
    if ((env[@"WAYLAND_DISPLAY"] ?: @"").length == 0)
        env[@"WAYLAND_DISPLAY"] = DefaultMuplarWaylandDisplayName();
    [env removeObjectForKey:@"DISPLAY"];
    [env removeObjectForKey:@"XAUTHORITY"];
    if ((env[@"XDG_SESSION_TYPE"] ?: @"").length == 0)
        env[@"XDG_SESSION_TYPE"] = @"wayland";
    if ((env[@"GDK_BACKEND"] ?: @"").length == 0)
        env[@"GDK_BACKEND"] = @"wayland,x11";
    if ((env[@"QT_QPA_PLATFORM"] ?: @"").length == 0)
        env[@"QT_QPA_PLATFORM"] = @"wayland;xcb";
    if ((env[@"SDL_VIDEODRIVER"] ?: @"").length == 0)
        env[@"SDL_VIDEODRIVER"] = @"wayland";
    if ((env[@"CLUTTER_BACKEND"] ?: @"").length == 0)
        env[@"CLUTTER_BACKEND"] = @"wayland";
    if ((env[@"EGL_PLATFORM"] ?: @"").length == 0)
        env[@"EGL_PLATFORM"] = @"wayland";
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

static std::filesystem::path HostPathForGuestPath(
    const std::filesystem::path& rootfs,
    NSString* guestPath)
{
    std::string relative = StdStringFromNSString(guestPath);
    while (!relative.empty() && relative.front() == '/')
        relative.erase(relative.begin());
    return rootfs / relative;
}

static bool PathExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static NSString* FindWorkspaceRoot()
{
    NSString* path = [[NSBundle mainBundle] bundlePath];
    for (int i = 0; i < 5; ++i) {
        path = [path stringByDeletingLastPathComponent];
        if (path.length <= 1) break;

        NSString* cmakeFile = [path stringByAppendingPathComponent:@"CMakeLists.txt"];
        NSString* sysrootFolder = [path stringByAppendingPathComponent:@"build/sysroot"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:cmakeFile] ||
            [[NSFileManager defaultManager] fileExistsAtPath:sysrootFolder]) {
            return path;
        }
    }
    return nil;
}

static NSString* ResolveWorkspaceRelativePath(NSString* path)
{
    if (!path || path.length == 0) return path;

    BOOL isDistorted = NO;
    if ([path isEqualToString:@"/build/sysroot"] || [path hasPrefix:@"/build/sysroot/"]) {
        if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
            isDistorted = YES;
        }
    }

    if ([path isAbsolutePath] && !isDistorted) {
        return path;
    }

    NSString* workspaceRoot = FindWorkspaceRoot();
    if (!workspaceRoot) {
        NSString* parentDir = [[NSBundle mainBundle] bundlePath].stringByDeletingLastPathComponent;
        if (isDistorted) {
            return [[parentDir stringByDeletingLastPathComponent] stringByAppendingPathComponent:[path substringFromIndex:1]];
        }
        return [[parentDir stringByAppendingPathComponent:path] stringByStandardizingPath];
    }

    if (isDistorted) {
        NSString* rel = [path substringFromIndex:1];
        return [[workspaceRoot stringByAppendingPathComponent:rel] stringByStandardizingPath];
    }

    return [[workspaceRoot stringByAppendingPathComponent:path] stringByStandardizingPath];
}

static NSString* DefaultAndroidArtSysrootPath()
{
    NSString* home = NSHomeDirectory();
    return [[[[home stringByAppendingPathComponent:@".muplar"]
        stringByAppendingPathComponent:@"sysroots/android-arm64"]
        stringByAppendingPathComponent:@"api-35"]
        stringByAppendingPathComponent:@"sysroot"];
}

static NSString* DefaultAndroidArtSysrootArchivePath()
{
    NSString* home = NSHomeDirectory();
    return [[[[home stringByAppendingPathComponent:@".muplar"]
        stringByAppendingPathComponent:@"cache"]
        stringByAppendingPathComponent:@"android-sysroot"]
        stringByAppendingPathComponent:@"android-arm64-api35-sysroot.tar.gz"];
}

static NSString* BundledOrWorkspaceTool(NSString* name)
{
    NSString* bundled = [[[[NSBundle mainBundle] resourcePath]
        stringByAppendingPathComponent:@"tools"]
        stringByAppendingPathComponent:name];
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:bundled])
        return bundled;

    NSString* workspaceRoot = FindWorkspaceRoot();
    if (workspaceRoot) {
        NSString* dev = [[workspaceRoot stringByAppendingPathComponent:@"tools"]
            stringByAppendingPathComponent:name];
        if ([[NSFileManager defaultManager] isExecutableFileAtPath:dev])
            return dev;
    }
    return nil;
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
        if (!layout.distro.empty()) {
            std::string d = layout.distro;
            if (d == "opensuse") {
                kind = @"openSUSE";
            } else {
                d[0] = std::toupper(d[0]);
                kind = NSStringFromStdString(d);
            }
        } else {
            kind = @"Linux";
        }
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

        if (layout.distro == "alpine") {
            shell = [NSColor colorWithCalibratedRed:0.06 green:0.25 blue:0.47 alpha:1.0];
            prompt = [NSColor colorWithCalibratedRed:0.0 green:0.75 blue:1.0 alpha:1.0];
        } else if (layout.distro == "ubuntu") {
            shell = [NSColor colorWithCalibratedRed:0.91 green:0.33 blue:0.13 alpha:1.0];
            prompt = [NSColor whiteColor];
        } else if (layout.distro == "debian") {
            shell = [NSColor colorWithCalibratedRed:0.84 green:0.04 blue:0.33 alpha:1.0];
            prompt = [NSColor whiteColor];
        } else if (layout.distro == "fedora") {
            shell = [NSColor colorWithCalibratedRed:0.24 green:0.43 blue:0.71 alpha:1.0];
            prompt = [NSColor whiteColor];
        } else if (layout.distro == "arch") {
            shell = [NSColor colorWithCalibratedRed:0.09 green:0.58 blue:0.82 alpha:1.0];
            prompt = [NSColor whiteColor];
        } else if (layout.distro == "opensuse") {
            shell = [NSColor colorWithCalibratedRed:0.45 green:0.73 blue:0.15 alpha:1.0];
            prompt = [NSColor whiteColor];
        }

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
@property (nonatomic, copy) NSString* iconName;
@property (nonatomic, assign) BOOL isManual;
@property (nonatomic, assign) BOOL isLnk;
/* path with symlinks resolved, used only to recognize an app already in the
 * list. Distinct from path, which stays as discovered so the row shows where
 * the entry actually came from.
 */
@property (nonatomic, copy) NSString* resolvedPath;
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
- (std::filesystem::path)activeSysrootForPrefix:(const prefix::PrefixLayout*)selected;
- (void)writeLaunchLogHeaderForPrefix:(prefix::PrefixLayout*)selected appName:(NSString*)appName;
- (BOOL)guestExecutableExists:(NSString*)guestPath prefix:(prefix::PrefixLayout*)selected;
- (BOOL)guestXwaylandAvailableInPrefix:(prefix::PrefixLayout*)selected;
- (NSString*)guestXwaylandPathForPrefix:(prefix::PrefixLayout*)selected;
- (BOOL)isLinuxShellShortcut:(MuplarAppShortcut*)app;
- (NSString*)guestShellPathForPrefix:(prefix::PrefixLayout*)selected;
- (void)launchLinuxShellInHostTerminal:(MuplarAppShortcut*)app prefix:(prefix::PrefixLayout*)selected;
- (NSArray<NSString*>*)wrapGuestArgumentsForX11IfNeeded:(NSArray<NSString*>*)guestArguments
                                                 prefix:(prefix::PrefixLayout*)selected
                                           errorMessage:(NSString**)errorMessage;
- (NSWindow*)beginBusySheetWithTitle:(NSString*)title message:(NSString*)message;
- (void)endBusySheet:(NSWindow*)sheet;
- (BOOL)androidArtSysrootReadyAtPath:(NSString*)sysroot;
- (NSString*)ensureAndroidArtSysrootForInstanceName:(NSString*)name
                                       errorMessage:(NSString**)errorMessage;
- (void)setupMenu;
- (BOOL)ensureLinuxSessionForPrefix:(prefix::PrefixLayout*)selected
                       errorMessage:(NSString**)errorMessage;
- (void)launchLinuxDefaultPackageInstallForName:(NSString*)name
                                         distro:(NSString*)distro
                                            arch:(NSString*)arch
                                      targetRoot:(NSString*)targetRoot;
- (void)prestartLinuxSessionIfNeeded:(prefix::PrefixLayout*)selected;
- (void)launchAndroidApp:(MuplarAppShortcut*)app
                   prefix:(prefix::PrefixLayout*)selected;
- (NSString*)androidDeviceKeyForPrefix:(prefix::PrefixLayout*)selected;
- (AndroidDeviceShell*)androidDeviceShellForPrefix:(prefix::PrefixLayout*)selected
                                               app:(MuplarAppShortcut*)app
                                        sessionKey:(NSString*)sessionKey;
- (void)recordAndroidDeviceEvent:(NSString*)event
                    tabIdentifier:(NSString*)tabIdentifier
                          logPath:(NSString*)logPath;
- (void)sendAndroidDeviceAction:(NSString*)action
                   tabIdentifier:(NSString*)tabIdentifier
                      socketPath:(NSString*)socketPath
                         logPath:(NSString*)logPath;
- (void)sendAndroidDeviceInputForTab:(NSString*)tabIdentifier
                                 type:(int32_t)type
                               action:(int32_t)action
                               source:(int32_t)source
                             deviceId:(int32_t)deviceId
                              keyCode:(int32_t)keyCode
                                    x:(float)x
                                    y:(float)y
                           socketPath:(NSString*)socketPath
                              logPath:(NSString*)logPath;
- (void)pollTabFinishedForIdentifier:(NSString*)tabIdentifier
                          deviceShell:(AndroidDeviceShell*)deviceShell
                           socketPath:(NSString*)socketPath;
- (NSDictionary<NSString*, NSString*>*)androidLaunchMetadataForApp:
    (MuplarAppShortcut*)app
                                                           prefix:
                                                               (prefix::PrefixLayout*)selected;
- (void)prepareAndroidLaunchMetadataForApp:(MuplarAppShortcut*)app
                                    rootfs:(NSString*)rootfsPath
                                completion:
                                    (void (^)(NSDictionary<NSString*, NSString*>* metadata))
                                        completion;
- (NSDictionary<NSString*, NSString*>*)androidLaunchMetadataForAppPath:
                                          (NSString*)appPath
                                                           rootfs:
                                                               (NSString*)rootfsPath;
- (void)sendAndroidDeviceAction:(NSString*)action
                   tabIdentifier:(NSString*)tabIdentifier
                  launchMetadata:(NSDictionary<NSString*, NSString*>*)metadata
                      socketPath:(NSString*)socketPath
                         logPath:(NSString*)logPath;
- (void)waitForLinuxWindowForKey:(NSString*)key
                           token:(NSUUID*)token
           baselineWindowNumbers:(NSSet<NSNumber*>*)baselineWindowNumbers
                        attempts:(NSUInteger)attempts;
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
    NSMutableDictionary<NSString*, NSUUID*>* _linuxLaunchTokens;
    NSMutableDictionary<NSString*, NSTask*>* _linuxSessionTasks;
    NSMutableDictionary<NSString*, NSTask*>* _packageInstallTasks;
    NSMutableDictionary<NSString*, AndroidDeviceShell*>* _androidDeviceShells;
    NSMutableDictionary<NSString*, NSTask*>* _androidSessionTasks;
    NSMutableDictionary<NSString*, NSTask*>* _pendingAndroidSessionTasks;
    NSMutableDictionary<NSString*, NSString*>* _androidSessionAppKeys;
    NSMutableDictionary<NSString*, NSString*>* _androidSessionTabIdentifiers;
    NSMutableDictionary<NSString*, NSDictionary<NSString*, NSString*>*>*
        _androidTabLaunchMetadata;
    NSMutableSet<NSString*>* _startingAndroidSessionKeys;
    NSMutableSet<NSString*>* _cancelledAndroidSessionKeys;
    NSMutableSet<NSString*>* _stoppingAndroidSessionKeys;
    NSTask* _waylandTask;
    std::unique_ptr<supervisor::SupervisorService> _supervisor;
    dispatch_source_t _termSignalSource;

    NSTextField* _autoNameField;
    NSPopUpButton* _autoNameKindPopup;
    NSPopUpButton* _autoNameArchPopup;
    NSPopUpButton* _autoNameDistroPopup;
    NSTextField* _autoNameSysrootField;
    NSString* _autoGeneratedName;
    BOOL _autoNameEdited;
    std::vector<prefix::PrefixLayout> _prefixes;
}

- (void)setupMenu
{
    NSMenu* menubar = [[NSMenu alloc] init];
    NSString* appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"] ?: @"Muplar Instance Manager";

    // App menu
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] init];

    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"About %@", appName]
                                                action:@selector(orderFrontStandardAboutPanel:)
                                         keyEquivalent:@""]];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
                                                action:@selector(hide:)
                                         keyEquivalent:@"h"]];

    NSMenuItem* hideOthers = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                                        action:@selector(hideOtherApplications:)
                                                 keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [appMenu addItem:hideOthers];

    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Show All"
                                                action:@selector(unhideAllApplications:)
                                         keyEquivalent:@""]];
    [appMenu addItem:[NSMenuItem separatorItem]];

    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
                                                action:@selector(terminate:)
                                         keyEquivalent:@"q"]];

    [appMenuItem setSubmenu:appMenu];
    [menubar addItem:appMenuItem];

    // Window menu
    NSMenuItem* windowMenuItem = [[NSMenuItem alloc] init];
    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Minimize"
                                                   action:@selector(performMiniaturize:)
                                            keyEquivalent:@"m"]];
    [windowMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Zoom"
                                                   action:@selector(performZoom:)
                                            keyEquivalent:@""]];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Bring All to Front"
                                                   action:@selector(arrangeInFront:)
                                            keyEquivalent:@""]];
    [windowMenuItem setSubmenu:windowMenu];
    [menubar addItem:windowMenuItem];

    [NSApp setWindowsMenu:windowMenu];
    [NSApp setMainMenu:menubar];
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _appsList = [NSMutableArray array];
        _runningTasks = [NSMutableDictionary dictionary];
        _launchingAppPaths = [NSMutableSet set];
        _linuxLaunchTokens = [NSMutableDictionary dictionary];
        _linuxSessionTasks = [NSMutableDictionary dictionary];
        _packageInstallTasks = [NSMutableDictionary dictionary];
        _androidDeviceShells = [NSMutableDictionary dictionary];
        _androidSessionTasks = [NSMutableDictionary dictionary];
        _pendingAndroidSessionTasks = [NSMutableDictionary dictionary];
        _androidSessionAppKeys = [NSMutableDictionary dictionary];
        _androidSessionTabIdentifiers = [NSMutableDictionary dictionary];
        _androidTabLaunchMetadata = [NSMutableDictionary dictionary];
        _startingAndroidSessionKeys = [NSMutableSet set];
        _cancelledAndroidSessionKeys = [NSMutableSet set];
        _stoppingAndroidSessionKeys = [NSMutableSet set];
        _supervisor = std::make_unique<supervisor::SupervisorService>();
    }
    return self;
}

- (void)installTerminationSignalHandler
{
    if (_termSignalSource)
        return;

    signal(SIGTERM, SIG_IGN);
    _termSignalSource =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,
                               SIGTERM,
                               0,
                               dispatch_get_main_queue());
    dispatch_source_set_event_handler(_termSignalSource, ^{
        [NSApp terminate:nil];
    });
    dispatch_resume(_termSignalSource);
}

- (std::filesystem::path)activeSysrootForPrefix:(const prefix::PrefixLayout*)selected
{
    if (!selected) return {};

    std::filesystem::path sysroot = selected->runtime_sysroot;
    if (!sysroot.empty()) {
        NSString* sysrootStr = NSStringFromPath(sysroot);
        NSString* resolvedStr = ResolveWorkspaceRelativePath(sysrootStr);
        std::filesystem::path resolvedPath = std::filesystem::path(resolvedStr.UTF8String);
        std::error_code ec;
        if (std::filesystem::exists(resolvedPath, ec)) {
            return resolvedPath;
        }
    }
    return selected->rootfs;
}

- (NSWindow*)beginBusySheetWithTitle:(NSString*)title message:(NSString*)message
{
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 420, 132)
                                                styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    panel.title = title ?: @"Working";
    panel.releasedWhenClosed = NO;

    NSView* content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 132)];
    panel.contentView = content;

    NSProgressIndicator* spinner = [[NSProgressIndicator alloc] init];
    spinner.style = NSProgressIndicatorStyleSpinning;
    spinner.controlSize = NSControlSizeRegular;
    spinner.displayedWhenStopped = YES;
    spinner.translatesAutoresizingMaskIntoConstraints = NO;
    [spinner startAnimation:nil];
    [content addSubview:spinner];

    NSTextField* titleLabel = [NSTextField labelWithString:title ?: @"Working"];
    titleLabel.font = [NSFont systemFontOfSize:16 weight:NSFontWeightSemibold];
    titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:titleLabel];

    NSTextField* messageLabel = [NSTextField wrappingLabelWithString:message ?: @""];
    messageLabel.textColor = NSColor.secondaryLabelColor;
    messageLabel.font = [NSFont systemFontOfSize:12];
    messageLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:messageLabel];

    [NSLayoutConstraint activateConstraints:@[
        [spinner.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:24.0],
        [spinner.centerYAnchor constraintEqualToAnchor:content.centerYAnchor],
        [spinner.widthAnchor constraintEqualToConstant:28.0],
        [spinner.heightAnchor constraintEqualToConstant:28.0],

        [titleLabel.leadingAnchor constraintEqualToAnchor:spinner.trailingAnchor constant:18.0],
        [titleLabel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24.0],
        [titleLabel.topAnchor constraintEqualToAnchor:content.topAnchor constant:30.0],

        [messageLabel.leadingAnchor constraintEqualToAnchor:titleLabel.leadingAnchor],
        [messageLabel.trailingAnchor constraintEqualToAnchor:titleLabel.trailingAnchor],
        [messageLabel.topAnchor constraintEqualToAnchor:titleLabel.bottomAnchor constant:8.0],
    ]];

    [_window beginSheet:panel completionHandler:nil];
    return panel;
}

- (void)endBusySheet:(NSWindow*)sheet
{
    if (!sheet)
        return;
    [_window endSheet:sheet];
    [sheet orderOut:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [self setupMenu];
    [self installTerminationSignalHandler];
    [self buildWindow];
    [self reloadPrefixes:nil];
    for (size_t i = 0; i < _prefixes.size(); ++i) {
        if (_prefixes[i].kind == prefix::PrefixKind::Linux) {
            [self prestartLinuxSessionIfNeeded:&_prefixes[i]];
        }
    }
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // Warm the shared compositor after the manager has had a chance to draw.
    // Linux launches can then connect immediately instead of paying this cost
    // while their application is starting.
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_supervisor && !self->_supervisor->is_running())
            self->_supervisor->start();
    });

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1500 * NSEC_PER_MSEC)),
                   dispatch_get_main_queue(), ^{
        [self reloadPrefixes:nil];
    });
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    static BOOL alreadyStopping = NO;
    if (alreadyStopping) {
        return NSTerminateNow;
    }

    alreadyStopping = YES;

    // Kill all running app tasks first so they don't hold up supervisor shutdown
    NSDictionary<NSString*, NSTask*>* tasksCopy = [_runningTasks copy];
    for (NSString* key in tasksCopy) {
        NSTask* task = tasksCopy[key];
        if (task.isRunning) {
            [task terminate];
        }
    }
    [_runningTasks removeAllObjects];
    NSDictionary<NSString*, NSTask*>* androidSessionTasksCopy =
        [_androidSessionTasks copy];
    for (NSTask* task in androidSessionTasksCopy.allValues) {
        if (task.isRunning)
            [task terminate];
    }
    NSDictionary<NSString*, NSTask*>* pendingAndroidTasksCopy =
        [_pendingAndroidSessionTasks copy];
    for (NSTask* task in pendingAndroidTasksCopy.allValues) {
        if (task.isRunning)
            [task terminate];
    }
    [_androidSessionTasks removeAllObjects];
    [_pendingAndroidSessionTasks removeAllObjects];
    [_androidSessionAppKeys removeAllObjects];
    [_androidSessionTabIdentifiers removeAllObjects];
    [_androidTabLaunchMetadata removeAllObjects];
    [_startingAndroidSessionKeys removeAllObjects];
    [_cancelledAndroidSessionKeys removeAllObjects];
    [_stoppingAndroidSessionKeys removeAllObjects];
    [_androidDeviceShells removeAllObjects];
    for (NSTask* task in _packageInstallTasks.allValues) {
        if (task.isRunning)
            [task terminate];
    }
    [_packageInstallTasks removeAllObjects];
    NSDictionary<NSString*, NSTask*>* sessionTasksCopy = [_linuxSessionTasks copy];
    [_linuxSessionTasks removeAllObjects];

    // Create a standalone progress window if the main window isn't available
    NSWindow* busyWindow = nil;
    if (_window && [_window isVisible]) {
        busyWindow = [self beginBusySheetWithTitle:@"Exiting"
                                           message:@"Shutting down background services..."];
    } else {
        // Create a floating panel to show exit progress
        busyWindow = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 360, 100)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskHUDWindow
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
        busyWindow.title = @"Exiting";
        busyWindow.releasedWhenClosed = NO;
        NSView* content = busyWindow.contentView;

        NSProgressIndicator* spinner = [[NSProgressIndicator alloc] init];
        spinner.style = NSProgressIndicatorStyleSpinning;
        spinner.controlSize = NSControlSizeRegular;
        spinner.translatesAutoresizingMaskIntoConstraints = NO;
        [spinner startAnimation:nil];
        [content addSubview:spinner];

        NSTextField* label = [NSTextField labelWithString:@"Shutting down background services..."];
        label.font = [NSFont systemFontOfSize:13];
        label.textColor = NSColor.secondaryLabelColor;
        label.translatesAutoresizingMaskIntoConstraints = NO;
        [content addSubview:label];

        [NSLayoutConstraint activateConstraints:@[
            [spinner.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:20],
            [spinner.centerYAnchor constraintEqualToAnchor:content.centerYAnchor],
            [spinner.widthAnchor constraintEqualToConstant:24],
            [spinner.heightAnchor constraintEqualToConstant:24],
            [label.leadingAnchor constraintEqualToAnchor:spinner.trailingAnchor constant:14],
            [label.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-20],
            [label.centerYAnchor constraintEqualToAnchor:content.centerYAnchor],
        ]];
        [busyWindow center];
        [busyWindow makeKeyAndOrderFront:nil];
    }

    __weak PrefixManagerAppDelegate* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        for (int i = 0; i < 20; ++i) {
            BOOL clientsRunning = NO;
            for (NSTask* task in tasksCopy.allValues)
                clientsRunning = clientsRunning || task.isRunning;
            if (!clientsRunning)
                break;
            usleep(100000);
        }
        for (NSTask* task in sessionTasksCopy.allValues) {
            if (task.isRunning)
                [task terminate];
        }
        if (strongSelf) {
            if (strongSelf->_supervisor) {
                strongSelf->_supervisor->stop();
            }
            for (const auto& p : strongSelf->_prefixes) {
                if (p.kind == prefix::PrefixKind::Android) {
                    StopAndroidMuplard(p);
                }
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            PrefixManagerAppDelegate* strongSelf2 = weakSelf;
            if (strongSelf2 && strongSelf2->_window && [strongSelf2->_window isVisible]) {
                [strongSelf2 endBusySheet:busyWindow];
            } else {
                [busyWindow orderOut:nil];
            }
            [NSApp replyToApplicationShouldTerminate:YES];
        });
    });

    return NSTerminateLater;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    if (_termSignalSource) {
        dispatch_source_cancel(_termSignalSource);
        _termSignalSource = nil;
    }
    // Note: _supervisor->stop() is already called in applicationShouldTerminate:
    // Do NOT call it again here to avoid blocking the main thread.
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

- (NSString*)defaultInstanceNameForType:(NSString*)type arch:(NSString*)arch distro:(NSString*)distro
{
    NSString* normalizedType = type ?: @"Android";

    if ([normalizedType rangeOfString:@"wine" options:NSCaseInsensitiveSearch].location != NSNotFound ||
        [normalizedType rangeOfString:@"windows" options:NSCaseInsensitiveSearch].location != NSNotFound) {
        normalizedType = @"Windows";
    } else if ([normalizedType caseInsensitiveCompare:@"android"] == NSOrderedSame) {
        normalizedType = @"Android";
    } else if ([normalizedType caseInsensitiveCompare:@"linux"] == NSOrderedSame) {
        if (distro && ![distro isEqualToString:@"Generic Linux"]) {
            normalizedType = distro;
        } else {
            normalizedType = @"Linux";
        }
    }

    return [NSString stringWithFormat:@"%@-%@", normalizedType.lowercaseString, CanonicalArchForUI(arch).lowercaseString];
}

- (void)updateAutoInstanceName
{
    if (!_autoNameField || !_autoNameKindPopup || !_autoNameArchPopup ||
        _autoNameEdited) {
        return;
    }

    _autoGeneratedName =
        [self defaultInstanceNameForType:_autoNameKindPopup.titleOfSelectedItem
                                    arch:_autoNameArchPopup.titleOfSelectedItem
                                  distro:_autoNameDistroPopup.titleOfSelectedItem];
    _autoNameField.stringValue = _autoGeneratedName;
}

- (void)autoNameChoiceChanged:(id)sender
{
    if (sender == _autoNameKindPopup) {
        [self configureArchPopup:_autoNameArchPopup
                         forType:_autoNameKindPopup.titleOfSelectedItem];
        BOOL isLinux = [_autoNameKindPopup.titleOfSelectedItem isEqualToString:@"Linux"];
        _autoNameDistroPopup.enabled = isLinux;
        if (_autoNameSysrootField) {
            _autoNameSysrootField.stringValue = @"";
        }
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
               distroPopup:(NSPopUpButton*)distroPopup
              sysrootField:(NSTextField*)sysrootField
{
    _autoNameField = nameField;
    _autoNameKindPopup = kindPopup;
    _autoNameArchPopup = archPopup;
    _autoNameDistroPopup = distroPopup;
    _autoNameSysrootField = sysrootField;
    _autoGeneratedName = nil;
    _autoNameEdited = NO;

    nameField.delegate = self;
    kindPopup.target = self;
    kindPopup.action = @selector(autoNameChoiceChanged:);
    archPopup.target = self;
    archPopup.action = @selector(autoNameChoiceChanged:);
    distroPopup.target = self;
    distroPopup.action = @selector(autoNameChoiceChanged:);

    [self configureArchPopup:archPopup forType:kindPopup.titleOfSelectedItem];
    [self updateAutoInstanceName];
}

- (void)clearAutoNameTracking
{
    _autoNameField = nil;
    _autoNameKindPopup = nil;
    _autoNameArchPopup = nil;
    _autoNameDistroPopup = nil;
    _autoNameSysrootField = nil;
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
    if ([choices containsObject:selected]) {
        [archPopup selectItemWithTitle:selected];
    } else {
        [archPopup selectItemWithTitle:choices.firstObject];
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
    _window.releasedWhenClosed = NO;
    _window.title = @"Muplar Instance Manager";
    _window.minSize = NSMakeSize(980, 560);
    [_window center];
    [WWNCompositorBridge sharedBridge].parentWindowForClients = _window;

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

- (prefix::PrefixLayout*)prefixNamed:(NSString*)name
{
    if (name.length == 0)
        return nullptr;
    std::string target(name.UTF8String);
    for (auto &item : _prefixes) {
        if (item.name == target)
            return &item;
    }
    return nullptr;
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

static NSString* MapLinuxIconToSFSymbol(NSString* icon)
{
    if (!icon || icon.length == 0) return nil;
    NSString* lower = icon.lowercaseString;
    if ([lower containsString:@"firefox"] || [lower containsString:@"browser"] || [lower containsString:@"web"] || [lower containsString:@"chrome"]) {
        return @"globe";
    }
    if ([lower containsString:@"terminal"] || [lower containsString:@"console"] || [lower containsString:@"xterm"]) {
        return @"terminal";
    }
    if ([lower containsString:@"text"] || [lower containsString:@"edit"] || [lower containsString:@"writer"] || [lower containsString:@"word"]) {
        return @"doc.text";
    }
    if ([lower containsString:@"system"] || [lower containsString:@"settings"] || [lower containsString:@"config"] || [lower containsString:@"control"]) {
        return @"gearshape";
    }
    if ([lower containsString:@"file"] || [lower containsString:@"folder"] || [lower containsString:@"directory"]) {
        return @"folder";
    }
    if ([lower containsString:@"mail"] || [lower containsString:@"thunderbird"] || [lower containsString:@"envelope"]) {
        return @"envelope";
    }
    if ([lower containsString:@"music"] || [lower containsString:@"audio"] || [lower containsString:@"player"] || [lower containsString:@"sound"]) {
        return @"music.note";
    }
    if ([lower containsString:@"video"] || [lower containsString:@"movie"] || [lower containsString:@"media"]) {
        return @"play.rectangle";
    }
    if ([lower containsString:@"image"] || [lower containsString:@"photo"] || [lower containsString:@"paint"] || [lower containsString:@"drawing"]) {
        return @"photo";
    }
    if ([lower containsString:@"game"] || [lower containsString:@"arcade"] || [lower containsString:@"play"]) {
        return @"gamecontroller";
    }
    return nil;
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
            prefix::PrefixLayout* selected = [self selectedPrefix];
            if (app.iconName && app.iconName.length > 0) {
                NSString* mapped = MapLinuxIconToSFSymbol(app.iconName);
                if (mapped) iconName = mapped;
                else iconName = @"cpu";
            } else if (app.isLnk) {
                iconName = @"link";
            } else if ([app.path isEqualToString:@"explorer"]) {
                iconName = @"macwindow";
            } else if ([app.path isEqualToString:@"winecfg"]) {
                iconName = @"gearshape";
            } else if ([app.path isEqualToString:@"regedit"]) {
                iconName = @"slider.horizontal.3";
            } else if ([app.path isEqualToString:@"cmd"]) {
                iconName = @"terminal";
            } else if ([app.path isEqualToString:@"iexplore"] ||
                       [app.path isEqualToString:@"iexplore.exe"]) {
                iconName = @"globe";
            } else if ([app.path isEqualToString:@"control"]) {
                iconName = @"slider.horizontal.below.rectangle";
            } else if ([app.path isEqualToString:@"taskmgr"]) {
                iconName = @"chart.bar";
            } else if (selected && selected->kind == prefix::PrefixKind::Android) {
                iconName = [app.name.lowercaseString containsString:@"setting"]
                    ? @"gearshape"
                    : @"app.fill";
            } else if (selected && selected->kind == prefix::PrefixKind::Linux) {
                iconName = @"terminal";
                NSString* lowerName = app.name.lowercaseString;
                if ([lowerName containsString:@"firefox"] || [lowerName containsString:@"browser"]) {
                    iconName = @"globe";
                } else if ([lowerName containsString:@"terminal"] || [lowerName containsString:@"xterm"] || [lowerName containsString:@"console"] || [lowerName containsString:@"sh"]) {
                    iconName = @"terminal";
                } else if ([lowerName containsString:@"text"] || [lowerName containsString:@"edit"]) {
                    iconName = @"doc.text";
                } else if ([lowerName containsString:@"system"] || [lowerName containsString:@"config"] || [lowerName containsString:@"setting"]) {
                    iconName = @"gearshape";
                } else if ([lowerName containsString:@"file"] || [lowerName containsString:@"folder"]) {
                    iconName = @"folder";
                }
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
    [kindPopup addItemsWithTitles:@[@"Android", @"Linux", @"Windows"]];
    NSPopUpButton* archPopup = [[NSPopUpButton alloc] init];
    [archPopup addItemsWithTitles:@[@"ARM64", @"x64"]];
    NSPopUpButton* distroPopup = [[NSPopUpButton alloc] init];
    [distroPopup addItemsWithTitles:@[@"Ubuntu", @"Alpine", @"Debian", @"Fedora", @"Arch", @"openSUSE"]];
    distroPopup.enabled = NO; // Android selected by default
    NSTextField* sysrootField = [NSTextField textFieldWithString:@""];
    [self trackAutoNameField:nameField kindPopup:kindPopup archPopup:archPopup distroPopup:distroPopup sysrootField:sysrootField];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[[self label:@"Name"], nameField],
        @[[self label:@"Type"], kindPopup],
        @[[self label:@"Distro"], distroPopup],
        @[[self label:@"Arch"], archPopup],
        @[[self label:@"Parent"], [self locationPickerWithField:locationField]],
    ]];
    grid.rowSpacing = 8.0;
    grid.columnSpacing = 12.0;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:1].width = 420.0;
    alert.accessoryView = [self dialogAccessoryWithGrid:grid
                                                  width:540.0
                                                 height:168.0];
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

    std::string kind = StdStringFromNSString(kindPopup.titleOfSelectedItem.lowercaseString);
    if (kind.find("wine") != std::string::npos || kind.find("windows") != std::string::npos)
        kind = "wine";
    else if (kind == "android" || kind == "linux") {
        std::transform(kind.begin(), kind.end(), kind.begin(), ::tolower);
    }
    std::string distro = "";
    if (kind == "linux") {
        distro = StdStringFromNSString(distroPopup.titleOfSelectedItem.lowercaseString);
    }
    std::string location = StdStringFromNSString(locationField.stringValue);
    NSString* resolvedSysroot = ResolveWorkspaceRelativePath(sysrootField.stringValue);
    NSString* internalArch = InternalArchFromUI(archPopup.titleOfSelectedItem);
    std::filesystem::path targetRoot = location.empty()
        ? prefix::resolve_prefix_root(name)
        : ChildPathForName(location, name);
    if (PathExists(targetRoot)) {
        [self showError:
            [NSString stringWithFormat:@"Instance location already exists: %@",
                                       NSStringFromPath(targetRoot)]];
        return;
    }



    NSString* instanceName = [nameField.stringValue copy];
    NSString* distroTitle = [distroPopup.titleOfSelectedItem copy];
    NSString* targetRootString = NSStringFromPath(targetRoot);
    NSString* sysrootString = [resolvedSysroot copy];
    NSString* archString = [internalArch copy];
    BOOL androidArchiveCached =
        [[NSFileManager defaultManager] fileExistsAtPath:DefaultAndroidArtSysrootArchivePath()];
    BOOL androidNeedsSysrootDialog =
        kind == "android" &&
        (![self androidArtSysrootReadyAtPath:DefaultAndroidArtSysrootPath()] ||
         !androidArchiveCached);
    NSString* busyMessage = nil;
    if (kind == "wine") {
        busyMessage = @"Preparing the Windows instance and updating its configuration. This can take a little while.";
    } else if (kind == "linux") {
        busyMessage = @"Downloading or preparing the Linux rootfs. The instance will appear when setup finishes.";
    } else {
        busyMessage = @"Preparing the Android instance rootfs.";
    }
    NSWindow* busySheet = androidNeedsSysrootDialog
        ? nil
        : [self beginBusySheetWithTitle:[NSString stringWithFormat:@"Creating %@", instanceName]
                                message:busyMessage];
    _statusLabel.stringValue =
        [NSString stringWithFormat:@"Creating %@...", instanceName];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        __block BOOL ok = YES;
        __block NSString* errorMessage = nil;
        __block prefix::PrefixLayout createdLayout;
        __block BOOL hasCreatedLayout = NO;

        @autoreleasepool {
            try {
                if (kind == "linux") {
                    NSString* provisionError = nil;
                    ok = [self provisionLinuxInstanceName:instanceName
                                                   distro:distroTitle
                                                     arch:archString
                                               targetRoot:targetRootString
                                                  sysroot:sysrootString
                                             errorMessage:&provisionError];
                    if (!ok) {
                        errorMessage = provisionError ?: @"Linux provisioning failed.";
                    }
                } else {
                    NSString* runtimeSysroot = sysrootString;
                    if (kind == "android") {
                        NSString* androidError = nil;
                        runtimeSysroot =
                            [self ensureAndroidArtSysrootForInstanceName:instanceName
                                                            errorMessage:&androidError];
                        if (!runtimeSysroot) {
                            ok = NO;
                            errorMessage = androidError ?:
                                @"Android ART sysroot setup failed.";
                        }
                    }
                    if (!ok)
                        return;
                    createdLayout = prefix::open_prefix_at_root(
                        name,
                        targetRoot,
                        StdStringFromNSString(runtimeSysroot),
                        true,
                        prefix::parse_prefix_kind(kind),
                        prefix::parse_guest_arch(StdStringFromNSString(archString)),
                        "elfuse",
                        distro);
                    hasCreatedLayout = YES;
                }
            } catch (const std::exception& e) {
                ok = NO;
                errorMessage = NSStringFromStdString(e.what());
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            [self endBusySheet:busySheet];
            if (!ok) {
                self->_statusLabel.stringValue = @"Creation failed";
                [self showError:errorMessage ?: @"Instance creation failed."];
                return;
            }
            if (hasCreatedLayout && self->_supervisor)
                self->_supervisor->on_prefix_created(createdLayout);
            [self reloadPrefixesSelectingName:name];
            if (kind == "linux") {
                [self launchLinuxDefaultPackageInstallForName:instanceName
                                                       distro:distroTitle
                                                          arch:archString
                                                    targetRoot:targetRootString];
            }
        });
    });
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

        NSString* instanceName = [nameField.stringValue copy];
        NSWindow* busySheet =
            [self beginBusySheetWithTitle:[NSString stringWithFormat:@"Cloning %@", instanceName]
                                   message:@"Copying the instance root and registering the cloned environment."];
        _statusLabel.stringValue =
            [NSString stringWithFormat:@"Cloning %@...", instanceName];

        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            __block BOOL ok = YES;
            __block NSString* errorMessage = nil;
            __block prefix::PrefixLayout clonedLayout;
            __block BOOL hasClonedLayout = NO;

            @autoreleasepool {
                try {
                    clonedLayout =
                        prefix::clone_prefix_to_root(sourceRoot, newName,
                                                     targetRoot, replaceExisting);
                    hasClonedLayout = YES;
                } catch (const std::exception& e) {
                    ok = NO;
                    errorMessage = NSStringFromStdString(e.what());
                }
            }

            dispatch_async(dispatch_get_main_queue(), ^{
                [self endBusySheet:busySheet];
                if (!ok) {
                    self->_statusLabel.stringValue = @"Clone failed";
                    [self showError:errorMessage ?: @"Instance clone failed."];
                    return;
                }
                if (hasClonedLayout && self->_supervisor)
                    self->_supervisor->on_prefix_created(clonedLayout);
                [self reloadPrefixesSelectingName:newName];
            });
        });
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
        NSString* sessionKey = NSStringFromStdString(selectedName);
        NSTask* packageTask = _packageInstallTasks[sessionKey];
        if (packageTask && packageTask.isRunning)
            [packageTask terminate];
        [_packageInstallTasks removeObjectForKey:sessionKey];
        NSTask* sessionTask = _linuxSessionTasks[sessionKey];
        if (sessionTask && sessionTask.isRunning) {
            [sessionTask terminate];
            for (int i = 0; i < 20 && sessionTask.isRunning; ++i)
                usleep(100000);
        }
        [_linuxSessionTasks removeObjectForKey:sessionKey];
        if (_supervisor)
            _supervisor->on_prefix_deleted(selectedName);
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
    NSString* path = nil;

    // 1. runtime_sysroot stored in the prefix.
    if (!layout.runtime_sysroot.empty()) {
        NSString* sysrootStr = NSStringFromPath(layout.runtime_sysroot);
        NSString* resolvedStr = ResolveWorkspaceRelativePath(sysrootStr);
        for (NSString* binName in @[@"wine64", @"wine"]) {
            NSString* cPath = [[resolvedStr stringByAppendingPathComponent:@"bin"] stringByAppendingPathComponent:binName];
            NSLog(@"[Windows] checking sysroot: %@", cPath);
            if ([[NSFileManager defaultManager] isExecutableFileAtPath:cPath]) {
                path = cPath;
                break;
            }
        }
    }

    // 2. Embedded in the app bundle: Contents/Frameworks/wine/bin/wine (preferred fallback)
    if (!path) {
        NSString* frameworksPath = [[NSBundle mainBundle] privateFrameworksPath];
        NSString* bundleWine = [[[frameworksPath stringByAppendingPathComponent:@"wine"]
                                                 stringByAppendingPathComponent:@"bin"]
                                                 stringByAppendingPathComponent:@"wine"];
        NSLog(@"[Windows] checking bundle: %@", bundleWine);
        if ([[NSFileManager defaultManager] isExecutableFileAtPath:bundleWine]) {
            path = bundleWine;
        }
    }

    // 3. Dev build: build/bin/App.app -> build/bin -> build -> build/wine-prefix/bin/wine
    if (!path) {
        NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
        NSString* buildDir   = [[bundlePath stringByDeletingLastPathComponent]
                                            stringByDeletingLastPathComponent];
        NSString* devCandidate = [[[[buildDir stringByAppendingPathComponent:@"wine-prefix"]
                                               stringByAppendingPathComponent:@"bin"]
                                               stringByAppendingPathComponent:@"wine"]
                                               stringByStandardizingPath];
        NSLog(@"[Windows] checking dev build: %@", devCandidate);
        if ([[NSFileManager defaultManager] isExecutableFileAtPath:devCandidate]) {
            path = devCandidate;
        }
    }

    // 4. System PATH.
    if (!path) {
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
            NSLog(@"[Windows] which: %@", result);
            if (result.length > 0 &&
                [[NSFileManager defaultManager] isExecutableFileAtPath:result]) {
                path = result;
            }
        } @catch (...) {}
    }

    if (path) {
        // Resolve symlinks to get canonical path so that sibling DLL / lib paths work
        std::error_code ec;
        std::filesystem::path canonicalPath = std::filesystem::canonical(path.UTF8String, ec);
        if (!ec) {
            path = NSStringFromStdString(canonicalPath.string());
        }
        NSLog(@"[Windows] resolved compatibility runtime path to canonical: %@", path);
        return path;
    }

    NSLog(@"[Windows] not found. bundle=%@", [[NSBundle mainBundle] bundlePath]);
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
    [self addShortcutWithName:name path:path unixPath:unixPath iconName:nil isManual:isManual isLnk:isLnk];
}

- (void)addShortcutWithName:(NSString*)name
                       path:(NSString*)path
                   unixPath:(NSString*)unixPath
                   iconName:(NSString*)iconName
                   isManual:(BOOL)isManual
                      isLnk:(BOOL)isLnk
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    NSString* normalizedPath = path;
    if (selected && selected->kind == prefix::PrefixKind::Wine) {
        normalizedPath = [path stringByReplacingOccurrencesOfString:@"/" withString:@"\\"];
    }

    /* Match on the resolved path, not the string we were handed. Distros that
     * merge /bin into /usr/bin reach one binary by two names, and the terminal
     * scan probes both spellings while a .desktop Exec= may name a third, so a
     * literal compare lists the same program several times -- xterm and uxterm
     * showed up twice each. Wine paths are already backslash-normalized above
     * and are not host paths, so they keep the literal comparison.
     */
    NSString* resolvedPath = normalizedPath;
    if (!(selected && selected->kind == prefix::PrefixKind::Wine)) {
        std::error_code resolveEc;
        std::filesystem::path canonical =
            std::filesystem::canonical(normalizedPath.UTF8String, resolveEc);
        if (!resolveEc)
            resolvedPath = NSStringFromStdString(canonical.string());
    }

    for (MuplarAppShortcut* existing in _appsList) {
        NSString* existingKey = existing.resolvedPath ?: existing.path;
        if ([existingKey caseInsensitiveCompare:resolvedPath] == NSOrderedSame) {
            return;
        }
    }

    MuplarAppShortcut* app = [[MuplarAppShortcut alloc] init];
    app.name = name;
    app.path = normalizedPath;
    app.resolvedPath = resolvedPath;
    app.unixPath = unixPath;
    app.iconName = iconName;
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
                if (selected->kind == prefix::PrefixKind::Wine) {
                    NSString* winPath;
                    NSString* unixPath;
                    if ([relPath hasPrefix:@"/"]) {
                        // Absolute host path (outside drive_c) — Wine maps host FS under Z:
                        unixPath = relPath;
                        winPath = [@"Z:" stringByAppendingString: [relPath stringByReplacingOccurrencesOfString:@"/" withString:@"\\"]];
                    } else {
                        // Relative path inside drive_c
                        winPath = [@"C:\\" stringByAppendingString: [relPath stringByReplacingOccurrencesOfString:@"/" withString:@"\\"]];
                        unixPath = [[NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"] stringByAppendingPathComponent:relPath];
                    }
                    [self addShortcutWithName:name path:winPath unixPath:unixPath isManual:YES isLnk:NO];
                } else {
                    NSString* fullPath = [NSStringFromPath([self activeSysrootForPrefix:selected]) stringByAppendingPathComponent:relPath];
                    [self addShortcutWithName:name path:fullPath unixPath:fullPath isManual:YES isLnk:NO];
                }
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
            if (selected->kind == prefix::PrefixKind::Wine) {
                // strip C:\ and convert to forward slashes
                if ([relPath hasPrefix:@"C:\\"] || [relPath hasPrefix:@"c:\\"]) {
                    relPath = [relPath substringFromIndex:3];
                }
                relPath = [relPath stringByReplacingOccurrencesOfString:@"\\" withString:@"/"];
            } else {
                // Strip whichever sysroot prefix is present — rootfs or runtime_sysroot
                NSArray<NSString*>* bases = @[
                    NSStringFromPath([self activeSysrootForPrefix:selected]).stringByStandardizingPath,
                    NSStringFromPath(selected->rootfs).stringByStandardizingPath,
                ];
                NSString* stdPath = relPath.stringByStandardizingPath;
                for (NSString* base in bases) {
                    if ([stdPath hasPrefix:base]) {
                        relPath = [stdPath substringFromIndex:base.length];
                        if ([relPath hasPrefix:@"/"]) 
                            relPath = [relPath substringFromIndex:1];
                        break;
                    }
                }
            }
            [list addObject:@{@"name": app.name, @"path": relPath}];
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

    if (selected->kind == prefix::PrefixKind::Android) {
        NSString* companion =
            [[app.path stringByDeletingPathExtension]
                stringByAppendingString:@"-classes.jar"];
        [[NSFileManager defaultManager] removeItemAtPath:app.path error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:companion error:nil];
        [self loadAppsForSelectedPrefix];
        return;
    }

    [_appsList removeObject:app];
    [self saveManualApps:selected];
    [_appsTableView reloadData];
}

- (void)scanWineApps:(prefix::PrefixLayout*)selected
{
    std::filesystem::path driveC = selected->rootfs / "drive_c";
    if (!PathExists(driveC)) return;

    [self addShortcutWithName:@"File Explorer" path:@"explorer" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Internet Explorer" path:@"iexplore.exe" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"System Configuration" path:@"winecfg" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Registry Editor" path:@"regedit" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Command Prompt" path:@"cmd" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Control Panel" path:@"control" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Task Manager" path:@"taskmgr" unixPath:@"" isManual:NO isLnk:NO];
    [self addShortcutWithName:@"Uninstaller" path:@"uninstaller" unixPath:@"" isManual:NO isLnk:NO];

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

- (NSDictionary*)parseDesktopFile:(NSString*)desktopPath rootfs:(NSString*)rootfs
{
    NSError* error = nil;
    NSString* content = [NSString stringWithContentsOfFile:desktopPath encoding:NSUTF8StringEncoding error:&error];
    if (error || !content) return nil;

    NSString* name = nil;
    NSString* execLine = nil;
    NSString* icon = nil;
    BOOL terminal = NO;
    BOOL noDisplay = NO;
    BOOL hidden = NO;

    NSArray* lines = [content componentsSeparatedByString:@"\n"];
    for (NSString* line in lines) {
        NSString* trimmed = [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([trimmed hasPrefix:@"["] && !([trimmed isEqualToString:@"[Desktop Entry]"] || [trimmed hasPrefix:@"[Desktop Entry "])) {
            break;
        }
        if ([trimmed hasPrefix:@"Name="] && !name) {
            name = [trimmed substringFromIndex:5];
        } else if ([trimmed hasPrefix:@"Exec="] && !execLine) {
            execLine = [trimmed substringFromIndex:5];
        } else if ([trimmed hasPrefix:@"Icon="] && !icon) {
            icon = [trimmed substringFromIndex:5];
        } else if ([trimmed hasPrefix:@"Terminal="]) {
            NSString* value = [[trimmed substringFromIndex:9] lowercaseString];
            terminal = [value isEqualToString:@"true"] ||
                       [value isEqualToString:@"1"] ||
                       [value isEqualToString:@"yes"];
        } else if ([trimmed hasPrefix:@"NoDisplay="]) {
            NSString* value = [[trimmed substringFromIndex:10] lowercaseString];
            noDisplay = [value isEqualToString:@"true"] ||
                        [value isEqualToString:@"1"] ||
                        [value isEqualToString:@"yes"];
        } else if ([trimmed hasPrefix:@"Hidden="]) {
            NSString* value = [[trimmed substringFromIndex:7] lowercaseString];
            hidden = [value isEqualToString:@"true"] ||
                     [value isEqualToString:@"1"] ||
                     [value isEqualToString:@"yes"];
        }
    }

    if (noDisplay || hidden) return nil;
    if (!execLine) return nil;

    NSString* rawExec = nil;
    if ([execLine hasPrefix:@"\""]) {
        NSRange nextQuote = [execLine rangeOfString:@"\"" options:0 range:NSMakeRange(1, execLine.length - 1)];
        if (nextQuote.location != NSNotFound) {
            rawExec = [execLine substringWithRange:NSMakeRange(1, nextQuote.location - 1)];
        }
    } else if ([execLine hasPrefix:@"'"]) {
        NSRange nextQuote = [execLine rangeOfString:@"'" options:0 range:NSMakeRange(1, execLine.length - 1)];
        if (nextQuote.location != NSNotFound) {
            rawExec = [execLine substringWithRange:NSMakeRange(1, nextQuote.location - 1)];
        }
    }

    if (!rawExec) {
        NSArray* tokens = [execLine componentsSeparatedByString:@" "];
        if (tokens.count > 0) {
            rawExec = tokens[0];
        }
    }

    if (!rawExec) return nil;
    rawExec = [rawExec stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\"'"]];

    NSString* hostPath = nil;
    if ([rawExec hasPrefix:@"/"]) {
        hostPath = [rootfs stringByAppendingPathComponent:rawExec];
    } else {
        NSArray* binDirs = @[@"usr/bin", @"bin", @"usr/local/bin", @"usr/sbin", @"sbin", @"usr/games"];
        for (NSString* dir in binDirs) {
            NSString* candidate = [[rootfs stringByAppendingPathComponent:dir] stringByAppendingPathComponent:rawExec];
            if ([[NSFileManager defaultManager] fileExistsAtPath:candidate]) {
                hostPath = candidate;
                break;
            }
        }
    }
    if (!hostPath) {
        hostPath = [[rootfs stringByAppendingPathComponent:@"usr/bin"] stringByAppendingPathComponent:rawExec];
    }

    if (!name) {
        name = [[desktopPath lastPathComponent] stringByDeletingPathExtension];
    }

    return @{
        @"name": name,
        @"path": hostPath,
        @"icon": icon ? icon : @"",
        @"terminal": @(terminal)
    };
}

- (NSString*)linuxTerminalDisplayNameForGuestPath:(NSString*)guestPath
{
    NSString* name = guestPath.lastPathComponent.lowercaseString;
    if ([name isEqualToString:@"gnome-terminal"])
        return @"GNOME Terminal";
    if ([name isEqualToString:@"kgx"])
        return @"Console";
    if ([name isEqualToString:@"x-terminal-emulator"])
        return @"Terminal";
    if ([name isEqualToString:@"xterm"])
        return @"xterm";
    if ([name isEqualToString:@"uxterm"])
        return @"UXTerm";
    if ([name isEqualToString:@"foot"])
        return @"foot";
    if ([name isEqualToString:@"alacritty"])
        return @"Alacritty";
    if ([name isEqualToString:@"kitty"])
        return @"Kitty";
    if ([name isEqualToString:@"konsole"])
        return @"Konsole";
    if ([name isEqualToString:@"xfce4-terminal"])
        return @"Xfce Terminal";
    if ([name isEqualToString:@"lxterminal"])
        return @"LXTerminal";
    if ([name isEqualToString:@"qterminal"])
        return @"QTerminal";
    if ([name isEqualToString:@"mate-terminal"])
        return @"MATE Terminal";
    if ([name isEqualToString:@"weston-terminal"])
        return @"Weston Terminal";
    return guestPath.lastPathComponent;
}

- (void)addLinuxTerminalShortcutIfPresent:(NSString*)guestPath
                                   prefix:(prefix::PrefixLayout*)selected
{
    if ([self linuxTerminalRequiresX11:guestPath] &&
        ![self guestXwaylandAvailableInPrefix:selected])
        return;

    std::filesystem::path activeSysroot = [self activeSysrootForPrefix:selected];
    std::filesystem::path hostPath = HostPathForGuestPath(activeSysroot, guestPath);
    std::error_code ec;
    if (!std::filesystem::exists(hostPath, ec) && !std::filesystem::is_symlink(hostPath, ec))
        return;

    [self addShortcutWithName:[self linuxTerminalDisplayNameForGuestPath:guestPath]
                         path:NSStringFromPath(hostPath)
                     unixPath:NSStringFromPath(hostPath)
                     iconName:@"terminal"
                     isManual:NO
                        isLnk:NO];
}

- (BOOL)linuxTerminalRequiresX11:(NSString*)guestPath
{
    NSString* name = guestPath.lastPathComponent.lowercaseString;
    return [name isEqualToString:@"xterm"] ||
           [name isEqualToString:@"uxterm"] ||
           [name isEqualToString:@"x-terminal-emulator"] ||
           [name isEqualToString:@"lxterminal"];
}

- (void)scanLinuxTerminalApps:(prefix::PrefixLayout*)selected
{
    auto profile = muplar::runtime::linux_common::distro_profile(
        selected ? selected->distro : std::string());

    NSMutableSet<NSString*>* seen = [NSMutableSet set];
    auto addCandidate = ^(NSString* guestPath) {
        if (guestPath.length == 0 || [seen containsObject:guestPath])
            return;
        [seen addObject:guestPath];
        [self addLinuxTerminalShortcutIfPresent:guestPath prefix:selected];
    };

    for (const auto& path : profile.terminal_paths)
        addCandidate(NSStringFromStdString(path));

    NSArray<NSString*>* genericFallbacks = @[
        @"/usr/bin/xterm", @"/bin/xterm",
        @"/usr/bin/uxterm", @"/bin/uxterm",
        @"/usr/bin/kgx",
        @"/usr/bin/gnome-terminal",
        @"/usr/bin/foot", @"/usr/local/bin/foot",
        @"/usr/bin/alacritty", @"/usr/local/bin/alacritty",
        @"/usr/bin/kitty", @"/usr/local/bin/kitty",
        @"/usr/bin/konsole",
        @"/usr/bin/xfce4-terminal",
        @"/usr/bin/lxterminal",
        @"/usr/bin/qterminal",
        @"/usr/bin/mate-terminal",
        @"/usr/bin/weston-terminal"
    ];
    for (NSString* guestPath in genericFallbacks)
        addCandidate(guestPath);
}

- (void)scanLinuxApps:(prefix::PrefixLayout*)selected
{
    std::filesystem::path activeSysroot = [self activeSysrootForPrefix:selected];
    NSString* sysrootStr = NSStringFromPath(activeSysroot);
    NSString* shellPath = [self guestShellPathForPrefix:selected];
    std::filesystem::path shellHostPath = HostPathForGuestPath(activeSysroot, shellPath);
    std::error_code shellEc;
    if (std::filesystem::exists(shellHostPath, shellEc) ||
        std::filesystem::is_symlink(shellHostPath, shellEc)) {
        [self addShortcutWithName:@"Mini Shell"
                             path:NSStringFromPath(shellHostPath)
                         unixPath:shellPath
                         iconName:@"terminal"
                         isManual:NO
                            isLnk:NO];
    }
    [self scanLinuxTerminalApps:selected];

    NSArray* appDirs = @[
        @"usr/share/applications",
        @"usr/local/share/applications"
    ];
    for (NSString* relDir in appDirs) {
        std::filesystem::path appsDir = activeSysroot / relDir.UTF8String;
        if (PathExists(appsDir)) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(appsDir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".desktop") {
                    NSString* desktopPath = NSStringFromPath(entry.path());
                    NSDictionary* info = [self parseDesktopFile:desktopPath rootfs:sysrootStr];
                    if (info) {
                        if ([info[@"terminal"] boolValue])
                            continue;
                        NSString* appPath = info[@"path"];
                        if ([self linuxTerminalRequiresX11:appPath] &&
                            ![self guestXwaylandAvailableInPrefix:selected])
                            continue;
                        if ([appPath.lastPathComponent.lowercaseString isEqualToString:@"footclient"])
                            continue;
                        if (![[NSFileManager defaultManager] fileExistsAtPath:appPath])
                            continue;

                        [self addShortcutWithName:info[@"name"]
                                             path:appPath
                                         unixPath:desktopPath
                                         iconName:info[@"icon"]
                                         isManual:NO
                                            isLnk:NO];
                    }
                }
            }
        }
    }

    [self loadManualApps:selected];
}

- (void)scanAndroidApps:(prefix::PrefixLayout*)selected
{
    std::error_code ec;
    std::filesystem::create_directories(selected->packages_dir, ec);

    NSString* builtinDir =
        [[NSBundle mainBundle].resourcePath stringByAppendingPathComponent:@"android"];
    NSString* builtinApk =
        [builtinDir stringByAppendingPathComponent:@"muplar-launcher.apk"];
    std::filesystem::path installedApk =
        selected->packages_dir / "muplar-launcher.apk";
    std::filesystem::path installedJar =
        selected->packages_dir / "muplar-launcher-classes.jar";
    if ([[NSFileManager defaultManager] isReadableFileAtPath:builtinApk]) {
        std::filesystem::copy_file(
            builtinApk.UTF8String, installedApk,
            std::filesystem::copy_options::overwrite_existing, ec);
    }
    std::filesystem::remove(installedJar, ec);

    std::string registryText;
    for (const auto& entry :
         std::filesystem::directory_iterator(selected->packages_dir, ec)) {
        if (!entry.is_regular_file(ec) ||
            entry.path().extension() != ".apk") {
            continue;
        }

        NSString* name = NSStringFromPath(entry.path().stem());
        BOOL isBuiltinLauncher = entry.path().filename() == "muplar-launcher.apk";
        @try {
            auto apk = muplar::runtime::apk::classify_apk(entry.path());
            if (apk.manifest_application_label &&
                !apk.manifest_application_label->empty() &&
                apk.manifest_application_label->front() != '@') {
                name = NSStringFromStdString(*apk.manifest_application_label);
            } else if (apk.manifest_package && !apk.manifest_package->empty()) {
                // No hardcoded names — derive display name from the last segment of the package.
                // e.g. "com.android.launcher3" -> "Launcher3"
                //      "com.android.settings"  -> "Settings"
                std::string pkg = *apk.manifest_package;
                std::string last = pkg;
                auto dot = pkg.rfind('.');
                if (dot != std::string::npos) last = pkg.substr(dot + 1);
                if (!last.empty()) last[0] = std::toupper((unsigned char)last[0]);
                name = NSStringFromStdString(last);
            }
            // The launcher itself doesn't show as an icon in its own app
            // drawer/home screen — everything else does, as long as the
            // manifest gave us enough to build a launchable component.
            if (!isBuiltinLauncher && apk.manifest_package &&
                !apk.manifest_package->empty() &&
                apk.manifest_launch_activity &&
                !apk.manifest_launch_activity->empty()) {
                registryText += "package=" + *apk.manifest_package + "\n";
                registryText +=
                    "activity=" + *apk.manifest_launch_activity + "\n";
                registryText += "label=" + std::string(name.UTF8String) + "\n";
                registryText += "apk=" + entry.path().string() + "\n";
                registryText += "---\n";
            }
        } @catch (...) {
            // Keep malformed APKs visible so launch can report the full error.
        }

        NSString* apkPath = NSStringFromPath(entry.path());
        [self addShortcutWithName:name
                             path:apkPath
                         unixPath:apkPath
                         isManual:!isBuiltinLauncher
                            isLnk:NO];
    }

    std::filesystem::create_directories(selected->registry_dir, ec);
    std::filesystem::path registryPath =
        selected->registry_dir / "android-packages.properties";
    NSString* registryPathStr = NSStringFromPath(registryPath);
    NSError* writeError = nil;
    [[NSString stringWithUTF8String:registryText.c_str()]
        writeToFile:registryPathStr
         atomically:YES
           encoding:NSUTF8StringEncoding
              error:&writeError];
    if (writeError) {
        NSLog(@"[PrefixManager] failed to write package registry at %@: %@",
              registryPathStr, writeError);
    }
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
        [self prestartLinuxSessionIfNeeded:selected];
    } else {
        [self scanAndroidApps:selected];
    }

    [_appsTableView reloadData];
}

- (BOOL)installApkAtPath:(NSString*)unixPath
               forPrefix:(prefix::PrefixLayout*)selected
                   error:(NSError**)outError
    installedPackageName:(NSString**)outPackageName
{
    if (!selected || unixPath.length == 0) return NO;

    NSString* packagesDir = NSStringFromPath(selected->packages_dir);
    NSError* installError = nil;
    [[NSFileManager defaultManager]
        createDirectoryAtPath:packagesDir
  withIntermediateDirectories:YES
                   attributes:nil
                        error:&installError];
    if (installError) {
        if (outError) *outError = installError;
        return NO;
    }

    NSString* destination =
        [packagesDir stringByAppendingPathComponent:unixPath.lastPathComponent];
    if (![destination isEqualToString:unixPath]) {
        [[NSFileManager defaultManager] removeItemAtPath:destination error:nil];
        [[NSFileManager defaultManager] copyItemAtPath:unixPath
                                                toPath:destination
                                                 error:&installError];
        if (installError) {
            if (outError) *outError = installError;
            return NO;
        }

        NSString* sourceJar =
            [[unixPath stringByDeletingPathExtension]
                stringByAppendingString:@"-classes.jar"];
        if ([[NSFileManager defaultManager] isReadableFileAtPath:sourceJar]) {
            NSString* destinationJar =
                [[destination stringByDeletingPathExtension]
                    stringByAppendingString:@"-classes.jar"];
            [[NSFileManager defaultManager] removeItemAtPath:destinationJar
                                                       error:nil];
            [[NSFileManager defaultManager] copyItemAtPath:sourceJar
                                                    toPath:destinationJar
                                                     error:nil];
        }
    }

    std::string pkgName;
    @try {
        auto apk = muplar::runtime::apk::classify_apk(destination.fileSystemRepresentation);
        if (apk.manifest_package && !apk.manifest_package->empty()) {
            pkgName = *apk.manifest_package;
        }
    } @catch (...) {}

    if (outPackageName && !pkgName.empty()) {
        *outPackageName = [NSString stringWithUTF8String:pkgName.c_str()];
    }

    [self scanAndroidApps:selected];
    [self loadAppsForSelectedPrefix];
    return YES;
}

- (void)installApkForPrefix:(prefix::PrefixLayout*)selected
{
    if (!selected) return;

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Install";
    panel.title = @"Select Android Package (.apk)";
    panel.directoryURL = [NSURL fileURLWithPath:NSStringFromPath(selected->root)];
    if (@available(macOS 11.0, *)) {
        panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"apk"]];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        panel.allowedFileTypes = @[@"apk"];
#pragma clang diagnostic pop
    }

    if ([panel runModal] != NSModalResponseOK || !panel.URL) return;

    NSError* installError = nil;
    NSString* pkg = nil;
    if (![self installApkAtPath:panel.URL.path
                     forPrefix:selected
                         error:&installError
          installedPackageName:&pkg]) {
        [self showError:[NSString stringWithFormat:
            @"Failed to install APK: %@", installError.localizedDescription]];
    }
}

- (void)addApplication:(id)sender
{
    (void)sender;
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) return;
    if (selected->kind == prefix::PrefixKind::Android) {
        [self installApkForPrefix:selected];
        return;
    }

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Add";

    if (@available(macOS 11.0, *)) {
        if (selected->kind == prefix::PrefixKind::Wine) {
            panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"exe"]];
            panel.title = @"Select Windows Executable (.exe)";
            NSString* driveC = [NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"];
            panel.directoryURL = [NSURL fileURLWithPath:driveC];
        } else {
            panel.allowedContentTypes = @[];
            panel.title = @"Select Linux Binary";
            panel.directoryURL = [NSURL fileURLWithPath:NSStringFromPath(selected->rootfs)];
        }
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if (selected->kind == prefix::PrefixKind::Wine) {
            panel.allowedFileTypes = @[@"exe"];
            panel.title = @"Select Windows Executable (.exe)";
            NSString* driveC = [NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"];
            panel.directoryURL = [NSURL fileURLWithPath:driveC];
        } else {
            panel.allowedFileTypes = nil;
            panel.title = @"Select Linux Binary";
            panel.directoryURL = [NSURL fileURLWithPath:NSStringFromPath(selected->rootfs)];
        }
#pragma clang diagnostic pop
    }

    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        NSString* unixPath = panel.URL.path;
        if (selected->kind == prefix::PrefixKind::Wine) {
            // NSString* driveC = [NSStringFromPath(selected->rootfs) stringByAppendingPathComponent:@"drive_c"];
            // if (![unixPath hasPrefix:driveC]) {
            //     [self showError:@"Executable must be inside the instance C: drive."];
            //     return;
            // }
        } else {
            NSString* rootfs = NSStringFromPath(selected->rootfs);
            if (![unixPath hasPrefix:rootfs]) {
                [self showError:@"Binary must be inside the instance rootfs."];
                return;
            }
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

            if (selected->kind == prefix::PrefixKind::Wine) {
                NSString* winPath = [self unixToWindowsPath:unixPath forPrefix:selected];
                [self addShortcutWithName:name path:winPath unixPath:unixPath isManual:YES isLnk:NO];
            } else {
                // Standardize so saveManualApps can reliably strip the sysroot prefix
                NSString* canonical = unixPath.stringByStandardizingPath;
                [self addShortcutWithName:name path:canonical unixPath:canonical isManual:YES isLnk:NO];
            }
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
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (selected && selected->kind == prefix::PrefixKind::Android) {
        NSString* deviceKey = [self androidDeviceKeyForPrefix:selected];
        NSString* activeKey = _androidSessionAppKeys[deviceKey];
        if (activeKey && [activeKey isEqualToString:key]) {
            if ([_stoppingAndroidSessionKeys containsObject:deviceKey])
                return @"Stopping...";
            NSTask* task = _androidSessionTasks[deviceKey];
            if (task && task.isRunning)
                return @"Running";
        }
        if ([_launchingAppPaths containsObject:key])
            return @"Launching...";
        return @"Stopped";
    }
    if ([_launchingAppPaths containsObject:key]) {
        return @"Launching...";
    }
    NSTask* task = _runningTasks[key];
    if (task && task.isRunning) {
        return @"Running";
    }
    return @"Stopped";
}

- (void)waitForLinuxWindowForKey:(NSString*)key
                           token:(NSUUID*)token
           baselineWindowNumbers:(NSSet<NSNumber*>*)baselineWindowNumbers
                         attempts:(NSUInteger)attempts
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
        if (![self->_linuxLaunchTokens[key] isEqual:token])
            return;

        NSTask* task = self->_runningTasks[key];
        if (!task || !task.isRunning)
            return;

        if (attempts >= 600) {
            [self->_linuxLaunchTokens removeObjectForKey:key];
            [self->_launchingAppPaths removeObject:key];
            [self->_appsTableView reloadData];
            kill(-task.processIdentifier, SIGKILL);
            [self showError:@"The Linux application did not create a window within 60 seconds and was stopped."];
            return;
        }

        NSMutableSet<NSNumber*>* newWindowNumbers =
            [VisibleMuplarWaylandWindowNumbers() mutableCopy];
        [newWindowNumbers minusSet:baselineWindowNumbers];
        if (newWindowNumbers.count > 0) {
            [self->_linuxLaunchTokens removeObjectForKey:key];
            [self->_launchingAppPaths removeObject:key];
            [self->_appsTableView reloadData];
            return;
        }

        [self waitForLinuxWindowForKey:key
                                 token:token
                 baselineWindowNumbers:baselineWindowNumbers
                               attempts:attempts + 1];
    });
}

- (NSString* )mupBinPath
{
    NSString* macosPath = [[NSBundle mainBundle] executablePath].stringByDeletingLastPathComponent;
    NSString* mupInBundle = [macosPath stringByAppendingPathComponent:@"mup"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:mupInBundle]) {
        return mupInBundle;
    }
    NSString* parentDir = [[NSBundle mainBundle] bundlePath].stringByDeletingLastPathComponent;
    NSString* mupInParent = [parentDir stringByAppendingPathComponent:@"mup"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:mupInParent]) {
        return mupInParent;
    }
    return nil;
}

- (NSString*)angleLibraryDir
{
    NSString* bundleAngle = [[[NSBundle mainBundle] privateFrameworksPath]
        stringByAppendingPathComponent:@"angle"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:bundleAngle])
        return bundleAngle;

    NSString* workspaceRoot = FindWorkspaceRoot();
    if (workspaceRoot) {
        NSString* devAngle = [workspaceRoot
            stringByAppendingPathComponent:@"third_party/angle-bin"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:devAngle])
            return devAngle;
    }
    return nil;
}

- (BOOL)waitForMuplarWaylandSocket
{
    NSString* socketPath = [DefaultMuplarWaylandRuntimeDir()
        stringByAppendingPathComponent:DefaultMuplarWaylandDisplayName()];
    for (int i = 0; i < 20; ++i) {
        if (UnixSocketAcceptsConnection(socketPath))
            return YES;
        usleep(100000);
    }
    return UnixSocketAcceptsConnection(socketPath);
}

- (BOOL)ensureMuplarWaylandForLinuxPrefix:(prefix::PrefixLayout*)selected
                      errorMessage:(NSString**)errorMessage
{
    NSString* socketPath = [DefaultMuplarWaylandRuntimeDir()
        stringByAppendingPathComponent:DefaultMuplarWaylandDisplayName()];
    [self appendLaunchLogLineForPrefix:selected
                                  line:[NSString stringWithFormat:
                                      @"[muplar-wayland] ensure start socket=%@",
                                      socketPath]];
    if (!_supervisor)
        _supervisor = std::make_unique<supervisor::SupervisorService>();
    if (!_supervisor->is_running()) {
        [self appendLaunchLogLineForPrefix:selected
                                      line:@"[muplar-wayland] supervisor not running; starting"];
        _supervisor->start();
    } else {
        [self appendLaunchLogLineForPrefix:selected
                                      line:@"[muplar-wayland] supervisor already running"];
    }

    BOOL waylandReady = _supervisor->wayland().wait_for_socket(5000);
    [self appendLaunchLogLineForPrefix:selected
                                  line:[NSString stringWithFormat:
                                      @"[muplar-wayland] initial socket ready=%@",
                                      waylandReady ? @"yes" : @"no"]];
    if (!waylandReady) {
        log_warn("[Muplar Linux] Muplar Wayland socket is not live at %s; restarting "
                 "compositor",
                 socketPath.UTF8String);
        [self appendLaunchLogLineForPrefix:selected
                                      line:@"[muplar-wayland] restarting compositor"];
        _supervisor->wayland().stop();
        _supervisor->wayland().start();
        waylandReady = _supervisor->wayland().wait_for_socket(5000);
        [self appendLaunchLogLineForPrefix:selected
                                      line:[NSString stringWithFormat:
                                          @"[muplar-wayland] restart socket ready=%@",
                                          waylandReady ? @"yes" : @"no"]];
    }
    if (!waylandReady) {
        if (errorMessage) {
            *errorMessage = [NSString stringWithFormat:
                @"Muplar display compositor did not create a live Wayland socket at %@.",
                socketPath];
        }
        return NO;
    }

    return YES;
}

- (void)prestartLinuxSessionIfNeeded:(prefix::PrefixLayout*)selected
{
    if (!selected || selected->kind != prefix::PrefixKind::Linux)
        return;

    NSString* key = NSStringFromStdString(selected->name);
    NSTask* existing = _linuxSessionTasks[key];
    if (existing && existing.isRunning)
        return;

    if (!_supervisor)
        _supervisor = std::make_unique<supervisor::SupervisorService>();
    if (!_supervisor->is_running())
        _supervisor->start();

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        BOOL waylandReady = self->_supervisor->wayland().wait_for_socket(5000);
        if (waylandReady) {
            dispatch_async(dispatch_get_main_queue(), ^{
                NSString* sessionError = nil;
                [self ensureLinuxSessionForPrefix:selected errorMessage:&sessionError];
            });
        }
    });
}

- (BOOL)ensureLinuxSessionForPrefix:(prefix::PrefixLayout*)selected
                       errorMessage:(NSString**)errorMessage
{
    if (!selected || selected->kind != prefix::PrefixKind::Linux)
        return NO;

    NSString* key = NSStringFromStdString(selected->name);
    NSTask* existing = _linuxSessionTasks[key];
    if (existing && existing.isRunning) {
        std::filesystem::path dbusBinary = selected->rootfs / "usr/bin/dbus-daemon";
        std::filesystem::path dbusConfig =
            selected->rootfs / "etc/dbus-1/muplar-session.conf";
        std::filesystem::path dbusAddress =
            selected->rootfs / "tmp/muplar-session/dbus-address";
        std::error_code ec;
        bool dbusExpected = std::filesystem::exists(dbusBinary, ec) &&
                            std::filesystem::exists(dbusConfig, ec);
        bool dbusReady = false;
        if (std::filesystem::exists(dbusAddress, ec)) {
            ec.clear();
            auto addressSize = std::filesystem::file_size(dbusAddress, ec);
            dbusReady = !ec && addressSize > 0;
        }
        if (!dbusExpected || dbusReady)
            return YES;

        // A session can be prestarted while default packages are still being
        // installed. Replace that session once D-Bus becomes available.
        [existing terminate];
        for (int i = 0; i < 20 && existing.isRunning; ++i)
            usleep(50000);
        if (existing.isRunning)
            kill(existing.processIdentifier, SIGKILL);
        [_linuxSessionTasks removeObjectForKey:key];
    }

    NSString* mupBin = [self mupBinPath];
    if (!mupBin || ![[NSFileManager defaultManager] isExecutableFileAtPath:mupBin]) {
        if (errorMessage)
            *errorMessage = @"mup binary is unavailable for the Linux session launcher.";
        return NO;
    }

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = mupBin;
    task.arguments = @[@"--quiet", @"--prefix", key,
                       @"/usr/local/libexec/muplar-session-launcher"];
    NSMutableDictionary<NSString*, NSString*>* env =
        [NSProcessInfo.processInfo.environment mutableCopy];
    ApplyDefaultLinuxDisplayEnvironment(env);
    env[@"ELFUSE_GUEST_UID"] = @"1000";
    env[@"ELFUSE_GUEST_GID"] = @"1000";
    // Arms the sudo shim. elfuse reads this once at process start and hands it
    // to fork children, so it belongs on the host environment here rather than
    // exported from inside the guest session.
    env[@"ELFUSE_FAKEROOT_EXEC"] = @(prefix::linux_fakeroot_exec_guest_path());
    env[@"MUPLAR_APP_PROCESS_GROUP"] = @"1";
    env[@"MUPLAR_SESSION_MAP_DIR"] =
        [NSStringFromPath(selected->rootfs / "tmp" / "muplar-session")
            stringByAppendingPathComponent:@"host-pids"];
    task.environment = env;
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    task.terminationHandler = ^(NSTask* finished) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (self->_linuxSessionTasks[key] == finished)
                [self->_linuxSessionTasks removeObjectForKey:key];
        });
    };

    @try {
        [task launch];
        _linuxSessionTasks[key] = task;
        return YES;
    } @catch (NSException* ex) {
        if (errorMessage) {
            *errorMessage = [NSString stringWithFormat:
                @"Failed to start Linux session launcher: %@", ex.reason];
        }
        return NO;
    }
}

- (NSString*)linuxProvisionerScriptPath
{
    NSString* resourceScript = [[[[NSBundle mainBundle] resourcePath]
                                    stringByAppendingPathComponent:@"tools"]
                                    stringByAppendingPathComponent:@"provision-linux-rootfs.sh"];
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:resourceScript]) {
        return resourceScript;
    }

    NSString* workspaceRoot = FindWorkspaceRoot();
    if (workspaceRoot) {
        NSString* devScript = [[workspaceRoot stringByAppendingPathComponent:@"tools"]
                                  stringByAppendingPathComponent:@"provision-linux-rootfs.sh"];
        if ([[NSFileManager defaultManager] isExecutableFileAtPath:devScript]) {
            return devScript;
        }
    }
    return nil;
}

- (NSString*)linuxRootfsCachePath
{
    NSString* home = NSHomeDirectory();
    return [[[home stringByAppendingPathComponent:@".muplar"]
              stringByAppendingPathComponent:@"cache"]
              stringByAppendingPathComponent:@"linux-rootfs"];
}

- (NSString*)shortTaskOutputFromData:(NSData*)data
{
    NSString* output = [[NSString alloc] initWithData:data
                                             encoding:NSUTF8StringEncoding];
    if (output.length == 0)
        return @"";
    output = [output stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    static const NSUInteger kMaxLength = 6000;
    if (output.length > kMaxLength) {
        output = [@"...\n" stringByAppendingString:
            [output substringFromIndex:output.length - kMaxLength]];
    }
    return output;
}

- (void)writeProvisionLog:(NSString*)output targetRoot:(NSString*)targetRoot
{
    if (output.length == 0 || targetRoot.length == 0)
        return;

    NSString* logDir = [targetRoot stringByAppendingPathComponent:@"logs"];
    [[NSFileManager defaultManager] createDirectoryAtPath:logDir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString* logPath = [logDir stringByAppendingPathComponent:@"provision-linux-rootfs.log"];
    [output writeToFile:logPath
             atomically:YES
               encoding:NSUTF8StringEncoding
                  error:nil];
}

- (BOOL)provisionLinuxInstanceName:(NSString*)name
                             distro:(NSString*)distro
                               arch:(NSString*)arch
                         targetRoot:(NSString*)targetRoot
                            sysroot:(NSString*)sysroot
                       errorMessage:(NSString**)errorMessage
{
    NSString* script = [self linuxProvisionerScriptPath];
    if (!script) {
        if (errorMessage)
            *errorMessage = @"Linux rootfs provisioner was not found in the app bundle.";
        return NO;
    }

    NSString* mupBin = [self mupBinPath];
    if (!mupBin) {
        if (errorMessage)
            *errorMessage = @"mup binary not found. Build the app bundle first.";
        return NO;
    }

    NSMutableArray<NSString*>* args = [NSMutableArray arrayWithArray:@[
        script,
        @"--prefix", name,
        @"--distro", distro.lowercaseString,
        @"--arch", arch,
        @"--root", targetRoot,
        @"--download",
        @"--base-only"
    ]];
    if (sysroot.length > 0) {
        [args addObjectsFromArray:@[@"--sysroot", sysroot]];
    }

    NSMutableDictionary<NSString*, NSString*>* env =
        [NSProcessInfo.processInfo.environment mutableCopy];
    env[@"MUP"] = mupBin;
    env[@"MUPLAR_ROOTFS_CACHE"] = [self linuxRootfsCachePath];
    NSString* existingPath = env[@"PATH"] ?: @"";
    NSString* finderSafePath = @"/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    env[@"PATH"] = existingPath.length > 0
        ? [finderSafePath stringByAppendingFormat:@":%@", existingPath]
        : finderSafePath;

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = @"/bin/bash";
    task.arguments = args;
    task.environment = env;

    NSString* tempLog = [NSTemporaryDirectory()
        stringByAppendingPathComponent:
            [NSString stringWithFormat:@"muplar-provision-%@.log",
                                       [[NSUUID UUID] UUIDString]]];
    [[NSFileManager defaultManager] createFileAtPath:tempLog
                                            contents:nil
                                          attributes:nil];
    NSFileHandle* logHandle = [NSFileHandle fileHandleForWritingAtPath:tempLog];
    if (!logHandle) {
        if (errorMessage)
            *errorMessage = @"Unable to create a temporary provisioner log file.";
        return NO;
    }
    task.standardOutput = logHandle;
    task.standardError = logHandle;

    @try {
        [task launch];
        [task waitUntilExit];
    } @catch (NSException* ex) {
        [logHandle closeFile];
        [[NSFileManager defaultManager] removeItemAtPath:tempLog error:nil];
        if (errorMessage)
            *errorMessage = [NSString stringWithFormat:@"Failed to start Linux provisioner: %@", ex.reason];
        return NO;
    }

    [logHandle closeFile];
    NSData* data = [NSData dataWithContentsOfFile:tempLog];
    [[NSFileManager defaultManager] removeItemAtPath:tempLog error:nil];
    NSString* output = [self shortTaskOutputFromData:data];
    [self writeProvisionLog:output targetRoot:targetRoot];

    if (task.terminationStatus != 0) {
        if (errorMessage) {
            *errorMessage = [NSString stringWithFormat:
                @"Linux provisioning failed with exit code %d.%@%@",
                task.terminationStatus,
                output.length > 0 ? @"\n\n" : @"",
                output.length > 0 ? output : @""];
        }
        return NO;
    }
    return YES;
}

- (BOOL)androidArtSysrootReadyAtPath:(NSString*)sysroot
{
    NSString* checkScript = BundledOrWorkspaceTool(@"check-android-art-sysroot.sh");
    if (!checkScript || sysroot.length == 0)
        return NO;

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = @"/bin/zsh";
    task.arguments = @[checkScript, @"--sysroot", sysroot, @"--quiet"];
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];

    @try {
        [task launch];
        [task waitUntilExit];
    } @catch (NSException* __unused ex) {
        return NO;
    }
    return task.terminationReason == NSTaskTerminationReasonExit &&
           task.terminationStatus == 0;
}

- (NSString*)ensureAndroidArtSysrootForInstanceName:(NSString*)name
                                       errorMessage:(NSString**)errorMessage
{
    NSString* script = BundledOrWorkspaceTool(@"ensure-android-art-sysroot.sh");
    if (!script) {
        if (errorMessage)
            *errorMessage = @"Android ART sysroot helper was not found in the app bundle.";
        return nil;
    }

    NSString* sysroot = DefaultAndroidArtSysrootPath();
    if ([self androidArtSysrootReadyAtPath:sysroot])
        return sysroot;

    __block NSWindow* console = nil;
    __block NSTextField* status = nil;
    __block NSProgressIndicator* spinner = nil;
    __block NSTextView* outputView = nil;

    void (^createConsole)(void) = ^{
        console = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 760, 480)
                      styleMask:NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable |
                                NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        console.title = [NSString stringWithFormat:@"Preparing Android Sysroot - %@", name ?: @"Android"];
        console.releasedWhenClosed = NO;

        NSView* content = console.contentView;
        status = [NSTextField labelWithString:
            @"Downloading and preparing the shared Android runtime sysroot..."];
        status.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
        status.translatesAutoresizingMaskIntoConstraints = NO;

        spinner = [[NSProgressIndicator alloc] init];
        spinner.style = NSProgressIndicatorStyleSpinning;
        spinner.controlSize = NSControlSizeSmall;
        spinner.translatesAutoresizingMaskIntoConstraints = NO;
        [spinner startAnimation:nil];

        outputView = [[NSTextView alloc] initWithFrame:NSZeroRect];
        outputView.editable = NO;
        outputView.selectable = YES;
        outputView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        outputView.textColor = NSColor.textColor;
        outputView.backgroundColor = NSColor.textBackgroundColor;
        outputView.insertionPointColor = NSColor.textColor;
        outputView.autoresizingMask = NSViewWidthSizable;
        outputView.verticallyResizable = YES;
        outputView.horizontallyResizable = NO;
        outputView.minSize = NSMakeSize(0, 0);
        outputView.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
        outputView.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
        outputView.textContainer.widthTracksTextView = YES;
        outputView.string = @"Starting Android sysroot setup...\n";

        NSScrollView* scroll = [[NSScrollView alloc] init];
        scroll.translatesAutoresizingMaskIntoConstraints = NO;
        scroll.hasVerticalScroller = YES;
        scroll.hasHorizontalScroller = YES;
        scroll.autohidesScrollers = YES;
        scroll.borderType = NSBezelBorder;
        scroll.documentView = outputView;
        outputView.frame = scroll.contentView.bounds;

        NSButton* actionButton = [NSButton buttonWithTitle:@"Close"
                                                    target:console
                                                    action:@selector(performClose:)];
        actionButton.bezelStyle = NSBezelStyleRounded;
        actionButton.translatesAutoresizingMaskIntoConstraints = NO;

        [content addSubview:spinner];
        [content addSubview:status];
        [content addSubview:scroll];
        [content addSubview:actionButton];
        [NSLayoutConstraint activateConstraints:@[
            [spinner.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
            [spinner.centerYAnchor constraintEqualToAnchor:status.centerYAnchor],
            [spinner.widthAnchor constraintEqualToConstant:16],
            [spinner.heightAnchor constraintEqualToConstant:16],
            [status.leadingAnchor constraintEqualToAnchor:spinner.trailingAnchor constant:8],
            [status.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
            [status.topAnchor constraintEqualToAnchor:content.topAnchor constant:14],
            [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
            [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
            [scroll.topAnchor constraintEqualToAnchor:status.bottomAnchor constant:12],
            [scroll.bottomAnchor constraintEqualToAnchor:actionButton.topAnchor constant:-12],
            [actionButton.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
            [actionButton.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-14],
            [actionButton.widthAnchor constraintEqualToConstant:88],
        ]];

        [console center];
        [console makeKeyAndOrderFront:nil];
    };

    if ([NSThread isMainThread])
        createConsole();
    else
        dispatch_sync(dispatch_get_main_queue(), createConsole);

    NSMutableDictionary<NSString*, NSString*>* env =
        [NSProcessInfo.processInfo.environment mutableCopy];
    NSString* finderSafePath = @"/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    NSString* existingPath = env[@"PATH"] ?: @"";
    env[@"PATH"] = existingPath.length > 0
        ? [finderSafePath stringByAppendingFormat:@":%@", existingPath]
        : finderSafePath;

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = @"/bin/zsh";
    task.arguments = @[script, @"--sysroot", sysroot];
    task.environment = env;
    task.standardInput = [NSFileHandle fileHandleWithNullDevice];

    NSPipe* pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    NSMutableData* outputData = [NSMutableData data];

    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle* handle) {
        NSData* data = [handle availableData];
        if (data.length == 0) {
            handle.readabilityHandler = nil;
            return;
        }
        @synchronized (outputData) {
            [outputData appendData:data];
        }
        NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (text.length == 0)
            return;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSDictionary<NSAttributedStringKey, id>* attributes = @{
                NSFontAttributeName: outputView.font,
                NSForegroundColorAttributeName: outputView.textColor,
            };
            [outputView.textStorage appendAttributedString:
                [[NSAttributedString alloc] initWithString:text
                                                attributes:attributes]];
            [outputView scrollRangeToVisible:NSMakeRange(outputView.string.length, 0)];
        });
    };

    @try {
        [task launch];
        [task waitUntilExit];
    } @catch (NSException* ex) {
        pipe.fileHandleForReading.readabilityHandler = nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            [spinner stopAnimation:nil];
            spinner.hidden = YES;
            status.stringValue = @"Android sysroot setup failed";
        });
        if (errorMessage)
            *errorMessage = [NSString stringWithFormat:
                @"Failed to start Android sysroot setup: %@", ex.reason];
        return nil;
    }

    pipe.fileHandleForReading.readabilityHandler = nil;
    NSData* data = nil;
    @synchronized (outputData) {
        data = [outputData copy];
    }
    NSString* output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
    BOOL success = task.terminationReason == NSTaskTerminationReasonExit &&
                   task.terminationStatus == 0;
    dispatch_async(dispatch_get_main_queue(), ^{
        [spinner stopAnimation:nil];
        spinner.hidden = YES;
        status.stringValue = success
            ? @"Android sysroot is ready"
            : [NSString stringWithFormat:@"Android sysroot setup failed (exit %d)",
                                         task.terminationStatus];
    });

    if (!success) {
        if (errorMessage) {
            NSString* detail = [output stringByTrimmingCharactersInSet:
                NSCharacterSet.whitespaceAndNewlineCharacterSet];
            *errorMessage = detail.length > 0
                ? [NSString stringWithFormat:
                    @"Android ART sysroot setup failed:\n%@", detail]
                : @"Android ART sysroot setup failed.";
        }
        return nil;
    }
    return sysroot;
}

- (void)launchLinuxDefaultPackageInstallForName:(NSString*)name
                                         distro:(NSString*)distro
                                            arch:(NSString*)arch
                                      targetRoot:(NSString*)targetRoot
{
    NSString* script = [self linuxProvisionerScriptPath];
    NSString* mupBin = [self mupBinPath];
    if (!script || !mupBin) {
        [self showError:@"Default packages could not be started because the provisioner or mup binary is missing."];
        return;
    }

    NSWindow* console = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 760, 480)
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    console.title = [NSString stringWithFormat:@"Installing Defaults - %@", name];
    console.releasedWhenClosed = NO;

    NSView* content = console.contentView;
    NSTextField* status = [NSTextField labelWithString:
        @"Installing the default terminal and supporting packages..."];
    status.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    status.translatesAutoresizingMaskIntoConstraints = NO;

    NSProgressIndicator* spinner = [[NSProgressIndicator alloc] init];
    spinner.style = NSProgressIndicatorStyleSpinning;
    spinner.controlSize = NSControlSizeSmall;
    spinner.translatesAutoresizingMaskIntoConstraints = NO;
    [spinner startAnimation:nil];

    NSTextView* outputView = [[NSTextView alloc] initWithFrame:NSZeroRect];
    outputView.editable = NO;
    outputView.selectable = YES;
    outputView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    outputView.textColor = NSColor.textColor;
    outputView.backgroundColor = NSColor.textBackgroundColor;
    outputView.insertionPointColor = NSColor.textColor;
    outputView.autoresizingMask = NSViewWidthSizable;
    outputView.verticallyResizable = YES;
    outputView.horizontallyResizable = NO;
    outputView.minSize = NSMakeSize(0, 0);
    outputView.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    outputView.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    outputView.textContainer.widthTracksTextView = YES;
    outputView.string = @"Starting package installation...\n";

    NSScrollView* scroll = [[NSScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSBezelBorder;
    scroll.documentView = outputView;
    outputView.frame = scroll.contentView.bounds;

    NSButton* actionButton = [NSButton buttonWithTitle:@"Close"
                                                target:console
                                                action:@selector(performClose:)];
    actionButton.bezelStyle = NSBezelStyleRounded;
    actionButton.translatesAutoresizingMaskIntoConstraints = NO;

    [content addSubview:spinner];
    [content addSubview:status];
    [content addSubview:scroll];
    [content addSubview:actionButton];
    [NSLayoutConstraint activateConstraints:@[
        [spinner.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [spinner.centerYAnchor constraintEqualToAnchor:status.centerYAnchor],
        [spinner.widthAnchor constraintEqualToConstant:16],
        [spinner.heightAnchor constraintEqualToConstant:16],
        [status.leadingAnchor constraintEqualToAnchor:spinner.trailingAnchor constant:8],
        [status.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [status.topAnchor constraintEqualToAnchor:content.topAnchor constant:14],
        [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [scroll.topAnchor constraintEqualToAnchor:status.bottomAnchor constant:12],
        [scroll.bottomAnchor constraintEqualToAnchor:actionButton.topAnchor constant:-12],
        [actionButton.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [actionButton.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-14],
        [actionButton.widthAnchor constraintEqualToConstant:88],
    ]];

    NSMutableDictionary<NSString*, NSString*>* env =
        [NSProcessInfo.processInfo.environment mutableCopy];
    env[@"MUP"] = mupBin;
    env[@"MUPLAR_ROOTFS_CACHE"] = [self linuxRootfsCachePath];
    NSString* finderSafePath = @"/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    NSString* existingPath = env[@"PATH"] ?: @"";
    env[@"PATH"] = existingPath.length > 0
        ? [finderSafePath stringByAppendingFormat:@":%@", existingPath]
        : finderSafePath;

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = @"/bin/bash";
    task.arguments = @[script, @"--prefix", name,
                       @"--distro", distro.lowercaseString,
                       @"--arch", arch, @"--packages-only"];
    task.environment = env;
    NSPipe* pipe = [NSPipe pipe];
    task.standardInput = [NSFileHandle fileHandleWithNullDevice];
    task.standardOutput = pipe;
    task.standardError = pipe;

    NSString* logDir = [targetRoot stringByAppendingPathComponent:@"logs"];
    [[NSFileManager defaultManager] createDirectoryAtPath:logDir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString* logPath = [logDir stringByAppendingPathComponent:@"default-packages.log"];
    [[NSFileManager defaultManager] createFileAtPath:logPath contents:[NSData data] attributes:nil];
    __block NSFileHandle* logHandle = [NSFileHandle fileHandleForWritingAtPath:logPath];
    [logHandle truncateFileAtOffset:0];
    __block unsigned long long logBytes = 0;

    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle* handle) {
        NSData* data = [handle availableData];
        if (data.length == 0) {
            handle.readabilityHandler = nil;
            return;
        }
        @try {
            if (logBytes + data.length >= kMaxLogFileBytes) {
                [logHandle closeFile];
                RotateLogFileIfNeeded(logPath);
                logHandle = OpenRotatingLogForAppend(logPath);
                NSData* marker = LogRolloverMarkerData();
                [logHandle writeData:marker];
                logBytes = LogFileSize(logPath);
            }
            [logHandle writeData:data];
            logBytes += data.length;
        } @catch (NSException* __unused e) {}
        NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (text.length == 0)
            return;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSDictionary<NSAttributedStringKey, id>* attributes = @{
                NSFontAttributeName: outputView.font,
                NSForegroundColorAttributeName: outputView.textColor,
            };
            [outputView.textStorage appendAttributedString:
                [[NSAttributedString alloc] initWithString:text
                                                attributes:attributes]];
            [outputView scrollRangeToVisible:NSMakeRange(outputView.string.length, 0)];
        });
    };

    task.terminationHandler = ^(NSTask* finished) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [spinner stopAnimation:nil];
            spinner.hidden = YES;
            BOOL success = finished.terminationReason == NSTaskTerminationReasonExit &&
                           finished.terminationStatus == 0;
            status.stringValue = success
                ? @"Default packages installed"
                : [NSString stringWithFormat:@"Package installation stopped (exit %d)",
                                                   finished.terminationStatus];
            if (self->_packageInstallTasks[name] == finished)
                [self->_packageInstallTasks removeObjectForKey:name];
            [self loadAppsForSelectedPrefix];
        });
        @try { [logHandle closeFile]; } @catch (NSException* __unused e) {}
    };

    @try {
        [task launch];
        _packageInstallTasks[name] = task;
        [console center];
        [console makeKeyAndOrderFront:nil];
    } @catch (NSException* ex) {
        pipe.fileHandleForReading.readabilityHandler = nil;
        [logHandle closeFile];
        [console close];
        [self showError:[NSString stringWithFormat:
            @"Failed to start default package installation: %@", ex.reason]];
    }
}

- (BOOL)isTerminalShortcut:(MuplarAppShortcut*)app
{
    NSString* lowerName = app.name.lowercaseString;
    NSString* lowerPath = app.path.lowercaseString;
    return [lowerName containsString:@"terminal"] ||
           [lowerName containsString:@"console"] ||
           [lowerName isEqualToString:@"xterm"] ||
           [lowerName isEqualToString:@"uxterm"] ||
           [lowerName containsString:@"command prompt"] ||
           [lowerPath isEqualToString:@"cmd"] ||
           [lowerPath hasSuffix:@"/x-terminal-emulator"] ||
           [lowerPath hasSuffix:@"/xterm"] ||
           [lowerPath hasSuffix:@"/uxterm"] ||
           [lowerPath hasSuffix:@"/kgx"] ||
           [lowerPath hasSuffix:@"/foot"] ||
           [lowerPath hasSuffix:@"/alacritty"] ||
           [lowerPath hasSuffix:@"/kitty"] ||
           [lowerPath hasSuffix:@"/konsole"] ||
           [lowerPath hasSuffix:@"/bash"] ||
           [lowerPath hasSuffix:@"/sh"] ||
           [lowerPath hasSuffix:@"/zsh"] ||
           [lowerPath hasSuffix:@"/dash"] ||
           [lowerPath hasSuffix:@"\\cmd.exe"];
}

- (BOOL)isLinuxShellShortcut:(MuplarAppShortcut*)app
{
    NSString* lowerName = app.name.lowercaseString;
    NSString* lowerPath = app.path.lowercaseString;
    return [lowerName isEqualToString:@"sh"] ||
           [lowerName isEqualToString:@"bash"] ||
           [lowerName isEqualToString:@"zsh"] ||
           [lowerName isEqualToString:@"dash"] ||
           [lowerName isEqualToString:@"fish"] ||
           [lowerPath hasSuffix:@"/bash"] ||
           [lowerPath hasSuffix:@"/sh"] ||
           [lowerPath hasSuffix:@"/zsh"] ||
           [lowerPath hasSuffix:@"/dash"] ||
           [lowerPath hasSuffix:@"/fish"];
}

- (BOOL)isWineCommandPromptShortcut:(MuplarAppShortcut*)app
{
    NSString* lowerName = app.name.lowercaseString;
    NSString* lowerPath = app.path.lowercaseString;
    return [lowerName containsString:@"command prompt"] ||
           [lowerPath isEqualToString:@"cmd"] ||
           [lowerPath isEqualToString:@"cmd.exe"] ||
           [lowerPath hasSuffix:@"\\cmd.exe"];
}

- (void)writeLaunchLogHeaderForPrefix:(prefix::PrefixLayout*)selected appName:(NSString*)appName
{
    NSString* logDir = NSStringFromPath(selected->logs_dir);
    [[NSFileManager defaultManager] createDirectoryAtPath:logDir withIntermediateDirectories:YES attributes:nil error:nil];

    NSString* logPath = NSStringFromPath(muplar::runtime::prefix::main_log_path(*selected));
    NSFileHandle* logHandle = OpenRotatingLogForAppend(logPath);
    if (logHandle) {
        NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
        formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
        NSString* timestamp = [formatter stringFromDate:[NSDate date]];

        NSString* header = [NSString stringWithFormat:@"\n=== [%@] Launching app: %@ ===\n", timestamp, appName];
        [logHandle writeData:[header dataUsingEncoding:NSUTF8StringEncoding]];
        [logHandle closeFile];
    }
}

- (void)appendLaunchLogLineForPrefix:(prefix::PrefixLayout*)selected
                                line:(NSString*)line
{
    if (!selected || line.length == 0)
        return;

    NSString* logDir = NSStringFromPath(selected->logs_dir);
    [[NSFileManager defaultManager] createDirectoryAtPath:logDir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];

    NSString* logPath =
        NSStringFromPath(muplar::runtime::prefix::main_log_path(*selected));
    NSFileHandle* logHandle = OpenRotatingLogForAppend(logPath);
    if (!logHandle)
        return;

    NSString* message = [line hasSuffix:@"\n"] ? line
                                               : [line stringByAppendingString:@"\n"];
    [logHandle writeData:[message dataUsingEncoding:NSUTF8StringEncoding]];
    [logHandle closeFile];
}

- (void)setupLoggingForTask:(NSTask*)task prefix:(prefix::PrefixLayout*)selected appName:(NSString*)appName
{
    if (!selected || !selected->logging) {
        task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
        task.standardError = [NSFileHandle fileHandleWithNullDevice];
        return;
    }

    // Write a header to the per-prefix log
    [self writeLaunchLogHeaderForPrefix:selected appName:appName];

    // Use NSPipe to capture output and write to per-prefix log with flushing
    NSPipe* pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;

    // Also write to the per-prefix log
    NSString* prefixLogPath = NSStringFromPath(muplar::runtime::prefix::main_log_path(*selected));
    __block NSFileHandle* prefixHandle = OpenRotatingLogForAppend(prefixLogPath);
    __block unsigned long long prefixLogBytes = LogFileSize(prefixLogPath);

    prefix::PrefixKind kind = selected->kind;

    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle* handle) {
        NSData* data = [handle availableData];
        if (data.length == 0) {
            handle.readabilityHandler = nil;
            if (prefixHandle) {
                @try { [prefixHandle closeFile]; } @catch (NSException* __unused e) {}
            }
            return;
        }
        if (kind == prefix::PrefixKind::Wine) {
            data = SanitizeWindowsCompatibilityLogData(data);
        }
        if (prefixHandle) {
            @try {
                if (prefixLogBytes + data.length >= kMaxLogFileBytes) {
                    [prefixHandle closeFile];
                    RotateLogFileIfNeeded(prefixLogPath);
                    prefixHandle = OpenRotatingLogForAppend(prefixLogPath);
                    NSData* marker = LogRolloverMarkerData();
                    [prefixHandle writeData:marker];
                    prefixLogBytes = LogFileSize(prefixLogPath);
                }
                [prefixHandle writeData:data];
                prefixLogBytes += data.length;
            } @catch (NSException* __unused e) {}
        }
    };
}



- (NSString*)guestPathFromHostPath:(NSString*)hostPath prefix:(prefix::PrefixLayout*)selected
{
    // Strip the host active sysroot path if it is present to get the guest-relative path
    std::filesystem::path activeSysroot = [self activeSysrootForPrefix:selected];
    NSString* sysrootStr = NSStringFromPath(activeSysroot).stringByStandardizingPath;
    NSString* stdHostPath = hostPath.stringByStandardizingPath;
    if ([stdHostPath.lowercaseString hasPrefix:sysrootStr.lowercaseString]) {
        NSString* rel = [stdHostPath substringFromIndex:sysrootStr.length];
        if (rel.length == 0 || [rel characterAtIndex:0] != '/') {
            rel = [@"/" stringByAppendingString:rel];
        }
        return rel;
    }
    return hostPath;
}

- (void)runCommandInTerminal:(NSString*)commandString prefix:(prefix::PrefixLayout*)selected app:(MuplarAppShortcut*)app
{
    NSString* logDir = NSStringFromPath(selected->logs_dir);
    [[NSFileManager defaultManager] createDirectoryAtPath:logDir withIntermediateDirectories:YES attributes:nil error:nil];

    NSMutableString* safeAppName = [app.name mutableCopy];
    NSRegularExpression* regex = [NSRegularExpression regularExpressionWithPattern:@"[^a-zA-Z0-9_-]" options:0 error:nil];
    [regex replaceMatchesInString:safeAppName options:0 range:NSMakeRange(0, safeAppName.length) withTemplate:@"_"];
    NSString* scriptName = [NSString stringWithFormat:@"launch_%@.command", safeAppName];
    NSString* scriptPath = [logDir stringByAppendingPathComponent:scriptName];

    NSString* fileContent = [NSString stringWithFormat:
        @"#!/bin/sh\n"
        @"%@"
        @"clear\n"
        @"%@\n",
        HostDisplayEnvironmentExportScript(),
        commandString];

    NSError* error = nil;
    [fileContent writeToFile:scriptPath atomically:YES encoding:NSUTF8StringEncoding error:&error];
    if (error) {
        [self showError:[NSString stringWithFormat:@"Failed to write launch script: %@", error.localizedDescription]];
        return;
    }

    // chmod +x
    int rc = chmod(scriptPath.UTF8String, 0755);
    if (rc != 0) {
        [self showError:@"Failed to make launch script executable."];
        return;
    }

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = @"/usr/bin/open";
    task.arguments = @[@"-n", @"-a", @"Terminal", scriptPath];

    @try {
        [task launch];
    } @catch (NSException* exception) {
        [self showError:[NSString stringWithFormat:@"Failed to launch Terminal: %@", exception.reason]];
    }
}

- (BOOL)guestExecutableExists:(NSString*)guestPath prefix:(prefix::PrefixLayout*)selected
{
    std::filesystem::path activeSysroot = [self activeSysrootForPrefix:selected];
    std::filesystem::path hostPath = HostPathForGuestPath(activeSysroot, guestPath);
    std::error_code ec;
    return std::filesystem::exists(hostPath, ec) ||
           std::filesystem::is_symlink(hostPath, ec);
}

- (NSString*)guestXwaylandPathForPrefix:(prefix::PrefixLayout*)selected
{
    NSArray<NSString*>* candidates = @[
        @"/usr/bin/Xwayland",
        @"/bin/Xwayland",
        @"/usr/libexec/Xwayland",
        @"/usr/lib/xorg/Xwayland"
    ];
    for (NSString* guestPath in candidates) {
        if ([self guestExecutableExists:guestPath prefix:selected])
            return guestPath;
    }
    return nil;
}

- (BOOL)guestXwaylandAvailableInPrefix:(prefix::PrefixLayout*)selected
{
    return [self guestXwaylandPathForPrefix:selected] != nil;
}

- (BOOL)guestArgumentsRequireX11:(NSArray<NSString*>*)guestArguments
{
    if (guestArguments.count == 0)
        return NO;
    return [self linuxTerminalRequiresX11:guestArguments[0]];
}

- (NSString*)guestShellPathForPrefix:(prefix::PrefixLayout*)selected
{
    for (NSString* guestPath in @[@"/bin/bash", @"/usr/bin/bash", @"/bin/sh", @"/usr/bin/sh"]) {
        if ([self guestExecutableExists:guestPath prefix:selected])
            return guestPath;
    }
    return @"/bin/sh";
}

- (NSArray<NSString*>*)wrapGuestArgumentsForX11IfNeeded:(NSArray<NSString*>*)guestArguments
                                                 prefix:(prefix::PrefixLayout*)selected
                                           errorMessage:(NSString**)errorMessage
{
    if (![self guestArgumentsRequireX11:guestArguments])
        return guestArguments;

    NSString* xwaylandPath = [self guestXwaylandPathForPrefix:selected];
    if (xwaylandPath.length == 0) {
        if (errorMessage) {
            *errorMessage =
                @"This Linux app needs X11. Install Xwayland inside the instance rootfs, then refresh the app list.";
        }
        return nil;
    }

    NSMutableString* execLine = [NSMutableString stringWithString:@"exec"];
    for (NSString* arg in guestArguments)
        [execLine appendFormat:@" %@", ShellSingleQuote(arg)];

    NSString* script = [NSString stringWithFormat:
        @"if [ -z \"$XDG_RUNTIME_DIR\" ]; then export XDG_RUNTIME_DIR=%@; fi; "
        @"if [ -z \"$WAYLAND_DISPLAY\" ]; then export WAYLAND_DISPLAY=%@; fi; "
        @"export DISPLAY=${DISPLAY:-:77}; "
        @"mkdir -p /tmp/.X11-unix; "
        @"if [ ! -S /tmp/.X11-unix/X77 ]; then "
        @"%@ \"$DISPLAY\" -rootless -terminate -nolisten tcp >/tmp/muplar-xwayland.log 2>&1 & "
        @"for i in 1 2 3 4 5; do "
        @"[ -S /tmp/.X11-unix/X77 ] && break; "
        @"sleep 1; "
        @"done; "
        @"fi; "
        @"%@",
        ShellSingleQuote(DefaultMuplarWaylandRuntimeDir()),
        ShellSingleQuote(DefaultMuplarWaylandDisplayName()),
        ShellSingleQuote(xwaylandPath),
        execLine];

    return @[[self guestShellPathForPrefix:selected], @"-lc", script];
}

- (NSString*)findGuestTerminalEmulatorInPrefix:(prefix::PrefixLayout*)selected
{
    auto profile = muplar::runtime::linux_common::distro_profile(
        selected ? selected->distro : std::string());

    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSMutableSet<NSString*>* seen = [NSMutableSet set];
    auto addCandidate = ^(NSString* guestPath) {
        if (guestPath.length == 0 || [seen containsObject:guestPath])
            return;
        [seen addObject:guestPath];
        [candidates addObject:guestPath];
    };

    for (const auto& path : profile.terminal_paths)
        addCandidate(NSStringFromStdString(path));

    NSArray<NSString*>* genericFallbacks = @[
        @"/usr/bin/qterminal",
        @"/usr/bin/foot",
        @"/usr/local/bin/foot",
        @"/usr/bin/xterm",
        @"/bin/xterm",
        @"/usr/bin/uxterm", @"/bin/uxterm",
        @"/usr/bin/gnome-terminal",
        @"/usr/bin/kgx",
        @"/usr/bin/alacritty",
        @"/usr/local/bin/alacritty",
        @"/usr/bin/kitty",
        @"/usr/local/bin/kitty",
        @"/usr/bin/konsole",
        @"/usr/bin/xfce4-terminal",
        @"/usr/bin/lxterminal",
        @"/usr/bin/mate-terminal",
        @"/usr/bin/weston-terminal"
    ];
    for (NSString* guestPath in genericFallbacks)
        addCandidate(guestPath);

    for (NSString* guestPath in candidates) {
        if ([self linuxTerminalRequiresX11:guestPath] &&
            ![self guestXwaylandAvailableInPrefix:selected])
            continue;
        if ([self guestExecutableExists:guestPath prefix:selected])
            return guestPath;
    }
    return nil;
}

- (NSString*)terminalInstallHintForPrefix:(prefix::PrefixLayout*)selected
{
    auto profile = muplar::runtime::linux_common::distro_profile(
        selected ? selected->distro : std::string());
    return NSStringFromStdString(
        muplar::runtime::linux_common::terminal_install_hint(profile));
}

- (NSArray<NSString*>*)guestTerminalArgumentsForTerminal:(NSString*)terminal
                                                   title:(NSString*)title
                                                   shell:(NSString*)shell
{
    NSString* name = terminal.lastPathComponent.lowercaseString;
    if ([name isEqualToString:@"xterm"] || [name isEqualToString:@"uxterm"] ||
        [name isEqualToString:@"lxterminal"]) {
        return @[terminal, @"-T", title, @"-e", shell];
    }
    if ([name isEqualToString:@"x-terminal-emulator"]) {
        return @[terminal, @"-e", shell];
    }
    if ([name isEqualToString:@"konsole"] ||
        [name isEqualToString:@"alacritty"]) {
        return @[terminal, @"--title", title, @"-e", shell];
    }
    if ([name isEqualToString:@"gnome-terminal"]) {
        return @[terminal, @"--wait", @"--title", title, @"--", shell];
    }
    if ([name isEqualToString:@"kgx"]) {
        return @[terminal, @"--title", title, @"--", shell];
    }
    if ([name isEqualToString:@"xfce4-terminal"]) {
        return @[terminal, @"--title", title, @"--command", shell];
    }
    if ([name isEqualToString:@"foot"]) {
        return @[terminal, @"--title", title, shell];
    }
    if ([name isEqualToString:@"qterminal"]) {
        return @[terminal, @"-e", shell];
    }
    if ([name isEqualToString:@"weston-terminal"]) {
        return @[terminal, shell];
    }
    return @[terminal, @"--title", title, @"--", shell];
}

- (void)launchLinuxTaskWithGuestArguments:(NSArray<NSString*>*)guestArguments
                                   prefix:(prefix::PrefixLayout*)selected
                                      app:(MuplarAppShortcut*)app
                                    quiet:(BOOL)quiet
{
    NSString* mupBin = [self mupBinPath];
    if (!mupBin) {
        [self showError:@"mup binary not found. Make sure the mup CLI is built and located next to the App bundle."];
        return;
    }

    if (![[NSFileManager defaultManager] isExecutableFileAtPath:mupBin]) {
        [self showError:[NSString stringWithFormat:@"mup binary at %@ is not executable.", mupBin]];
        return;
    }

    NSString* waylandError = nil;
    if (![self ensureMuplarWaylandForLinuxPrefix:selected errorMessage:&waylandError]) {
        [self showError:waylandError ?: @"Failed to start Muplar display compositor."];
        return;
    }

    NSString* sessionError = nil;
    if (![self ensureLinuxSessionForPrefix:selected errorMessage:&sessionError]) {
        [self showError:sessionError ?: @"Failed to start the Linux session launcher."];
        return;
    }

    NSString* x11Error = nil;
    NSArray<NSString*>* wrappedGuestArguments =
        [self wrapGuestArgumentsForX11IfNeeded:guestArguments
                                        prefix:selected
                                  errorMessage:&x11Error];
    if (!wrappedGuestArguments) {
        [self showError:x11Error ?: @"Failed to prepare Linux X11 launch."];
        return;
    }

    // Launch the guest program directly -- no `/usr/bin/env VAR=... ` prefix.
    //
    // env(1) reaches the program through a second execve, and a GUI client that
    // starts fine when exec'd once never produces a frame when exec'd that way:
    // it connects to the compositor, loads its config and fonts, then stops
    // silently with nothing on stderr and no window. Measured on foot -- first
    // frame after ~1s launched directly, no frame at all in 60s behind env(1),
    // alternating runs in one session.
    //
    // The session launcher already exports this environment (HOME, USER,
    // LOGNAME, LANG, LC_ALL, PATH, the Wayland/toolkit variables) and unsets
    // SESSION_MANAGER, DESKTOP_AUTOSTART_ID and GNOME_DESKTOP_SESSION_ID, so
    // every request inherits it without the extra exec.
    NSMutableArray<NSString*>* actualGuestArguments =
        [NSMutableArray arrayWithArray:wrappedGuestArguments];

    NSMutableDictionary<NSString*, NSString*>* env =
        [NSProcessInfo.processInfo.environment mutableCopy];
    ApplyDefaultLinuxDisplayEnvironment(env);
    env[@"ELFUSE_GUEST_UID"] = @"1000";
    env[@"ELFUSE_GUEST_GID"] = @"1000";
    // Arms the sudo shim. elfuse reads this once at process start and hands it
    // to fork children, so it belongs on the host environment here rather than
    // exported from inside the guest session.
    env[@"ELFUSE_FAKEROOT_EXEC"] = @(prefix::linux_fakeroot_exec_guest_path());
    env[@"MUPLAR_APP_PROCESS_GROUP"] = @"1";
    NSString* angleDir = [self angleLibraryDir];
    if (angleDir.length > 0) {
        NSString* existingDyld = env[@"DYLD_LIBRARY_PATH"];
        env[@"DYLD_LIBRARY_PATH"] = existingDyld.length > 0
            ? [angleDir stringByAppendingFormat:@":%@", existingDyld]
            : angleDir;
    }

    // The guest reaches the compositor through a hard link inside the prefix:
    // it cannot name the host socket, and the inode changes on every rebind, so
    // republish here rather than at provisioning time.
    prefix::publish_display_socket(selected->rootfs);

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = mupBin;
    (void)quiet;
    NSMutableArray<NSString*>* args = [NSMutableArray arrayWithArray:@[
        @"linux-session-exec", NSStringFromStdString(selected->name), @"--"]];
    [args addObjectsFromArray:actualGuestArguments];
    task.arguments = args;
    task.environment = env;
    [self setupLoggingForTask:task prefix:selected appName:app.name];

    NSString* key = [self appKeyForApp:app];
    NSSet<NSNumber*>* baselineWindowNumbers = VisibleMuplarWaylandWindowNumbers();
    NSUUID* launchToken = NSUUID.UUID;
    [_launchingAppPaths addObject:key];
    _linuxLaunchTokens[key] = launchToken;
    [_appsTableView reloadData];

    std::filesystem::path pidPath = prefix::pid_file_path(*selected);
    NSString* pidFile = NSStringFromPath(pidPath);
    [[NSFileManager defaultManager] createDirectoryAtPath:[pidFile stringByDeletingLastPathComponent]
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];

    task.terminationHandler = ^(NSTask* t) {
        // Fork helpers outlive the top-level guest unless the complete launch
        // group is reaped. They can otherwise retain Wayland sockets and keep
        // already-exited terminal windows visible in Muplar Wayland.
        kill(-t.processIdentifier, SIGKILL);
        [[NSFileManager defaultManager] removeItemAtPath:pidFile error:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_linuxLaunchTokens removeObjectForKey:key];
            [self->_launchingAppPaths removeObject:key];
            [self->_runningTasks removeObjectForKey:key];
            [self loadAppsForSelectedPrefix];
            [self reloadPrefixes:nil];
        });
    };

    @try {
        [task launch];
        _runningTasks[key] = task;
        NSString* pidContent = [NSString stringWithFormat:@"%d\n", task.processIdentifier];
        [pidContent writeToFile:pidFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
        [self waitForLinuxWindowForKey:key
                                 token:launchToken
                 baselineWindowNumbers:baselineWindowNumbers
                               attempts:0];
    } @catch (NSException* ex) {
        [_linuxLaunchTokens removeObjectForKey:key];
        [self->_launchingAppPaths removeObject:key];
        [_appsTableView reloadData];
        [self showError:[NSString stringWithFormat:@"Failed to launch Linux app: %@", ex.reason]];
    }
}

- (void)launchWineTerminal:(MuplarAppShortcut*)app prefix:(prefix::PrefixLayout*)selected
{
    NSString* wineBin = [self wine64PathForPrefix:*selected];
    if (!wineBin) {
        [self showError:@"Windows compatibility layer not found. Build Windows support first or set the prefix sysroot."];
        return;
    }

    EnsureWineSocketDirPrivate();

    NSString* winePrefix = NSStringFromPath(selected->rootfs);
    NSString* bundleDllPath = [[[[NSBundle mainBundle] privateFrameworksPath]
                                  stringByAppendingPathComponent:@"wine"]
                                  stringByAppendingPathComponent:@"lib/wine"];
    NSString* siblingDllPath = [[[wineBin stringByDeletingLastPathComponent]
                                          stringByDeletingLastPathComponent]
                                          stringByAppendingPathComponent:@"lib/wine"];
    NSString* wineLibDir = [[NSFileManager defaultManager] fileExistsAtPath:siblingDllPath]
                           ? siblingDllPath : bundleDllPath;

    NSString* wineLibPath = [[[wineBin stringByDeletingLastPathComponent]
                                       stringByDeletingLastPathComponent]
                                       stringByAppendingPathComponent:@"lib"];

    NSString* existingFallback = NSProcessInfo.processInfo.environment[@"DYLD_FALLBACK_LIBRARY_PATH"];
    NSString* newFallback = [NSString stringWithFormat:@"/usr/local/lib:/opt/homebrew/lib:%@", wineLibPath];
    if (existingFallback) {
        newFallback = [newFallback stringByAppendingFormat:@":%@", existingFallback];
    }

    NSString* cmdStr = [NSString stringWithFormat:
        @"export WINEPREFIX='%@'\n"
        @"export WINEDEBUG='-all'\n"
        @"%@\n"
        @"export DYLD_FALLBACK_LIBRARY_PATH='%@'\n"
        @"exec '%@' 'cmd'",
        winePrefix,
        wineLibDir ? [NSString stringWithFormat:@"export WINEDLLPATH='%@'", wineLibDir] : @"",
        newFallback,
        wineBin];

    [self runCommandInTerminal:cmdStr prefix:selected app:app];
}

- (void)launchLinuxApp:(MuplarAppShortcut*)app prefix:(prefix::PrefixLayout*)selected
{
    NSString* guestAppPath = [self guestPathFromHostPath:app.path prefix:selected];

    if ([self isLinuxShellShortcut:app]) {
        [self launchLinuxShellInHostTerminal:app prefix:selected];
        return;
    }

    if ([self isTerminalShortcut:app]) {
        // Launch the terminal the user actually picked. The preferred-terminal
        // lookup is a fallback for shortcuts that do not name a runnable binary
        // -- isTerminalShortcut also matches on display name, so an entry can
        // reach here pointing at something that is not an executable. It must
        // not override a real choice: it returns whichever terminal sorts first
        // in the distro profile, so clicking "GNOME Terminal" would silently
        // open qterminal instead.
        NSString* launchTerminal = guestAppPath;
        if (![self guestExecutableExists:launchTerminal prefix:selected]) {
            NSString* preferredTerminal =
                [self findGuestTerminalEmulatorInPrefix:selected];
            if (preferredTerminal.length > 0)
                launchTerminal = preferredTerminal;
        }
        NSArray<NSString*>* guestArgs =
            [self guestTerminalArgumentsForTerminal:launchTerminal
                                              title:app.name
                                              shell:[self guestShellPathForPrefix:selected]];
        [self launchLinuxTaskWithGuestArguments:guestArgs
                                         prefix:selected
                                            app:app
                                          quiet:YES];
        return;
    }

    [self launchLinuxTaskWithGuestArguments:@[guestAppPath]
                                     prefix:selected
                                        app:app
                                      quiet:NO];
}

- (void)launchLinuxShellInHostTerminal:(MuplarAppShortcut*)app prefix:(prefix::PrefixLayout*)selected
{
    NSString* mupBin = [self mupBinPath];
    if (!mupBin) {
        [self showError:@"mup binary not found. Make sure the mup CLI is built and located next to the App bundle."];
        return;
    }

    NSString* logPath = [NSStringFromPath(selected->logs_dir) stringByAppendingPathComponent:@"shell.log"];
    RotateLogFileIfNeeded(logPath);
    NSString* command = [NSString stringWithFormat:
        @"MUP=%@\n"
        @"PREFIX=%@\n"
        @"export ELFUSE_GUEST_UID=1000\n"
        @"export ELFUSE_GUEST_GID=1000\n"
        @"export ELFUSE_FAKEROOT_EXEC=%@\n"
        @"LOG=%@\n"
        @"LOGGING=%@\n"
        @"clear\n"
        @"if [ \"$LOGGING\" = 1 ]; then\n"
        @"  mkdir -p \"$(dirname \"$LOG\")\"\n"
        @"  printf '\\n=== Muplar Shell started at %%s ===\\n' \"$(date -u '+%%Y-%%m-%%dT%%H:%%M:%%SZ')\" >> \"$LOG\"\n"
        @"fi\n"
        @"printf 'Muplar Linux shell (%%s)\\n' \"$PREFIX\"\n"
        @"printf 'Type a command such as: ls -la, echo hello. Type exit to close.\\n\\n'\n"
        @"while :; do\n"
        @"  printf 'muplar:%%s$ ' \"$PREFIX\"\n"
        @"  IFS= read -r line || break\n"
        @"  case \"$line\" in\n"
        @"    '') continue ;;\n"
        @"    exit|quit) break ;;\n"
        @"    clear) clear; continue ;;\n"
        @"    cd|cd\\ *) printf 'cd is not available in this minimal shell yet.\\n'; continue ;;\n"
        @"  esac\n"
        @"  if [ \"$LOGGING\" = 1 ]; then printf '\\n$ %%s\\n' \"$line\" >> \"$LOG\"; fi\n"
        @"  if [ \"$LOGGING\" = 1 ]; then\n"
        @"    \"$MUP\" --quiet --prefix \"$PREFIX\" /usr/bin/env HOME=/home/muplar USER=muplar LOGNAME=muplar DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /bin/sh -c \"$line\"\n"
        @"    rc=$?\n"
        @"  else\n"
        @"    \"$MUP\" --quiet --prefix \"$PREFIX\" /usr/bin/env HOME=/home/muplar USER=muplar LOGNAME=muplar DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /bin/sh -c \"$line\"\n"
        @"    rc=$?\n"
        @"  fi\n"
        @"  if [ $rc -ne 0 ]; then printf '[exit %%d]\\n' \"$rc\"; fi\n"
        @"  if [ $rc -ne 0 ] && [ \"$LOGGING\" = 1 ]; then printf '[exit %%d]\\n' \"$rc\" >> \"$LOG\"; fi\n"
        @"done\n",
        ShellSingleQuote(mupBin),
        ShellSingleQuote(NSStringFromStdString(selected->name)),
        ShellSingleQuote(@(prefix::linux_fakeroot_exec_guest_path())),
        ShellSingleQuote(logPath),
        selected->logging ? @"1" : @"0"];
    [self runCommandInTerminal:command prefix:selected app:app];
}

- (void)launchShortcut:(MuplarAppShortcut*)app
{
    prefix::PrefixLayout* selected = [self selectedPrefix];
    if (!selected) return;

    if (selected->kind == prefix::PrefixKind::Wine) {
        [self launchWineApp:app prefix:selected];
    } else if (selected->kind == prefix::PrefixKind::Linux) {
        [self launchLinuxApp:app prefix:selected];
    } else {
        [self launchAndroidApp:app prefix:selected];
    }
}

- (void)launchAndroidApp:(MuplarAppShortcut*)app
                   prefix:(prefix::PrefixLayout*)selected
{
    NSString* mupBin = [self mupBinPath];
    if (!mupBin || ![[NSFileManager defaultManager] isExecutableFileAtPath:mupBin]) {
        [self showError:@"mup binary is unavailable for Android launch."];
        return;
    }
    if (![[NSFileManager defaultManager] isReadableFileAtPath:app.path]) {
        [self showError:@"The installed APK is missing or unreadable."];
        return;
    }

    NSString* key = [self appKeyForApp:app];
    NSString* deviceKey = [self androidDeviceKeyForPrefix:selected];
    BOOL isLauncherApp = app.name.length > 0 &&
        [app.name rangeOfString:@"launcher"
                        options:NSCaseInsensitiveSearch].location != NSNotFound;
    NSString* tabIdentifier = isLauncherApp ? @"launcher" : key;
    NSString* tabTitle = isLauncherApp ? @"Home" : (app.name ?: @"App");
    NSString* sessionSocketPath = NSStringFromPath(selected->root / "run" / "muplard.sock");
    NSString* frameSocketPath =
        NSStringFromPath(selected->root / "run" / "android-frame.sock");
    std::filesystem::path androidRuntimeRootfs = selected->runtime_sysroot.empty()
        ? selected->rootfs
        : selected->runtime_sysroot;
    NSString* softwareFrameHostPath = NSStringFromPath(
        androidRuntimeRootfs / "data" / "local" / "tmp" / "muplar" / "frames" /
        "software-frame.mhr");
    NSString* sessionLogPath =
        NSStringFromPath(muplar::runtime::prefix::main_log_path(*selected));
    NSString* rootfsPath = NSStringFromPath(androidRuntimeRootfs);
    AndroidDeviceShell* shell =
        [self androidDeviceShellForPrefix:selected app:app sessionKey:deviceKey];
    [shell startFrameServerAtPath:frameSocketPath];
    [shell focusOrCreateTabWithIdentifier:tabIdentifier title:tabTitle];

    NSTask* existingSession = _androidSessionTasks[deviceKey];
    BOOL existingRunning = existingSession && existingSession.isRunning;
    BOOL sessionIsStarting = [_startingAndroidSessionKeys containsObject:deviceKey] ||
        _pendingAndroidSessionTasks[deviceKey] != nil;
    if (existingRunning || sessionIsStarting) {
        _androidSessionAppKeys[deviceKey] = key;
        _androidSessionTabIdentifiers[deviceKey] = tabIdentifier;
        [_cancelledAndroidSessionKeys removeObject:deviceKey];
        [_stoppingAndroidSessionKeys removeObject:deviceKey];
        [shell showLaunchingApp:app.name];
        [self prepareAndroidLaunchMetadataForApp:app
                                          rootfs:rootfsPath
                                      completion:
                                          ^(NSDictionary<NSString*, NSString*>*
                                                metadata) {
                                              if (metadata)
                                                  self->_androidTabLaunchMetadata
                                                      [tabIdentifier] =
                                                          metadata;
                                              [shell showRunningApp:app.name];
                                              if (existingRunning) {
                                                  [self sendAndroidDeviceAction:
                                                            @"focus-tab"
                                                                  tabIdentifier:
                                                                      tabIdentifier
                                                                 launchMetadata:
                                                                     metadata
                                                                     socketPath:
                                                                         sessionSocketPath
                                                                        logPath:
                                                                            sessionLogPath];
                                              }
                                          }];
        [_appsTableView reloadData];
        return;
    }

    if ([_stoppingAndroidSessionKeys containsObject:deviceKey]) {
        [shell showStoppedWithMessage:@"Android session is stopping."];
        [_appsTableView reloadData];
        return;
    }

    NSTask* task = [[NSTask alloc] init];
    task.launchPath = mupBin;
    task.arguments = @[@"--prefix", NSStringFromStdString(selected->name),
                       @"--apk", @"--host-window", app.path];

    NSMutableDictionary<NSString*, NSString*>* env =
        [NSProcessInfo.processInfo.environment mutableCopy];
    NSString* angleDir = [self angleLibraryDir];
    if (angleDir.length > 0) {
        NSString* existing = env[@"DYLD_LIBRARY_PATH"];
        env[@"DYLD_LIBRARY_PATH"] = existing.length > 0
            ? [angleDir stringByAppendingFormat:@":%@", existing]
            : angleDir;
    }
    ApplyDefaultLinuxDisplayEnvironment(env);
    env[@"MUPLAR_HOST_WINDOW_FRAME_SOCKET"] = frameSocketPath;
    env[@"MUPLAR_HOST_WINDOW_SOFTWARE_FRAME_PATH"] = softwareFrameHostPath;
    env[@"MUPLAR_ANDROID_SOFTWARE_FRAME_PATH"] =
        @"/data/local/tmp/muplar/frames/software-frame.mhr";
    env[@"MUPLAR_SERVICE_SOCKET"] = sessionSocketPath;
    task.environment = env;
    [self setupLoggingForTask:task prefix:selected appName:app.name];

    [shell showLaunchingApp:app.name];
    [_launchingAppPaths addObject:key];
    _androidSessionAppKeys[deviceKey] = key;
    _androidSessionTabIdentifiers[deviceKey] = tabIdentifier;
    _pendingAndroidSessionTasks[deviceKey] = task;
    [_startingAndroidSessionKeys addObject:deviceKey];
    [_cancelledAndroidSessionKeys removeObject:deviceKey];
    [_appsTableView reloadData];

    task.terminationHandler = ^(NSTask* finished) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_launchingAppPaths removeObject:key];
            NSString* activeKey = self->_androidSessionAppKeys[deviceKey];
            if (self->_androidSessionTasks[deviceKey] == finished)
                [self->_androidSessionTasks removeObjectForKey:deviceKey];
            if (self->_pendingAndroidSessionTasks[deviceKey] == finished)
                [self->_pendingAndroidSessionTasks removeObjectForKey:deviceKey];
            [self->_startingAndroidSessionKeys removeObject:deviceKey];
            if (activeKey)
                [self->_runningTasks removeObjectForKey:activeKey];
            [self->_runningTasks removeObjectForKey:key];
            if (activeKey && [activeKey isEqualToString:key])
                [self->_androidSessionAppKeys removeObjectForKey:deviceKey];
            [self->_androidSessionTabIdentifiers removeObjectForKey:deviceKey];
            [self->_cancelledAndroidSessionKeys removeObject:deviceKey];
            [self->_stoppingAndroidSessionKeys removeObject:deviceKey];
            [self->_appsTableView reloadData];
            int status = finished.terminationStatus;
            AndroidDeviceShell* activeShell = self->_androidDeviceShells[deviceKey];
            BOOL expectedStop =
                status == 0 || status == 15 || status == 130 || status == 143;
            if (activeShell == shell) {
                NSString* message = expectedStop
                    ? @"Android session stopped."
                    : [NSString stringWithFormat:@"Android session stopped with exit code %d.",
                                                 status];
                [activeShell showStoppedWithMessage:message];
            }
            if (!expectedStop) {
                [self showError:[NSString stringWithFormat:
                    @"%@ stopped with exit code %d. See the instance log for details.",
                    app.name, status]];
            }
        });
    };

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSDictionary<NSString*, NSString*>* launchMetadata =
            [self androidLaunchMetadataForAppPath:app.path rootfs:rootfsPath];
        __block BOOL cancelledBeforeLaunch = NO;
        dispatch_sync(dispatch_get_main_queue(), ^{
            cancelledBeforeLaunch =
                [self->_cancelledAndroidSessionKeys containsObject:deviceKey];
        });
        if (cancelledBeforeLaunch) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_launchingAppPaths removeObject:key];
                if (self->_pendingAndroidSessionTasks[deviceKey] == task)
                    [self->_pendingAndroidSessionTasks removeObjectForKey:deviceKey];
                [self->_startingAndroidSessionKeys removeObject:deviceKey];
                [self->_cancelledAndroidSessionKeys removeObject:deviceKey];
                [self->_stoppingAndroidSessionKeys removeObject:deviceKey];
                [self->_androidSessionAppKeys removeObjectForKey:deviceKey];
                [self->_androidSessionTabIdentifiers removeObjectForKey:
                                                       deviceKey];
                [self->_appsTableView reloadData];
            });
            return;
        }
        @try {
            [task launch];
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!task.isRunning) {
                    [self->_launchingAppPaths removeObject:key];
                    if (self->_pendingAndroidSessionTasks[deviceKey] == task)
                        [self->_pendingAndroidSessionTasks removeObjectForKey:
                                                            deviceKey];
                    [self->_startingAndroidSessionKeys removeObject:deviceKey];
                    [self->_appsTableView reloadData];
                    return;
                }
                if ([self->_cancelledAndroidSessionKeys containsObject:deviceKey]) {
                    [task terminate];
                    return;
                }
                if (launchMetadata)
                    self->_androidTabLaunchMetadata[tabIdentifier] =
                        launchMetadata;
                NSString* activeTab =
                    self->_androidSessionTabIdentifiers[deviceKey] ?:
                    tabIdentifier;
                NSDictionary<NSString*, NSString*>* activeMetadata =
                    self->_androidTabLaunchMetadata[activeTab] ?:
                    launchMetadata;
                self->_androidSessionTasks[deviceKey] = task;
                if (self->_pendingAndroidSessionTasks[deviceKey] == task)
                    [self->_pendingAndroidSessionTasks removeObjectForKey:
                                                        deviceKey];
                [self->_launchingAppPaths removeObject:key];
                [self->_startingAndroidSessionKeys removeObject:deviceKey];
                [self->_stoppingAndroidSessionKeys removeObject:deviceKey];
                [shell showRunningApp:app.name];
                [self sendAndroidDeviceAction:@"focus-tab"
                                tabIdentifier:activeTab
                               launchMetadata:activeMetadata
                                   socketPath:sessionSocketPath
                                      logPath:sessionLogPath];
                [self->_appsTableView reloadData];
            });
        } @catch (NSException* ex) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_launchingAppPaths removeObject:key];
                [self->_androidSessionAppKeys removeObjectForKey:deviceKey];
                [self->_androidSessionTabIdentifiers removeObjectForKey:
                                                       deviceKey];
                if (self->_pendingAndroidSessionTasks[deviceKey] == task)
                    [self->_pendingAndroidSessionTasks removeObjectForKey:
                                                        deviceKey];
                [self->_startingAndroidSessionKeys removeObject:deviceKey];
                [self->_cancelledAndroidSessionKeys removeObject:deviceKey];
                [shell showStoppedWithMessage:@"Failed to start Android session."];
                [self->_appsTableView reloadData];
                [self showError:[NSString stringWithFormat:
                    @"Failed to launch Android app: %@", ex.reason]];
            });
        }
    });
}

- (NSString*)androidDeviceKeyForPrefix:(prefix::PrefixLayout*)selected
{
    if (!selected)
        return @"android";
    return [NSString stringWithFormat:@"%s", selected->name.c_str()];
}

- (AndroidDeviceShell*)androidDeviceShellForPrefix:(prefix::PrefixLayout*)selected
                                               app:(MuplarAppShortcut*)app
                                        sessionKey:(NSString*)sessionKey
{
    NSString* deviceKey = [self androidDeviceKeyForPrefix:selected];
    AndroidDeviceShell* shell = _androidDeviceShells[deviceKey];
    if (!shell) {
        NSString* prefixName =
            selected ? NSStringFromStdString(selected->name) : @"Android";
        shell = [[AndroidDeviceShell alloc] initWithPrefixName:prefixName];
        _androidDeviceShells[deviceKey] = shell;
    }
    __weak PrefixManagerAppDelegate* weakSelf = self;
    __weak AndroidDeviceShell* weakShell = shell;
    NSString* capturedSessionKey = [sessionKey copy];
    NSString* capturedDeviceKey = [deviceKey copy];
    NSString* capturedLogPath = selected
        ? NSStringFromPath(muplar::runtime::prefix::main_log_path(*selected))
        : nil;
    NSString* capturedSocketPath = selected
        ? NSStringFromPath(selected->root / "run" / "muplard.sock")
        : nil;
    NSString* capturedPrefixName =
        selected ? NSStringFromStdString(selected->name) : nil;
    shell.closeHandler = ^{
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        AndroidDeviceShell* strongShell = weakShell;
        if (!strongSelf || !strongShell)
            return;
        NSTask* running = strongSelf->_androidSessionTasks[capturedSessionKey];
        NSTask* pending =
            strongSelf->_pendingAndroidSessionTasks[capturedSessionKey];
        [strongSelf->_cancelledAndroidSessionKeys addObject:capturedSessionKey];
        [strongSelf->_stoppingAndroidSessionKeys addObject:capturedSessionKey];
        if (running.isRunning) {
            [running terminate];
        } else if (pending.isRunning) {
            [pending terminate];
        } else {
            [strongSelf->_androidSessionTasks removeObjectForKey:capturedSessionKey];
            [strongSelf->_pendingAndroidSessionTasks removeObjectForKey:
                                                  capturedSessionKey];
            [strongSelf->_androidSessionAppKeys removeObjectForKey:capturedSessionKey];
            [strongSelf->_androidSessionTabIdentifiers removeObjectForKey:
                                                    capturedSessionKey];
            [strongSelf->_startingAndroidSessionKeys removeObject:capturedSessionKey];
            [strongSelf->_cancelledAndroidSessionKeys removeObject:capturedSessionKey];
            [strongSelf->_stoppingAndroidSessionKeys removeObject:capturedSessionKey];
        }
        [strongSelf->_appsTableView reloadData];
        if (strongSelf->_androidDeviceShells[capturedDeviceKey] == strongShell)
            [strongSelf->_androidDeviceShells removeObjectForKey:capturedDeviceKey];
    };
    shell.actionHandler = ^(NSString* action, NSString* tabIdentifier) {
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        if (!strongSelf)
            return;
        [strongSelf recordAndroidDeviceEvent:action
                               tabIdentifier:tabIdentifier
                                     logPath:capturedLogPath];
        if ([action isEqualToString:@"install-apk"]) {
            AndroidDeviceShell* strongShell = weakShell;
            if (strongShell) {
                [strongShell deviceInstall:nil];
            } else {
                [strongSelf installApkForPrefix:
                                [strongSelf prefixNamed:capturedPrefixName]];
            }
            return;
        }
        [strongSelf sendAndroidDeviceAction:action
                              tabIdentifier:tabIdentifier
                                 socketPath:capturedSocketPath
                                    logPath:capturedLogPath];
        if ([action isEqualToString:@"back"] && tabIdentifier.length > 0) {
            [strongSelf pollTabFinishedForIdentifier:tabIdentifier
                                          deviceShell:weakShell
                                           socketPath:capturedSocketPath];
        }
    };
    shell.installApkHandler = ^(NSString* apkPath) {
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        AndroidDeviceShell* strongShell = weakShell;
        if (!strongSelf || !strongShell || apkPath.length == 0)
            return;
        [strongShell showInstallProgress:[NSString stringWithFormat:@"Installing %@...", apkPath.lastPathComponent]];
        prefix::PrefixLayout* prefix = [strongSelf prefixNamed:capturedPrefixName];
        NSError* error = nil;
        NSString* pkg = nil;
        BOOL ok = [strongSelf installApkAtPath:apkPath
                                    forPrefix:prefix
                                        error:&error
                         installedPackageName:&pkg];
        if (!ok) {
            [strongShell showStoppedWithMessage:[NSString stringWithFormat:@"Install failed: %@", error.localizedDescription ?: @"unknown error"]];
            return;
        }
        [strongShell showInstallSuccess:[NSString stringWithFormat:@"Installed %@", pkg ?: apkPath.lastPathComponent]];
        if (pkg.length > 0) {
            [strongSelf sendAndroidDeviceAction:@"package-installed"
                                  tabIdentifier:pkg
                                 launchMetadata:@{@"package": pkg, @"apk": apkPath}
                                     socketPath:capturedSocketPath
                                        logPath:capturedLogPath];
        }
    };
    shell.tabFocusHandler = ^(NSString* tabIdentifier) {
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        if (!strongSelf)
            return;
        [strongSelf recordAndroidDeviceEvent:@"focus-tab"
                               tabIdentifier:tabIdentifier
                                     logPath:capturedLogPath];
        NSDictionary<NSString*, NSString*>* metadata =
            strongSelf->_androidTabLaunchMetadata[tabIdentifier];
        [strongSelf sendAndroidDeviceAction:@"focus-tab"
                              tabIdentifier:tabIdentifier
                             launchMetadata:metadata
                                 socketPath:capturedSocketPath
                                    logPath:capturedLogPath];
    };
    shell.tabCloseHandler = ^(NSString* tabIdentifier) {
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        if (!strongSelf)
            return;
        [strongSelf recordAndroidDeviceEvent:@"close-tab"
                               tabIdentifier:tabIdentifier
                                     logPath:capturedLogPath];
        [strongSelf->_androidTabLaunchMetadata removeObjectForKey:
                                               tabIdentifier];
        [strongSelf sendAndroidDeviceAction:@"close-tab"
                              tabIdentifier:tabIdentifier
                                 socketPath:capturedSocketPath
                                    logPath:capturedLogPath];
    };
    shell.inputHandler = ^(NSString* tabIdentifier, int32_t type,
                           int32_t action, int32_t source, int32_t deviceId,
                           int32_t keyCode, float x, float y) {
        PrefixManagerAppDelegate* strongSelf = weakSelf;
        if (!strongSelf)
            return;
        [strongSelf sendAndroidDeviceInputForTab:tabIdentifier
                                            type:type
                                          action:action
                                          source:source
                                        deviceId:deviceId
                                         keyCode:keyCode
                                               x:x
                                               y:y
                                      socketPath:capturedSocketPath
                                         logPath:capturedLogPath];
    };
    shell.window.title = [NSString stringWithFormat:@"%@ - %@",
                                                    app.name ?: @"Android",
                                                    deviceKey];
    return shell;
}

- (void)recordAndroidDeviceEvent:(NSString*)event
                    tabIdentifier:(NSString*)tabIdentifier
                          logPath:(NSString*)logPath
{
    if (event.length == 0 || logPath.length == 0)
        return;
    NSFileHandle* logHandle = OpenRotatingLogForAppend(logPath);
    if (!logHandle)
        return;
    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    NSString* timestamp = [formatter stringFromDate:[NSDate date]];
    NSString* line = [NSString stringWithFormat:
        @"[%@] Android device action=%@ tab=%@\n",
        timestamp, event, tabIdentifier ?: @""];
    [logHandle writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
    [logHandle closeFile];
}

- (void)sendAndroidDeviceAction:(NSString*)action
                   tabIdentifier:(NSString*)tabIdentifier
                      socketPath:(NSString*)socketPath
                         logPath:(NSString*)logPath
{
    [self sendAndroidDeviceAction:action
                    tabIdentifier:tabIdentifier
                   launchMetadata:nil
                       socketPath:socketPath
                          logPath:logPath];
}

- (NSDictionary<NSString*, NSString*>*)androidLaunchMetadataForApp:
    (MuplarAppShortcut*)app
                                                           prefix:
                                                               (prefix::PrefixLayout*)selected
{
    if (!app || !selected || app.path.length == 0)
        return nil;
    return [self androidLaunchMetadataForAppPath:app.path
                                          rootfs:NSStringFromPath(selected->rootfs)];
}

- (void)prepareAndroidLaunchMetadataForApp:(MuplarAppShortcut*)app
                                    rootfs:(NSString*)rootfsPath
                                completion:
                                    (void (^)(NSDictionary<NSString*, NSString*>* metadata))
                                        completion
{
    NSString* appPath = [app.path copy];
    NSString* rootfsCopy = [rootfsPath copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSDictionary<NSString*, NSString*>* metadata =
            [self androidLaunchMetadataForAppPath:appPath rootfs:rootfsCopy];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (completion)
                completion(metadata);
        });
    });
}

- (NSDictionary<NSString*, NSString*>*)androidLaunchMetadataForAppPath:
                                          (NSString*)appPath
                                                           rootfs:
                                                               (NSString*)rootfsPath
{
    if (appPath.length == 0 || rootfsPath.length == 0)
        return nil;
    NSString* guestApkPath = @"";
    @try {
        std::filesystem::path source(appPath.UTF8String);
        std::filesystem::path stagingDir =
            std::filesystem::path(rootfsPath.UTF8String) / "data" / "local" /
            "tmp" / "muplar" / "apks";
        std::error_code ec;
        std::filesystem::create_directories(stagingDir, ec);
        std::filesystem::path target = stagingDir / source.filename();
        std::filesystem::copy_file(
            source, target,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            guestApkPath =
                [@"/data/local/tmp/muplar/apks"
                    stringByAppendingPathComponent:NSStringFromPath(
                                                       source.filename())];
        }
    } @catch (...) {
    }

    NSString* packageName = @"";
    NSString* activityName = @"";
    NSString* applicationName = @"";
    @try {
        auto apk = muplar::runtime::apk::classify_apk(
            std::filesystem::path(appPath.UTF8String));
        if (apk.manifest_package)
            packageName = NSStringFromStdString(*apk.manifest_package);
        if (apk.manifest_launch_activity)
            activityName =
                NSStringFromStdString(*apk.manifest_launch_activity);
        if (apk.manifest_application_class)
            applicationName =
                NSStringFromStdString(*apk.manifest_application_class);
    } @catch (...) {
    }

    if (guestApkPath.length == 0 && packageName.length == 0 &&
        activityName.length == 0 && applicationName.length == 0)
        return nil;
    return @{
        @"apk": guestApkPath,
        @"package": packageName,
        @"activity": activityName,
        @"application": applicationName,
    };
}

- (void)sendAndroidDeviceAction:(NSString*)action
                   tabIdentifier:(NSString*)tabIdentifier
                  launchMetadata:(NSDictionary<NSString*, NSString*>*)metadata
                      socketPath:(NSString*)socketPath
                         logPath:(NSString*)logPath
{
    if (action.length == 0 || socketPath.length == 0)
        return;
    NSString* actionCopy = [action copy];
    NSString* tabCopy = [tabIdentifier copy] ?: @"";
    NSString* apkCopy = [metadata[@"apk"] copy] ?: @"";
    NSString* packageCopy = [metadata[@"package"] copy] ?: @"";
    NSString* activityCopy = [metadata[@"activity"] copy] ?: @"";
    NSString* applicationCopy = [metadata[@"application"] copy] ?: @"";
    NSString* socketCopy = [socketPath copy];
    NSString* logCopy = [logPath copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        services::MuplardClient client(std::string(socketCopy.UTF8String));
        std::string generation;
        bool delivered = client.device_action(
            std::string(actionCopy.UTF8String),
            std::string(tabCopy.UTF8String),
            std::string(apkCopy.UTF8String),
            std::string(packageCopy.UTF8String),
            std::string(activityCopy.UTF8String),
            std::string(applicationCopy.UTF8String),
            generation);
        if (logCopy.length == 0)
            return;
        NSFileHandle* logHandle = OpenRotatingLogForAppend(logCopy);
        if (!logHandle)
            return;
        NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
        formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
        NSString* timestamp = [formatter stringFromDate:[NSDate date]];
        NSString* line = [NSString stringWithFormat:
            @"[%@] Android device delivery action=%@ tab=%@ package=%@ delivered=%@ generation=%s\n",
            timestamp, actionCopy, tabCopy, packageCopy,
            delivered ? @"yes" : @"no", delivered ? generation.c_str() : ""];
        [logHandle writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
        [logHandle closeFile];
    });
}

- (void)sendAndroidDeviceInputForTab:(NSString*)tabIdentifier
                                 type:(int32_t)type
                               action:(int32_t)action
                               source:(int32_t)source
                             deviceId:(int32_t)deviceId
                              keyCode:(int32_t)keyCode
                                    x:(float)x
                                    y:(float)y
                           socketPath:(NSString*)socketPath
                              logPath:(NSString*)logPath
{
    if (socketPath.length == 0)
        return;
    NSString* tabCopy = [tabIdentifier copy] ?: @"";
    NSString* socketCopy = [socketPath copy];
    NSString* logCopy = [logPath copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        services::MuplardClient client(std::string(socketCopy.UTF8String));
        std::string generation;
        bool delivered = client.device_input(std::string(tabCopy.UTF8String),
                                             type, action, source, deviceId,
                                             keyCode, x, y, generation);
#ifdef DEBUG
        NSLog(@"[PointerDebug] device_input tab=%@ type=%d action=%d x=%.1f y=%.1f delivered=%d generation=%s",
              tabCopy, type, action, x, y, (int)delivered, generation.c_str());
#endif
    });
}

- (void)pollTabFinishedForIdentifier:(NSString*)tabIdentifier
                          deviceShell:(AndroidDeviceShell*)deviceShell
                           socketPath:(NSString*)socketPath
{
    if (tabIdentifier.length == 0 || socketPath.length == 0 || !deviceShell)
        return;
    NSString* tabCopy = [tabIdentifier copy];
    NSString* socketCopy = [socketPath copy];
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.35 * NSEC_PER_SEC)),
        dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            services::MuplardClient client(std::string(socketCopy.UTF8String));
            std::string state;
            if (!client.query_tab_finished(state))
                return;
            std::string finishedTab;
            std::istringstream stream(state);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.rfind("tab=", 0) == 0) {
                    finishedTab = line.substr(4);
                    break;
                }
            }
            if (finishedTab.empty() ||
                finishedTab != std::string(tabCopy.UTF8String))
                return;
            dispatch_async(dispatch_get_main_queue(), ^{
                [deviceShell closeTabWithIdentifier:tabCopy];
            });
        });
}

- (void)launchWineApp:(MuplarAppShortcut*)app prefix:(prefix::PrefixLayout*)selected
{
    BOOL isCommandPrompt = [self isWineCommandPromptShortcut:app];
    if ([self isTerminalShortcut:app] && !isCommandPrompt) {
        [self launchWineTerminal:app prefix:selected];
        return;
    }

    NSString* wineBin = [self wine64PathForPrefix:*selected];
    if (!wineBin) {
        [self showError:@"Muplar Windows Compatibility runtime not found. Build Windows support first or set the prefix sysroot."];
        return;
    }

    NSString* winePrefix = NSStringFromPath(selected->rootfs);

    EnsureWineSocketDirPrivate();

    NSMutableDictionary* env = [NSProcessInfo.processInfo.environment mutableCopy];
    env[@"WINEPREFIX"] = winePrefix;

    NSString* bundleDllPath = [[[[NSBundle mainBundle] privateFrameworksPath]
                                  stringByAppendingPathComponent:@"wine"]
                                  stringByAppendingPathComponent:@"lib/wine"];
    NSString* siblingDllPath = [[[wineBin stringByDeletingLastPathComponent]
                                          stringByDeletingLastPathComponent]
                                          stringByAppendingPathComponent:@"lib/wine"];
    NSString* wineLibDir = [[NSFileManager defaultManager] fileExistsAtPath:siblingDllPath]
                           ? siblingDllPath : bundleDllPath;
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

    env[@"WINEDEBUG"] = @"-all";

    NSTask* task = [[NSTask alloc] init];
    NSString* launchPath = wineBin;
    NSArray<NSString*>* arguments = nil;
    if (isCommandPrompt) {
        NSString* wineConsole = [[wineBin stringByDeletingLastPathComponent]
                                      stringByAppendingPathComponent:@"wineconsole"];
        if ([[NSFileManager defaultManager] isExecutableFileAtPath:wineConsole]) {
            launchPath = wineConsole;
        }
        arguments = @[@"cmd"];
    } else if (app.isLnk) {
        arguments = @[@"start", app.path];
    } else {
        arguments = @[app.path];
    }
    task.launchPath = launchPath;
    task.arguments = arguments;
    task.environment = env;
    [self setupLoggingForTask:task prefix:selected appName:app.name];

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
        NSLog(@"[Windows] launching app: %@", app.name);
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
        NSString* home = NSHomeDirectory();
        NSString* muplarLogsDir = [[home stringByAppendingPathComponent:@".muplar"] stringByAppendingPathComponent:@"logs"];
        [[NSFileManager defaultManager] createDirectoryAtPath:muplarLogsDir withIntermediateDirectories:YES attributes:nil error:nil];
        NSString* logPath = [muplarLogsDir stringByAppendingPathComponent:[NSString stringWithUTF8String:muplar::runtime::prefix::kMainLogFilename]];
        RotateLogFileIfNeeded(logPath);

        const char* logPathStr = [logPath UTF8String];
        std::freopen(logPathStr, "a", stdout);
        std::freopen(logPathStr, "a", stderr);

        // Without this, SupervisorService/prefix log_* calls default to
        // elfuse's own library-level LOG_WARN threshold and never appear in
        // this log file at all -- there's no --verbose/--quiet flag for a
        // GUI app, so just match the CLI's own default.
        log_init();
        log_set_level(LOG_INFO);

        NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
        formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
        NSString* timestamp = [formatter stringFromDate:[NSDate date]];
        std::printf("\n=== Muplar Instance Manager Launched at %s ===\n", [timestamp UTF8String]);
        std::fflush(stdout);

        NSApplication* app = [NSApplication sharedApplication];
        PrefixManagerAppDelegate* delegate =
            [[PrefixManagerAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
