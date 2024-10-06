#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include "imgui.h"
#include "backends/imgui_impl_metal.h"
#include "backends/imgui_impl_osx.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) MTKView *metalView;
@property (nonatomic, strong) id<MTLDevice> metalDevice;
@end

@implementation AppDelegate {
    id<MTLCommandQueue> commandQueue;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    // Setup Metal
    self.metalDevice = MTLCreateSystemDefaultDevice();
    self.metalView = [[MTKView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) device:self.metalDevice];
    self.metalView.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
    commandQueue = [self.metalDevice newCommandQueue];

    // Setup NSWindow
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 800, 600)
                                               styleMask:(NSWindowStyleMaskTitled |
                                                          NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskResizable)
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    [self.window setContentView:self.metalView];
    [self.window setTitle:@"Metal ImGui Window"];
    [self.window makeKeyAndOrderFront:nil];

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplMetal_Init(self.metalDevice);
    ImGui_ImplOSX_Init(self.window.contentView);

    // Setup a timer to render frames
    [NSTimer scheduledTimerWithTimeInterval:1.0/60.0
                                     target:self
                                   selector:@selector(render)
                                   userInfo:nil
                                    repeats:YES];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
}

// Render method
// Render method
- (void)render {
    @autoreleasepool {
        // Create a new frame for ImGui with the Metal render pass descriptor
        MTLRenderPassDescriptor *renderPassDescriptor = self.metalView.currentRenderPassDescriptor;
        if (!renderPassDescriptor) {
            return; // If we can't get the render pass descriptor, skip this frame
        }

        // Start ImGui frame for Metal backend
        ImGui_ImplMetal_NewFrame(renderPassDescriptor);

        // Start ImGui frame for macOS backend - pass the view!
        ImGui_ImplOSX_NewFrame(self.metalView);

        // Create a new ImGui frame
        ImGui::NewFrame();

        // Create UI components
        ImGui::Begin("Hello, Metal!");
        ImGui::Text("This is a Metal-powered ImGui window.");
        ImGui::End();

        // Render ImGui frame
        ImGui::Render();

        // Encoding and command buffer setup
        id<CAMetalDrawable> drawable = self.metalView.currentDrawable;
        if (drawable) {
            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
            id<MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

            // Render ImGui's draw data using the Metal backend - pass all three arguments!
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, commandEncoder);

            // End encoding and present the drawable
            [commandEncoder endEncoding];
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
        }
    }
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}
