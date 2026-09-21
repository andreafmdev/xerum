#include "standalone/XerumWindowMac.h"

#import <Cocoa/Cocoa.h>

namespace
{
NSWindow* windowOf (void* nsViewHandle)
{
    if (nsViewHandle == nullptr)
        return nil;

    return [(__bridge NSView*) nsViewHandle window];
}
} // namespace

namespace xerum
{
void makeWindowChromeless (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);

    if (window == nil)
        return;

    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

double trafficLightWidth (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);

    if (window == nil)
        return 0.0;

    NSButton* zoom = [window standardWindowButton: NSWindowZoomButton];

    if (zoom == nil)
        return 0.0;

    // Il bordo destro del bottone piu' a destra, piu' lo stesso margine che i tre hanno a
    // sinistra: e' lo spazio che la UI non deve occupare.
    NSButton* close = [window standardWindowButton: NSWindowCloseButton];
    const CGFloat leftMargin = close != nil ? NSMinX (close.frame) : 0.0;

    return (double) (NSMaxX (zoom.frame) + leftMargin);
}

bool beginNativeWindowDrag (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);
    NSEvent* event = [NSApp currentEvent];

    if (window == nil || event == nil || event.type != NSEventTypeLeftMouseDown)
        return false;

    [window performWindowDragWithEvent: event];
    return true;
}

void toggleWindowZoom (void* nsViewHandle)
{
    [windowOf (nsViewHandle) zoom: nil];
}
} // namespace xerum
