#include <cstdlib>
#include <cstddef>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// The VST3 and the AU version of Triad Split are separate binaries; when a DAW loads both, they still
// have to find the same link groups. The Objective-C runtime is one per process, so the block hangs
// off the process-info object under a selector named after it (selectors are unique per process).
// The memory is never freed: a few kilobytes, kept until the DAW quits (a binary may be unloaded
// while another still uses the block).
void* triadProcessWideBlock (const char* name, size_t size, void (*construct) (void*))
{
    @autoreleasepool
    {
        NSProcessInfo* holder = [NSProcessInfo processInfo];
        const SEL key = sel_registerName (name);
        @synchronized (holder)
        {
            NSValue* existing = objc_getAssociatedObject (holder, key);
            if (existing != nil)
                return [existing pointerValue];
            void* block = std::calloc (1, size);
            construct (block);
            objc_setAssociatedObject (holder, key, [NSValue valueWithPointer: block], OBJC_ASSOCIATION_RETAIN);
            return block;
        }
    }
}
