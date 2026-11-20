#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

@interface AppDelegate : NSObject <NSApplicationDelegate, WKNavigationDelegate, NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@end
