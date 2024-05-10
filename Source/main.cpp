
#include <stdlib.h>
#include <assert.h>
#include <iostream>

#include "GLFW/glfw3.h"
#include "webgpu/webgpu.h"
#include "glfw3webgpu.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_wgpu.h"

struct WGPUState
{
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    
    int swapchainWidth;
    int swapchainHeight;
    WGPUSurfaceTexture frame;
    WGPUTextureView frameView;
    WGPURenderPassEncoder pass;
    WGPUCommandEncoder encoder;
    WGPUCommandBuffer cmdBuffer;
};

// Returns the DPI scale
float HandleDPI();
WGPUState InitWGPU(GLFWwindow* window);
void CleanupWGPU(WGPUState* state);
void InitDearImgui(GLFWwindow* window, const WGPUState state);
void RenderDearImgui(WGPUState* state);
void CleanupDearImgui();
void WGPUMessageCallback(WGPUErrorType type, char const* message, void* userDataPtr);
void FrameCleanup(WGPUState* state);
void Resize(WGPUState* state, int width, int height);

int main()
{
    // Glfw initialization
    bool ok = glfwInit();
    assert(ok);
    
    float scale = HandleDPI();
    
    // This is required because we're using webgpu
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    GLFWwindow* window = glfwCreateWindow(1200, 800, "Plotter", nullptr, nullptr);
    assert(window);
    
    WGPUState wgpu = InitWGPU(window);
    
    InitDearImgui(window, wgpu);
    
    bool showDemoWindow = true;
    
    // Main loop
    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        
        // React to changes in screen size
        int width, height;
        glfwGetFramebufferSize((GLFWwindow*)window, &width, &height);
        if(width != wgpu.swapchainWidth || height != wgpu.swapchainHeight)
        {
            ImGui_ImplWGPU_InvalidateDeviceObjects();
            Resize(&wgpu, width, height);
            ImGui_ImplWGPU_CreateDeviceObjects();
        }
        
        // Signal the start of frame to imgui
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        if(showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);
        
        RenderDearImgui(&wgpu);
        
        // This is necessary to display validation errors
        wgpuDeviceTick(wgpu.device);
        
        // Swap buffers
        wgpuSurfacePresent(wgpu.surface);
        
        FrameCleanup(&wgpu);
    }
    
    CleanupWGPU(&wgpu);
    CleanupDearImgui();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

float HandleDPI()
{
    float scale = 1.0f;
    
#ifdef _WIN32
    // If it's a high DPI monitor, try to scale everything
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    float xScale, yScale;
    glfwGetMonitorContentScale(monitor, &xScale, &yScale);
    
    if(xScale > 1 || yScale > 1)
    {
        scale = xScale;
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    }
#elif defined(__APPLE__)
    // To prevent weird resizings
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif
    
    return scale;
}

WGPUState InitWGPU(GLFWwindow* window)
{
    WGPUState state = {0};
    
    // Instance
    WGPUInstanceDescriptor instanceDesc = {0};
    instanceDesc.nextInChain = nullptr;
    state.instance = wgpuCreateInstance(&instanceDesc);
    assert(state.instance);
    
    // Surface
    state.surface = glfwGetWGPUSurface(state.instance, window);
    
    // Adapter
    {
        WGPURequestAdapterOptions adapterOpts = {0};
        adapterOpts.nextInChain = nullptr;
        adapterOpts.compatibleSurface = state.surface;
        
        struct UserData
        {
            volatile WGPUAdapter adapter;
            volatile bool requestEnded;
        };
        UserData userData = {0};
        
        auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userDataPtr)
        {
            assert(status == WGPURequestAdapterStatus_Success);
            auto userData = (UserData*)userDataPtr;
            userData->adapter = adapter;
            userData->requestEnded = true;
        };
        
        wgpuInstanceRequestAdapter(state.instance, &adapterOpts, onAdapterRequestEnded, (void*)&userData);
        
        while(!userData.requestEnded);  // It's unclear to me whether this is asynchronous or not... bizzarre either way
        
        assert(userData.adapter);
        state.adapter = userData.adapter;
    }
    
    // Device
    {
        WGPUDeviceDescriptor deviceDesc = {0};
        deviceDesc.nextInChain = nullptr;
        deviceDesc.label = "Device";
        deviceDesc.requiredFeatureCount = 0;
        deviceDesc.requiredLimits = nullptr;
        deviceDesc.defaultQueue.nextInChain = nullptr;
        deviceDesc.defaultQueue.label = "Default queue";
        
        struct UserData
        {
            volatile WGPUDevice device;
            volatile bool requestEnded;
        };
        UserData userData = {0};
        
        auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userDataPtr)
        {
            assert(status == WGPURequestDeviceStatus_Success);
            auto userData = (UserData*)userDataPtr;
            userData->device = device;
            userData->requestEnded = true;
        };
        
        wgpuAdapterRequestDevice(state.adapter, &deviceDesc, onDeviceRequestEnded, (void*)&userData);
        
        while(!userData.requestEnded);  // It's unclear to me whether this is asynchronous or not... bizzarre either way
        
        assert(userData.device);
        state.device = userData.device;
    }
    
    // Queue
    state.queue = wgpuDeviceGetQueue(state.device);
    
    // Error callback
    wgpuDeviceSetUncapturedErrorCallback(state.device, WGPUMessageCallback, nullptr);
    
    // Swapchain
    Resize(&state, 1200, 800);
    return state;
}

