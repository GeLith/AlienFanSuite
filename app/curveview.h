#import <AppKit/AppKit.h>
#import "curve.h"

@protocol AFCurveViewDelegate <NSObject>
- (void)curveViewDidChange:(AFPreset *)preset;
@end

@interface AFCurveView : NSView
@property (nonatomic, strong) AFPreset *preset;
@property (nonatomic) CGFloat currentTemp;
@property (nonatomic) CGFloat currentBoost;
@property (nonatomic, weak) id<AFCurveViewDelegate> delegate;
@end
