#import "curveview.h"

#define TEMP_MIN 30.0
#define TEMP_MAX 100.0
#define PAD_L 48.0
#define PAD_R 18.0
#define PAD_T 18.0
#define PAD_B 34.0

@implementation AFCurveView {
    NSInteger _dragIndex;
    BOOL _dragging;
}

- (BOOL)isFlipped { return YES; }

- (NSRect)plotRect
{
    return NSMakeRect(PAD_L, PAD_T,
                      self.bounds.size.width - PAD_L - PAD_R,
                      self.bounds.size.height - PAD_T - PAD_B);
}

- (NSPoint)pointForTemp:(CGFloat)t boost:(CGFloat)b
{
    NSRect r = [self plotRect];
    CGFloat x = r.origin.x + (t - TEMP_MIN) / (TEMP_MAX - TEMP_MIN) * r.size.width;
    CGFloat y = r.origin.y + r.size.height - (b / 100.0) * r.size.height;
    return NSMakePoint(x, y);
}

- (NSPoint)tempBoostForPoint:(NSPoint)p
{
    NSRect r = [self plotRect];
    CGFloat t = TEMP_MIN + (p.x - r.origin.x) / r.size.width * (TEMP_MAX - TEMP_MIN);
    CGFloat b = (r.origin.y + r.size.height - p.y) / r.size.height * 100.0;
    return NSMakePoint(t, b);
}

- (void)drawRect:(NSRect)dirtyRect
{
    [[NSColor colorWithCalibratedWhite:0.98 alpha:1] setFill];
    NSRectFill(self.bounds);

    NSRect r = [self plotRect];
    if (r.size.width <= 0 || r.size.height <= 0) return;

    /* 网格 */
    [[NSColor colorWithCalibratedWhite:0.88 alpha:1] setStroke];
    for (int t = 30; t <= 100; t += 10) {
        NSPoint p = [self pointForTemp:t boost:0];
        NSBezierPath *line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(p.x, r.origin.y)];
        [line lineToPoint:NSMakePoint(p.x, NSMaxY(r))];
        [line stroke];
    }
    for (int b = 0; b <= 100; b += 20) {
        NSPoint p = [self pointForTemp:30 boost:b];
        NSBezierPath *line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(r.origin.x, p.y)];
        [line lineToPoint:NSMakePoint(NSMaxX(r), p.y)];
        [line stroke];
    }

    /* 坐标轴标签 */
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new];
    ps.alignment = NSCenterTextAlignment;
    NSDictionary *attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10],
        NSForegroundColorAttributeName: [NSColor secondaryLabelColor],
        NSParagraphStyleAttributeName: ps
    };
    for (int t = 30; t <= 100; t += 10) {
        NSPoint p = [self pointForTemp:t boost:0];
        [@(t).stringValue drawInRect:NSMakeRect(p.x - 16, NSMaxY(r) + 6, 32, 16)
                      withAttributes:attr];
    }
    for (int b = 0; b <= 100; b += 20) {
        NSPoint p = [self pointForTemp:30 boost:b];
        [[NSString stringWithFormat:@"%d%%", b] drawInRect:NSMakeRect(4, p.y - 8, 38, 16)
                                                withAttributes:@{
            NSFontAttributeName: [NSFont systemFontOfSize:10],
            NSForegroundColorAttributeName: [NSColor secondaryLabelColor]
        }];
    }

    /* 当前温度竖线 */
    if (_currentTemp >= TEMP_MIN && _currentTemp <= TEMP_MAX) {
        NSPoint p = [self pointForTemp:_currentTemp boost:0];
        [[NSColor colorWithCalibratedRed:0.95 green:0.35 blue:0.3 alpha:0.65] setStroke];
        NSBezierPath *l = [NSBezierPath bezierPath];
        [l moveToPoint:NSMakePoint(p.x, r.origin.y)];
        [l lineToPoint:NSMakePoint(p.x, NSMaxY(r))];
        [l setLineWidth:1.5];
        [l stroke];
    }

    if (!_preset || _preset.points.count == 0) return;

    NSArray *pts = [_preset.points sortedArrayUsingComparator:^NSComparisonResult(AFCurvePoint *a, AFCurvePoint *b) {
        return a.temp < b.temp ? NSOrderedAscending : (a.temp > b.temp ? NSOrderedDescending : NSOrderedSame);
    }];

    /* 曲线下的填充 */
    NSBezierPath *fill = [NSBezierPath bezierPath];
    NSPoint first = [self pointForTemp:((AFCurvePoint *)pts[0]).temp boost:((AFCurvePoint *)pts[0]).boost];
    [fill moveToPoint:NSMakePoint(first.x, r.origin.y)];
    [fill lineToPoint:first];
    for (AFCurvePoint *p in pts) {
        NSPoint q = [self pointForTemp:p.temp boost:p.boost];
        [fill lineToPoint:q];
    }
    AFCurvePoint *lastP = pts.lastObject;
    [fill lineToPoint:NSMakePoint([self pointForTemp:lastP.temp boost:0].x, r.origin.y)];
    [fill closePath];
    [[NSColor colorWithCalibratedRed:0.2 green:0.55 blue:0.95 alpha:0.13] setFill];
    [fill fill];

    /* 曲线 */
    NSBezierPath *curve = [NSBezierPath bezierPath];
    [curve moveToPoint:first];
    for (AFCurvePoint *p in pts)
        [curve lineToPoint:[self pointForTemp:p.temp boost:p.boost]];
    [[NSColor colorWithCalibratedRed:0.15 green:0.5 blue:0.92 alpha:1] setStroke];
    [curve setLineWidth:2.5];
    [curve stroke];

    /* 当前工作点 */
    if (_currentBoost > 0) {
        NSPoint cp = [self pointForTemp:_currentTemp boost:_currentBoost];
        [[NSColor colorWithCalibratedRed:0.95 green:0.35 blue:0.3 alpha:1] setFill];
        NSRect dot = NSMakeRect(cp.x - 4, cp.y - 4, 8, 8);
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
    }

    /* 控制点 */
    for (NSInteger i = 0; i < (NSInteger)pts.count; i++) {
        NSPoint p = [self pointForTemp:((AFCurvePoint *)pts[i]).temp boost:((AFCurvePoint *)pts[i]).boost];
        NSRect dot = NSMakeRect(p.x - 6, p.y - 6, 12, 12);
        [[NSColor colorWithCalibratedWhite:1 alpha:1] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
        [[NSColor colorWithCalibratedRed:0.15 green:0.5 blue:0.92 alpha:1] setStroke];
        NSBezierPath *ring = [NSBezierPath bezierPathWithOvalInRect:dot];
        [ring setLineWidth:2];
        [ring stroke];
    }
}

