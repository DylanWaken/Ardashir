#include "PixelSortWindow.h"
#include <algorithm>
#include <stdexcept>
#if defined(ARDA_PIXEL_SORT_VULKAN)
// Only surface creation needs Vulkan declarations here. Fetch entry points from
// the installed system loader; no link-time loader library is used. Vulkan headers
// are supplied at build time, with no SDK lookup when launching the application.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#endif

namespace arda
{
    FPixelSortWindow::~FPixelSortWindow()
    {
        // Main's owner ordering has already destroyed the swap chain/Vulkan surface.
        if (mWindow) DestroyWindow(mWindow);
        if (mVulkanLoader) FreeLibrary(mVulkanLoader);
    }
    void FPixelSortWindow::Create(uint32_t Width, uint32_t Height, bool Hidden)
    {
        // Work in client-area pixels for swap-chain extents and kernel selection.
        // Account for borders so the requested image size is not the outer window size.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW Class{};
        Class.lpfnWndProc = Procedure; Class.hInstance = GetModuleHandleW(nullptr);
        Class.lpszClassName = L"ArdaPixelSort"; Class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("RegisterClass failed.");
        RECT Bounds{0, 0, LONG(Width), LONG(Height)};
        AdjustWindowRect(&Bounds, WS_OVERLAPPEDWINDOW, FALSE);
        mWindow = CreateWindowExW(0, Class.lpszClassName, L"Pixel Sort", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, Bounds.right - Bounds.left, Bounds.bottom - Bounds.top,
            nullptr, nullptr, Class.hInstance, this);
        if (!mWindow) throw std::runtime_error("CreateWindow failed.");
        if (!Hidden) ShowWindow(mWindow, SW_SHOW);
        RECT Client{}; GetClientRect(mWindow, &Client);
        mWidth = Client.right; mHeight = Client.bottom; mbResize = false;
    }
    bool FPixelSortWindow::Pump()
    {
        // Drain events without blocking normal animation. Main uses WaitMessage
        // only when minimized; a hidden test window still pumps and renders frames.
        MSG Message;
        while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
        { if (Message.message == WM_QUIT) return false; TranslateMessage(&Message); DispatchMessageW(&Message); }
        return !mbClosed;
    }
    bool FPixelSortWindow::ConsumeResize() { const bool Changed = mbResize; mbResize = false; return Changed; }
    void FPixelSortWindow::Resize(uint32_t Width, uint32_t Height)
    {
        // Used by --resize-test to trigger the same WM_SIZE path as an interactive
        // resize. This changes the native window; main handles GPU resizing later.
        RECT Bounds{0, 0, LONG(Width), LONG(Height)}; AdjustWindowRect(&Bounds, WS_OVERLAPPEDWINDOW, FALSE);
        if (!SetWindowPos(mWindow, nullptr, 0, 0, Bounds.right - Bounds.left, Bounds.bottom - Bounds.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) throw std::runtime_error("Window resize failed.");
    }
    void FPixelSortWindow::SetTitle(const char* Title) { SetWindowTextA(mWindow, Title); }
    LRESULT CALLBACK FPixelSortWindow::Procedure(HWND Window, UINT Message, WPARAM W, LPARAM L)
    {
        auto* Self = reinterpret_cast<FPixelSortWindow*>(GetWindowLongPtrW(Window, GWLP_USERDATA));
        if (Message == WM_NCCREATE)
        {
            // Connect Win32's callback to the C++ instance passed to CreateWindowEx.
            Self = static_cast<FPixelSortWindow*>(reinterpret_cast<CREATESTRUCTW*>(L)->lpCreateParams);
            SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Self));
        }
        if (!Self) return DefWindowProcW(Window, Message, W, L);
        // Defer DestroyWindow until GPU/surface teardown has completed in main.
        if (Message == WM_CLOSE) { Self->mbClosed = true; return 0; }
        // Record size only. Do not wait for the GPU or recreate textures here;
        // callbacks may run during other native window operations.
        if (Message == WM_SIZE)
        { Self->mWidth = LOWORD(L); Self->mHeight = HIWORD(L); Self->mbResize = true; return 0; }
        if (Message == WM_KEYDOWN)
        {
            if (W == VK_ESCAPE) Self->mbClosed = true;
            if (W == VK_UP) Self->mThreshold = std::min(255u, Self->mThreshold + 8);
            if (W == VK_DOWN) Self->mThreshold = Self->mThreshold > 8 ? Self->mThreshold - 8 : 0;
            if (!(L & (1ll << 30)))
            {
                // Toggle controls once per press; arrows above deliberately repeat.
                if (W == 'C') Self->mChannel = (Self->mChannel + 1) % 3;
                if (W == VK_SPACE) Self->mbPaused = !Self->mbPaused;
                if (W == VK_TAB) Self->mbOriginal = !Self->mbOriginal;
            }
            return 0;
        }
        return DefWindowProcW(Window, Message, W, L);
    }
    FArdaNativeObject FPixelSortWindow::GetD3D12WindowHandle() const noexcept { return FArdaNativeObject(mWindow); }
    // These two extensions let the backend create an instance supporting a Win32
    // presentation surface; no other platform surface extension is needed here.
    eastl::vector<const char*> FPixelSortWindow::GetVulkanInstanceExtensions() const
    { return {"VK_KHR_surface", "VK_KHR_win32_surface"}; }
    FArdaNativeObject FPixelSortWindow::CreateVulkanSurface(FArdaNativeObject Instance, eastl::string& Error)
    {
#if defined(ARDA_PIXEL_SORT_VULKAN)
        // Resolve the surface entry point against the backend's own VkInstance.
        // The returned surface transfers to backend ownership. Keep the loader
        // module and HWND alive until backend shutdown has destroyed that surface.
        mVulkanLoader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto GetProc = mVulkanLoader ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(mVulkanLoader, "vkGetInstanceProcAddr")) : nullptr;
        const auto Create = GetProc ? reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(GetProc(Instance.As<VkInstance>(), "vkCreateWin32SurfaceKHR")) : nullptr;
        if (Create)
        {
            VkWin32SurfaceCreateInfoKHR Info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
            Info.hinstance = GetModuleHandleW(nullptr); Info.hwnd = mWindow;
            VkSurfaceKHR Surface = VK_NULL_HANDLE;
            if (Create(Instance.As<VkInstance>(), &Info, nullptr, &Surface) == VK_SUCCESS) return FArdaNativeObject(Surface);
        }
#endif
        Error = "Cannot create the Win32 Vulkan surface.";
        return {};
    }
}