void CleanupWGPU(WGPUState* state)
{
    wgpuQueueRelease(state->queue);
	wgpuDeviceRelease(state->device);
	wgpuAdapterRelease(state->adapter);
	wgpuInstanceRelease(state->instance);
	wgpuSurfaceRelease(state->surface);
    
    memset(&state->queue, 0, sizeof(WGPUQueue));
    memset(&state->device, 0, sizeof(WGPUDevice));
    memset(&state->adapter, 0, sizeof(WGPUAdapter));
    memset(&state->instance, 0, sizeof(WGPUInstance));
    memset(&state->surface, 0, sizeof(WGPUSurface));
}

void WGPUMessageCallback(WGPUErrorType type, char const* message, void* userDataPtr)
{
    printf("Uncaptured device error: type %d", type);
    if(message) printf(" (%s)", message);
    printf("\n");
}

void InitDearImgui(GLFWwindow* window, const WGPUState state)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    // Setup style
    ImGui::StyleColorsDark();
    
    // Setup platform backend
    ImGui_ImplGlfw_InitForOther(window, true);
    
    // Setup renderer backend
    ImGui_ImplWGPU_InitInfo initInfo;
    initInfo.Device = state.device;
    initInfo.NumFramesInFlight = 1;
    initInfo.RenderTargetFormat = WGPUTextureFormat_BGRA8Unorm;
    initInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&initInfo);
}

void RenderDearImgui(WGPUState* state)
{
    // Generate the rendering data
    ImGui::Render();
    
    // Prepare frame
    wgpuSurfaceGetCurrentTexture(state->surface, &state->frame);
    
    switch(state->frame.status)
    {
        case WGPUSurfaceGetCurrentTextureStatus_Success: break;
        case WGPUSurfaceGetCurrentTextureStatus_Timeout: fprintf(stderr, "Timed out on GetSurfaceTexture!\n"); return;
        case WGPUSurfaceGetCurrentTextureStatus_Outdated: fprintf(stderr, "Surface texture is outdated!\n"); break;
        case WGPUSurfaceGetCurrentTextureStatus_Lost: fprintf(stderr, "Surface texture was lost\n"); return;
        case WGPUSurfaceGetCurrentTextureStatus_OutOfMemory: fprintf(stderr, "Out of memory!\n"); return;
        case WGPUSurfaceGetCurrentTextureStatus_DeviceLost: fprintf(stderr, "Device lost\n"); return;
        case WGPUSurfaceGetCurrentTextureStatus_Force32: break;
    }
    
    state->frameView = wgpuTextureCreateView(state->frame.texture, nullptr);
    
    WGPURenderPassColorAttachment colorAttachments = {};
    colorAttachments.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachments.loadOp = WGPULoadOp_Clear;
    colorAttachments.storeOp = WGPUStoreOp_Store;
    colorAttachments.clearValue = { 0.5, 0.5, 0.5, 1 };
    colorAttachments.view = state->frameView;
    
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachments;
    renderPassDesc.depthStencilAttachment = nullptr;
    
    WGPUCommandEncoderDescriptor encDesc = {};
    state->encoder = wgpuDeviceCreateCommandEncoder(state->device, &encDesc);
    
    // Perform actual rendering
    state->pass = wgpuCommandEncoderBeginRenderPass(state->encoder, &renderPassDesc);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), state->pass);
    wgpuRenderPassEncoderEnd(state->pass);
    
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    state->cmdBuffer = wgpuCommandEncoderFinish(state->encoder, &cmdBufferDesc);
    wgpuQueueSubmit(state->queue, 1, &state->cmdBuffer);
}

void Resize(WGPUState* state, int width, int height)
{
    WGPUSurfaceConfiguration surfaceConfig = {0};
    surfaceConfig.nextInChain = nullptr;
    surfaceConfig.device = state->device;
    surfaceConfig.format = WGPUTextureFormat_BGRA8Unorm;
    surfaceConfig.usage  = WGPUTextureUsage_RenderAttachment;
    surfaceConfig.viewFormatCount = 0;
    surfaceConfig.viewFormats = nullptr;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    surfaceConfig.width = width;
    surfaceConfig.height = height;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(state->surface, &surfaceConfig);
    
    state->swapchainWidth = width;
    state->swapchainHeight = height;
}

void FrameCleanup(WGPUState* state)
{
    wgpuTextureRelease(state->frame.texture);
    wgpuTextureViewRelease(state->frameView);
    wgpuRenderPassEncoderRelease(state->pass);
    wgpuCommandEncoderRelease(state->encoder);
    wgpuCommandBufferRelease(state->cmdBuffer);
}

void CleanupDearImgui()
{
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