- (NSInteger)hitTestPoint:(NSPoint)p
{
    NSArray *pts = [_preset.points sortedArrayUsingComparator:^NSComparisonResult(AFCurvePoint *a, AFCurvePoint *b) {
        return a.temp < b.temp ? NSOrderedAscending : (a.temp > b.temp ? NSOrderedDescending : NSOrderedSame);
    }];
    for (NSInteger i = 0; i < (NSInteger)pts.count; i++) {
        NSPoint q = [self pointForTemp:((AFCurvePoint *)pts[i]).temp boost:((AFCurvePoint *)pts[i]).boost];
        if (hypot(q.x - p.x, q.y - p.y) < 14) return i;
    }
    return -1;
}

- (void)mouseDown:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger hit = [self hitTestPoint:p];
    if (hit >= 0) {
        _dragIndex = hit;
        _dragging = YES;
    } else {
        /* 空白处双击由 mouseUp 的 clickCount 判断；单击先选中最近区间用于新增 */
        _dragIndex = -1;
    }
}

- (void)mouseDragged:(NSEvent *)event
{
    if (!_dragging || !_preset) return;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSPoint tb = [self tempBoostForPoint:p];

    NSArray *sorted = [_preset.points sortedArrayUsingComparator:^NSComparisonResult(AFCurvePoint *a, AFCurvePoint *b) {
        return a.temp < b.temp ? NSOrderedAscending : (a.temp > b.temp ? NSOrderedDescending : NSOrderedSame);
    }];
    if (_dragIndex >= (NSInteger)sorted.count) return;
    AFCurvePoint *pt = sorted[_dragIndex];

    pt.temp = MAX(TEMP_MIN + 2, MIN(TEMP_MAX - 2, tb.x));
    pt.boost = MAX(0, MIN(100, tb.y));
    [self setNeedsDisplay:YES];
    [_delegate curveViewDidChange:_preset];
}

- (void)mouseUp:(NSEvent *)event
{
    if (event.clickCount == 2 && !_dragging && _preset) {
        NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
        NSPoint tb = [self tempBoostForPoint:p];
        if (tb.x >= TEMP_MIN && tb.x <= TEMP_MAX && tb.y >= 0 && tb.y <= 100) {
            [_preset.points addObject:[[AFCurvePoint alloc] initWithTemp:tb.x boost:tb.y]];
            [self setNeedsDisplay:YES];
            [_delegate curveViewDidChange:_preset];
        }
    }
    _dragging = NO;
    _dragIndex = -1;
}

- (void)rightMouseDown:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger hit = [self hitTestPoint:p];
    if (hit >= 0 && _preset.points.count > 2) {
        NSArray *sorted = [_preset.points sortedArrayUsingComparator:^NSComparisonResult(AFCurvePoint *a, AFCurvePoint *b) {
            return a.temp < b.temp ? NSOrderedAscending : (a.temp > b.temp ? NSOrderedDescending : NSOrderedSame);
        }];
        [_preset.points removeObject:sorted[hit]];
        [self setNeedsDisplay:YES];
        [_delegate curveViewDidChange:_preset];
    }
}
@end
