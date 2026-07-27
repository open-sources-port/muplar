#import <AppKit/AppKit.h>

@interface AndroidDeviceShell : NSObject <NSWindowDelegate>
@property(nonatomic, strong, readonly) NSWindow* window;
@property(nonatomic, copy) void (^closeHandler)(void);
@property(nonatomic, copy) void (^actionHandler)(NSString* action,
                                                NSString* tabIdentifier);
@property(nonatomic, copy) void (^tabFocusHandler)(NSString* tabIdentifier);
@property(nonatomic, copy) void (^tabCloseHandler)(NSString* tabIdentifier);
- (instancetype)initWithPrefixName:(NSString*)prefixName;
- (void)focusOrCreateTabWithIdentifier:(NSString*)identifier title:(NSString*)title;
- (void)focusLauncherTab;
- (void)showLaunchingApp:(NSString*)appName;
- (void)showRunningApp:(NSString*)appName;
- (void)showStoppedWithMessage:(NSString*)message;
@end
