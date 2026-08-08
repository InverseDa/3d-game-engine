#include "Mac/MacPlatformWindow.h"

#include "Containers/String.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <new>

@interface FMacPlatformWindowDelegate : NSObject<NSWindowDelegate>
{
@public
    LE::FMacPlatformWindow* Owner;
}
@end

@implementation FMacPlatformWindowDelegate

- (void)windowWillClose:(NSNotification*)Notification
{
    (void)Notification;
    if (Owner != nullptr)
    {
        Owner->NotifyClosed();
    }
}

- (void)windowDidResize:(NSNotification*)Notification
{
    NSWindow* Window = [Notification object];
    if (Owner == nullptr || Window == nil)
    {
        return;
    }

    const NSRect ContentFrame = [[Window contentView] bounds];
    Owner->NotifyResized(
        static_cast<LE::uint32>(ContentFrame.size.width),
        static_cast<LE::uint32>(ContentFrame.size.height));
}

- (void)windowDidMiniaturize:(NSNotification*)Notification
{
    (void)Notification;
    if (Owner != nullptr)
    {
        Owner->NotifyMinimized(true);
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)Notification
{
    (void)Notification;
    if (Owner != nullptr)
    {
        Owner->NotifyMinimized(false);
    }
}

- (BOOL)windowShouldClose:(NSWindow*)Sender
{
    (void)Sender;
    if (Owner != nullptr)
    {
        Owner->NotifyClosed();
    }
    return YES;
}

@end

namespace LE
{

namespace
{
void InitializeApplication()
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];
}

void DestroyPlatformWindow(void*, FPlatformWindow* const Window) noexcept
{
    delete Window;
}
}

FMacPlatformWindow::FMacPlatformWindow(const FPlatformWindowDesc& Description)
    : Width(Description.Width)
    , Height(Description.Height)
{
    @autoreleasepool
    {
        InitializeApplication();

        const NSRect Frame = NSMakeRect(
            0.0,
            0.0,
            static_cast<CGFloat>(Width),
            static_cast<CGFloat>(Height));
        NSWindow* Window = [[NSWindow alloc]
            initWithContentRect:Frame
                      styleMask:(NSWindowStyleMaskTitled |
                                 NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable |
                                 NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        if (Window == nil)
        {
            return;
        }

        const LE::String Title(Description.Title);
        NSString* WindowTitle = [NSString stringWithUTF8String:
            (Title.IsEmpty() ? "Limitless Engine" : Title.Data())];
        [Window setTitle:WindowTitle];

        NSView* ContentView = [[NSView alloc] initWithFrame:Frame];
        if (ContentView == nil)
        {
            [Window close];
            return;
        }
        [ContentView setWantsLayer:YES];

        CAMetalLayer* MetalLayer = [CAMetalLayer layer];
        if (MetalLayer == nil)
        {
            [Window close];
            return;
        }
        [MetalLayer setContentsScale:[Window backingScaleFactor]];
        [ContentView setLayer:MetalLayer];
        [Window setContentView:ContentView];

        FMacPlatformWindowDelegate* Delegate = [[FMacPlatformWindowDelegate alloc] init];
        if (Delegate == nil)
        {
            [Window close];
            return;
        }
        Delegate->Owner = this;
        [Window setDelegate:Delegate];

        WindowHandle = (__bridge_retained void*)Window;
        ViewHandle = (__bridge_retained void*)ContentView;
        LayerHandle = (__bridge_retained void*)MetalLayer;
        DelegateHandle = (__bridge_retained void*)Delegate;

        UpdateDrawableSize();
        [Window center];
        [Window makeKeyAndOrderFront:nil];
    }
}

FMacPlatformWindow::~FMacPlatformWindow()
{
    PendingEvents.Clear();
    @autoreleasepool
    {
        NSWindow* Window = (__bridge_transfer NSWindow*)WindowHandle;
        NSView* View = (__bridge_transfer NSView*)ViewHandle;
        CAMetalLayer* MetalLayer = (__bridge_transfer CAMetalLayer*)LayerHandle;
        FMacPlatformWindowDelegate* Delegate =
            (__bridge_transfer FMacPlatformWindowDelegate*)DelegateHandle;

        WindowHandle = nullptr;
        ViewHandle = nullptr;
        LayerHandle = nullptr;
        DelegateHandle = nullptr;

        if (Window != nil)
        {
            [Window setDelegate:nil];
            [Window orderOut:nil];
            [Window close];
        }

        (void)View;
        (void)MetalLayer;
        (void)Delegate;
    }
}

FPlatformSurface FMacPlatformWindow::GetSurface() const noexcept
{
    FPlatformSurface Surface;
    Surface.Type = EPlatformSurfaceType::MetalLayer;
    Surface.WindowHandle = WindowHandle;
    Surface.ViewHandle = ViewHandle;
    Surface.LayerHandle = LayerHandle;
    return Surface;
}

void FMacPlatformWindow::PumpEvents(FPlatformEventQueue& OutEvents)
{
    @autoreleasepool
    {
        for (;;)
        {
            NSEvent* Event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (Event == nil)
            {
                break;
            }
            [NSApp sendEvent:Event];
        }
        [NSApp updateWindows];
    }

    DrainPendingEvents(OutEvents);
}

void FMacPlatformWindow::NotifyClosed()
{
    if (!bCloseRequested)
    {
        bCloseRequested = true;
        PushEvent(EPlatformEventType::CloseRequested);
    }
}

void FMacPlatformWindow::NotifyResized(const uint32 NewWidth, const uint32 NewHeight)
{
    Width = NewWidth;
    Height = NewHeight;
    UpdateDrawableSize();
    PushEvent(EPlatformEventType::Resized);
}

void FMacPlatformWindow::NotifyMinimized(const bool bIsMinimized)
{
    if (bMinimized == bIsMinimized)
    {
        return;
    }
    bMinimized = bIsMinimized;
    PushEvent(bMinimized ? EPlatformEventType::Minimized : EPlatformEventType::Restored);
}

void FMacPlatformWindow::PushEvent(const EPlatformEventType Type)
{
    FPlatformEvent Event;
    Event.Type = Type;
    Event.WindowId = GetId();
    Event.Width = Width;
    Event.Height = Height;
    PendingEvents.Push(Event);
}

void FMacPlatformWindow::DrainPendingEvents(FPlatformEventQueue& OutEvents)
{
    FPlatformEvent Event;
    while (PendingEvents.Poll(Event))
    {
        OutEvents.Push(Event);
    }
}

void FMacPlatformWindow::UpdateDrawableSize() const
{
    CAMetalLayer* MetalLayer = (__bridge CAMetalLayer*)LayerHandle;
    NSWindow* Window = (__bridge NSWindow*)WindowHandle;
    NSView* View = (__bridge NSView*)ViewHandle;
    if (MetalLayer == nil || Window == nil || View == nil)
    {
        return;
    }

    const CGFloat Scale = [Window backingScaleFactor];
    const NSRect Bounds = [View bounds];
    [MetalLayer setContentsScale:Scale];
    [MetalLayer setDrawableSize:CGSizeMake(Bounds.size.width * Scale, Bounds.size.height * Scale)];
}

FPlatformWindowPtr CreatePlatformWindow(const FPlatformWindowDesc& Description)
{
    FMacPlatformWindow* const Window = new (std::nothrow) FMacPlatformWindow(Description);
    if (Window == nullptr)
    {
        return {};
    }
    if (!Window->IsValid())
    {
        delete Window;
        return {};
    }
    return FPlatformWindowPtr::Adopt(Window, nullptr, &DestroyPlatformWindow);
}

} // namespace LE
