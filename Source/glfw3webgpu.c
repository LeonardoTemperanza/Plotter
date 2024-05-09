/**
 * This is an extension of GLFW for WebGPU, abstracting away the details of
 * OS-specific operations.
 * 
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://eliemichel.github.io/LearnWebGPU
 * 
 * Most of this code comes from the wgpu-native triangle example:
 *   https://github.com/gfx-rs/wgpu-native/blob/master/examples/triangle/main.c
 * 
 * MIT License
 * Copyright (c) 2022-2023 Elie Michel and the wgpu-native authors
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// NOTE(Leo): This has been adapted by me to also compile on C++20
// by removing the C-exclusive parts.

#include "glfw3webgpu.h"

#include <webgpu/webgpu.h>

#define WGPU_TARGET_MACOS 1
#define WGPU_TARGET_LINUX_X11 2
#define WGPU_TARGET_WINDOWS 3
#define WGPU_TARGET_LINUX_WAYLAND 4
#define WGPU_TARGET_EMSCRIPTEN 5

#if defined(__EMSCRIPTEN__)
#define WGPU_TARGET WGPU_TARGET_EMSCRIPTEN
#elif defined(_WIN32)
#define WGPU_TARGET WGPU_TARGET_WINDOWS
#elif defined(__APPLE__)
#define WGPU_TARGET WGPU_TARGET_MACOS
#elif defined(_GLFW_WAYLAND)
#define WGPU_TARGET WGPU_TARGET_LINUX_WAYLAND
#else
#define WGPU_TARGET WGPU_TARGET_LINUX_X11
#endif

#if WGPU_TARGET == WGPU_TARGET_MACOS
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#include <GLFW/glfw3.h>
#if WGPU_TARGET == WGPU_TARGET_MACOS
#define GLFW_EXPOSE_NATIVE_COCOA
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
#define GLFW_EXPOSE_NATIVE_X11
#elif WGPU_TARGET == WGPU_TARGET_LINUX_WAYLAND
#define GLFW_EXPOSE_NATIVE_WAYLAND
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#if !defined(__EMSCRIPTEN__)
#include <GLFW/glfw3native.h>
#endif

WGPUSurface glfwGetWGPUSurface(WGPUInstance instance, GLFWwindow* window) {
#if WGPU_TARGET == WGPU_TARGET_MACOS
    {
        id metal_layer = NULL;
        NSWindow* ns_window = glfwGetCocoaWindow(window);
        [ns_window.contentView setWantsLayer : YES] ;
        metal_layer = [CAMetalLayer layer];
        [ns_window.contentView setLayer : metal_layer] ;
        
        WGPUChainedStruct chainedStruct = {
            .next = NULL,
            .sType = WGPUSType_SurfaceDescriptorFromMetalLayer
        };
        
        WGPUSurfaceDescriptorFromMetalLayer tmp = {
            .chain = chainedStruct,
            .layer = metal_layer
        };
        
        WGPUSurfaceDescriptor surfaceDesc = {
            .nextInChain = (const WGPUChainedStruct*)&tmp,
            .label = NULL,
        };
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    }
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
    {
        Display* x11_display = glfwGetX11Display();
        Window x11_window = glfwGetX11Window(window);
        
        WGPUChainedStruct chainedStruct = {
            .next = NULL,
            .sType = WGPUSType_SurfaceDescriptorFromXlibWindow
        };
        
        WGPUSurfaceDescriptorFromXlibWindow tmp = {
            .chain = chainedStruct,
            .display = x11_display,
            .window = x11_window
        };
        
        WGPUSurfaceDescriptor surfaceDesc = {
            .nextInChain = (const WGPUChainedStruct*)&tmp,
            .label = NULL,
        };
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    }
#elif WGPU_TARGET == WGPU_TARGET_LINUX_WAYLAND
    {
        struct wl_display* wayland_display = glfwGetWaylandDisplay();
        struct wl_surface* wayland_surface = glfwGetWaylandWindow(window);
        
        WGPUChainedStruct chainedStruct = {
            .next = NULL,
            .sType = WGPUSType_SurfaceDescriptorFromWaylandSurface
        };
        
        WGPUSurfaceDescriptorFromWaylandSurface tmp = {
            .chain = chainedStruct,
            .display = wayland_display,
            .surface = wayland_surface
        };
        
        WGPUSurfaceDescriptor surfaceDesc = {
            .nextInChain = (const WGPUChainedStruct*)&tmp,
            .label = NULL,
        };
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    }
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
    {
        HWND hwnd = glfwGetWin32Window(window);
        HINSTANCE hinstance = GetModuleHandle(NULL);
        WGPUChainedStruct chainedStruct = {
            .next = NULL,
            .sType = WGPUSType_SurfaceDescriptorFromWindowsHWND
        };
        
        WGPUSurfaceDescriptorFromWindowsHWND tmp = {
            .chain = chainedStruct,
            .hinstance = hinstance,
            .hwnd = hwnd
        };
        
        WGPUSurfaceDescriptor surfaceDesc = {
            .nextInChain = (const WGPUChainedStruct*)&tmp,
            .label = NULL,
        };
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
        
    }
#elif WGPU_TARGET == WGPU_TARGET_EMSCRIPTEN
    {
        WGPUChainedStruct chainedStruct = {
            .next = NULL,
            .sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector
        };
        
        WGPUSurfaceDescriptorFromCanvasHTMLSelector tmp = {
            .chain = chainedStruct,
            .selector = "canvas"
        };
        
        WGPUSurfaceDescriptor surfaceDesc = {
            .nextInChain = (const WGPUChainedStruct*)&tmp;
        };
        
        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    }
#else
#error "Unsupported WGPU_TARGET"
#endif
}

