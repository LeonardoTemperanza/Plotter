
#include <stdlib.h>
#include <assert.h>
#include <iostream>

#include "glfw3-4_include/glfw3.h"
#include "dawn2024-05-05_include/webgpu.h"
#include "glfw3webgpu.h"

struct WGPUState
{
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSwapChain swapchain;
};

// Returns the DPI scale
float HandleDPI();
WGPUState InitWGPU(GLFWwindow* window);
void CleanupWGPU(WGPUState* state);
void InitDearImgui(GLFWwindow* window);
void WGPUMessageCallback(WGPUErrorType type, char const* message, void* userDataPtr);

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
    
    InitDearImgui(window);
    
    // Main loop
    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        
    }
    
    CleanupWGPU(&wgpu);
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
    {
        WGPUSwapChainDescriptor swapchainDesc = {0};
        swapchainDesc.width = 1200;
        swapchainDesc.height = 800;
        swapchainDesc.usage = WGPUTextureUsage_RenderAttachment;
        swapchainDesc.format = WGPUTextureFormat_BGRA8Unorm;
        swapchainDesc.presentMode = WGPUPresentMode_Fifo;
        
        state.swapchain = wgpuDeviceCreateSwapChain(state.device, state.surface, &swapchainDesc);
    }
    
    return state;
}

void InitDearImgui(GLFWwindow* window)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    // Setup style
    ImGui::StyleColorsDark();
    
    // Setup platform and renderer backends
    
}

void CleanupWGPU(WGPUState* state)
{
    wgpuQueueRelease(state->queue);
	wgpuSwapChainRelease(state->swapchain);
	wgpuDeviceRelease(state->device);
	wgpuAdapterRelease(state->adapter);
	wgpuInstanceRelease(state->instance);
	wgpuSurfaceRelease(state->surface);
    
    memset(&state->queue, 0, sizeof(WGPUQueue));
    memset(&state->swapchain, 0, sizeof(WGPUSwapChain));
    memset(&state->device, 0, sizeof(WGPUDevice));
    memset(&state->adapter, 0, sizeof(WGPUAdapter));
    memset(&state->instance, 0, sizeof(WGPUInstance));
    memset(&state->surface, 0, sizeof(WGPUSurface));
}

void WGPUMessageCallback(WGPUErrorType type, char const* message, void* userDataPtr)
{
    std::cout << "Uncaptured device error: type " << type;
    if(message) std::cout << " (" << message << ")";
    std::cout << std::endl;
}
