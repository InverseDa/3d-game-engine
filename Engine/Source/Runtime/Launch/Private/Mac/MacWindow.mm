#include "CoreMinimal.h"

#if PLATFORM_MAC

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "Mac/MacWindow.h"

@interface FMacWindowDelegate : NSObject<NSWindowDelegate>
{
@public
    LE::FMacWindow* Owner;
}
@end

@implementation FMacWindowDelegate

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
static void InitializeApplication()
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];
}
}

FMacWindow::FMacWindow(uint32 InWidth, uint32 InHeight, const char* Title)
    : Width(InWidth)
    , Height(InHeight)
{
    @autoreleasepool
    {
        InitializeApplication();

        const NSRect Frame = NSMakeRect(0.0, 0.0, static_cast<CGFloat>(Width), static_cast<CGFloat>(Height));
        NSWindow* Window = [[NSWindow alloc] initWithContentRect:Frame
                                                       styleMask:(NSWindowStyleMaskTitled |
                                                                  NSWindowStyleMaskClosable |
                                                                  NSWindowStyleMaskMiniaturizable |
                                                                  NSWindowStyleMaskResizable)
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        if (Window == nil)
        {
            bRunning = false;
            return;
        }

        NSString* WindowTitle = [NSString stringWithUTF8String:(Title != nullptr ? Title : "Limitless Engine")];
        [Window setTitle:WindowTitle];

        NSView* ContentView = [[NSView alloc] initWithFrame:Frame];
        [ContentView setWantsLayer:YES];

        CAMetalLayer* MetalLayer = [CAMetalLayer layer];
        [MetalLayer setContentsScale:[Window backingScaleFactor]];
        [ContentView setLayer:MetalLayer];
        [Window setContentView:ContentView];

        FMacWindowDelegate* Delegate = [[FMacWindowDelegate alloc] init];
        Delegate->Owner = this;
        [Window setDelegate:Delegate];

        this->WindowHandle = (__bridge_retained void*)Window;
        this->ViewHandle = (__bridge_retained void*)ContentView;
        this->LayerHandle = (__bridge_retained void*)MetalLayer;
        this->DelegateHandle = (__bridge_retained void*)Delegate;

        this->UpdateDrawableSize();

        [Window center];
        [Window makeKeyAndOrderFront:nil];
    }
}

FMacWindow::~FMacWindow()
{
    @autoreleasepool
    {
        NSWindow* Window = (__bridge_transfer NSWindow*)this->WindowHandle;
        NSView* View = (__bridge_transfer NSView*)this->ViewHandle;
        CAMetalLayer* MetalLayer = (__bridge_transfer CAMetalLayer*)this->LayerHandle;
        FMacWindowDelegate* Delegate = (__bridge_transfer FMacWindowDelegate*)this->DelegateHandle;

        this->WindowHandle = nullptr;
        this->ViewHandle = nullptr;
        this->LayerHandle = nullptr;
        this->DelegateHandle = nullptr;

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

void* FMacWindow::GetNativeHandle() const
{
    return this->WindowHandle;
}

LE::FRALSurfaceDesc FMacWindow::GetSurfaceDesc() const
{
    LE::FRALSurfaceDesc SurfaceDesc;
    SurfaceDesc.Type = LE::ERALSurfaceType::MetalLayer;
    SurfaceDesc.WindowHandle = this->WindowHandle;
    SurfaceDesc.ViewHandle = this->ViewHandle;
    SurfaceDesc.LayerHandle = this->LayerHandle;
    return SurfaceDesc;
}

bool FMacWindow::ProcessMessages()
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

    return this->bRunning;
}

void FMacWindow::GetSize(uint32& OutWidth, uint32& OutHeight) const
{
    OutWidth = this->Width;
    OutHeight = this->Height;
}

bool FMacWindow::IsMinimized() const
{
    NSWindow* Window = (__bridge NSWindow*)this->WindowHandle;
    return Window != nil ? [Window isMiniaturized] : false;
}

void FMacWindow::NotifyClosed()
{
    this->bRunning = false;
}

void FMacWindow::NotifyResized(uint32 NewWidth, uint32 NewHeight)
{
    this->Width = NewWidth;
    this->Height = NewHeight;
    this->UpdateDrawableSize();
}

void FMacWindow::UpdateDrawableSize() const
{
    CAMetalLayer* MetalLayer = (__bridge CAMetalLayer*)this->LayerHandle;
    NSWindow* Window = (__bridge NSWindow*)this->WindowHandle;
    NSView* View = (__bridge NSView*)this->ViewHandle;
    if (MetalLayer == nil || Window == nil || View == nil)
    {
        return;
    }

    const CGFloat Scale = [Window backingScaleFactor];
    const NSRect Bounds = [View bounds];
    [MetalLayer setContentsScale:Scale];
    [MetalLayer setDrawableSize:CGSizeMake(Bounds.size.width * Scale, Bounds.size.height * Scale)];
}

} // namespace LE

#endif
