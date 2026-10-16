// macOS helpers for the stress test: does this Mac draw OpenGL with a graphics card, and does a
// window contain an OpenGL view? (CI Macs are virtual machines without one: OpenGL falls back to a
// software renderer so slow that one plug-in window can hold up the whole test.)
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGL/gl.h>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// The OpenGL renderer's name ("Apple M1", "Apple Software Renderer", ...), or empty when there is none.
std::string triadStressOpenGLRenderer()
{
    std::string name;
    NSOpenGLPixelFormatAttribute attributes[] = { NSOpenGLPFAAccelerated, NSOpenGLPFANoRecovery, NSOpenGLPFADoubleBuffer, 0 };
    NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes: attributes];
    if (format == nil) return name;
    NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat: format shareContext: nil];
    if (context != nil)
    {
        [context makeCurrentContext];
        if (const auto* renderer = (const char*) glGetString (GL_RENDERER)) name = renderer;
        [NSOpenGLContext clearCurrentContext];
    }
   #if ! __has_feature(objc_arc)
    [context release];
    [format release];
   #endif
    return name;
}

static bool usesOpenGL (NSView* view, int depth)
{
    if (view == nil || depth > 40) return false;
    if ([view isKindOfClass: [NSOpenGLView class]]) return true;
    if (view.layer != nil && [view.layer isKindOfClass: [CAOpenGLLayer class]]) return true;
    for (NSView* child in view.subviews)
        if (usesOpenGL (child, depth + 1)) return true;
    return false;
}

// `nativeHandle` is a JUCE peer's native handle (an NSView).
bool triadStressUsesOpenGL (void* nativeHandle)
{
    NSView* view = (NSView*) nativeHandle;
    if (view == nil) return false;
    if (view.window != nil && view.window.contentView != nil) view = view.window.contentView;
    return usesOpenGL (view, 0);
}

#pragma clang diagnostic pop
