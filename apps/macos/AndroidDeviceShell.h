#import <AppKit/AppKit.h>

@interface AndroidDeviceShell : NSObject <NSWindowDelegate>
@property(nonatomic, strong, readonly) NSWindow* window;
@property(nonatomic, copy) void (^closeHandler)(void);
@property(nonatomic, copy) void (^actionHandler)(NSString* action,
                                                NSString* tabIdentifier);
@property(nonatomic, copy) void (^tabFocusHandler)(NSString* tabIdentifier);
@property(nonatomic, copy) void (^tabCloseHandler)(NSString* tabIdentifier);
@property(nonatomic, copy) void (^inputHandler)(NSString* tabIdentifier,
                                               int32_t type,
                                               int32_t action,
                                               int32_t source,
                                               int32_t deviceId,
                                               int32_t keyCode,
                                               float x,
                                               float y);
@property(nonatomic, copy) void (^installApkHandler)(NSString* apkPath);
- (instancetype)initWithPrefixName:(NSString*)prefixName;
- (void)focusOrCreateTabWithIdentifier:(NSString*)identifier title:(NSString*)title;
- (void)focusLauncherTab;
- (void)closeTabWithIdentifier:(NSString*)identifier;
- (void)startFrameServerAtPath:(NSString*)path;
- (void)showLaunchingApp:(NSString*)appName;
- (void)showRunningApp:(NSString*)appName;
- (void)showStoppedWithMessage:(NSString*)message;
- (void)showInstallProgress:(NSString*)message;
- (void)showInstallSuccess:(NSString*)message;
- (void)dismissInstallProgress;
- (void)deviceInstall:(id)sender;
@end
