#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

@interface AppDelegate : NSObject <NSApplicationDelegate, WKNavigationDelegate>
@property (nonatomic, strong) NSWindow *window;
@end
