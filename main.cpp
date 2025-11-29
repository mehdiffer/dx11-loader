#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <memory>
#include <array>
#include <cstring>
#include <cmath>
#include <string>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

template<typename T>
struct ComDeleter {
    void operator()(T* ptr) const {
        if (ptr) ptr->Release();
    }
};

template<typename T>
using ComPtr = std::unique_ptr<T, ComDeleter<T>>;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Color palette
namespace Colors {
    constexpr ImVec4 Background{ 0.0f, 0.0f, 0.0f, 1.0f };
    constexpr ImVec4 Primary{ 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr ImVec4 Secondary{ 0.55f, 0.55f, 0.55f, 1.0f };
    constexpr ImVec4 Red{ 0.90f, 0.20f, 0.20f, 1.0f };

    constexpr ImVec4 ButtonBg{ 14.0f / 255.0f, 14.0f / 255.0f, 14.0f / 255.0f, 1.0f };
    constexpr ImVec4 ButtonBorder{ 21.0f / 255.0f, 21.0f / 255.0f, 21.0f / 255.0f, 1.0f };
    constexpr ImVec4 ButtonText{ 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr ImVec4 ButtonHover{ 16.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f, 1.0f };

    constexpr ImVec4 InputBg{ 11.0f / 255.0f, 11.0f / 255.0f, 11.0f / 255.0f, 1.0f };
    constexpr ImVec4 InputBorder{ 18.0f / 255.0f, 18.0f / 255.0f, 18.0f / 255.0f, 1.0f };
    constexpr ImVec4 InputBorderActive{ 25.0f / 255.0f, 25.0f / 255.0f, 25.0f / 255.0f, 1.0f };

    constexpr ImVec4 LinkText{ 0.60f, 0.60f, 0.60f, 1.0f };
    constexpr ImVec4 LinkHover{ 0.80f, 0.80f, 0.80f, 1.0f };
}

class D3DRenderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTargetView;

public:
    bool Initialize(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 400;
        sd.BufferDesc.Height = 400;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr std::array<D3D_FEATURE_LEVEL, 2> featureLevels = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
        };

        D3D_FEATURE_LEVEL featureLevel;
        ID3D11Device* rawDevice = nullptr;
        ID3D11DeviceContext* rawContext = nullptr;
        IDXGISwapChain* rawSwapChain = nullptr;

        const HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels.data(), static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION, &sd, &rawSwapChain, &rawDevice, &featureLevel, &rawContext
        );

        if (result != S_OK) return false;

        device.reset(rawDevice);
        deviceContext.reset(rawContext);
        swapChain.reset(rawSwapChain);

        CreateRenderTarget();
        return true;
    }

    void CreateRenderTarget() {
        ID3D11Texture2D* backBuffer = nullptr;
        if (swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)) == S_OK && backBuffer) {
            ID3D11RenderTargetView* rawRTV = nullptr;
            device->CreateRenderTargetView(backBuffer, nullptr, &rawRTV);
            renderTargetView.reset(rawRTV);
            backBuffer->Release();
        }
    }

    void CleanupRenderTarget() {
        renderTargetView.reset();
    }

    void ResizeBuffers(UINT width, UINT height) {
        CleanupRenderTarget();
        swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        CreateRenderTarget();
    }

    void Render(const ImVec4& clearColor) {
        ID3D11RenderTargetView* rtv = renderTargetView.get();
        deviceContext->OMSetRenderTargets(1, &rtv, nullptr);
        deviceContext->ClearRenderTargetView(rtv, &clearColor.x);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(1, 0);
    }

    ID3D11Device* GetDevice() const { return device.get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return deviceContext.get(); }

    bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** outSRV, int* outWidth, int* outHeight) {
        int width, height, channels;
        unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
        if (!data) return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D* texture = nullptr;
        D3D11_SUBRESOURCE_DATA subResource{};
        subResource.pSysMem = data;
        subResource.SysMemPitch = width * 4;
        subResource.SysMemSlicePitch = 0;

        HRESULT hr = device->CreateTexture2D(&desc, &subResource, &texture);
        stbi_image_free(data);

        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;

        hr = device->CreateShaderResourceView(texture, &srvDesc, outSRV);
        texture->Release();

        if (FAILED(hr)) return false;

        if (outWidth) *outWidth = width;
        if (outHeight) *outHeight = height;
        return true;
    }
};

// Main application
class ImGuiApp {
    HWND hwnd{};
    D3DRenderer renderer;
    std::array<char, 256> username{};
    std::array<char, 256> password{};
    bool isDragging{ false };
    POINT dragOffset{};
    ImFont* boldFont{ nullptr };
    ImFont* mediumFont{ nullptr };
    ImFont* iconFont{ nullptr };
    ImFont* exitIconFont{ nullptr };
    ImFont* interMedium{ nullptr };
    float forgotPasswordAlpha{ 0.0f };
    float buttonHoverAlpha{ 0.0f };
    bool isLoading{ false };
    bool showMenu{ false };
    float loadingRotation{ 0.0f };
    float currentWindowWidth{ 400.0f };
    float targetWindowWidth{ 400.0f };
    float loadingContentAlpha{ 0.0f };
    float loadingStartTime{ 0.0f };
    float menuFadeAlpha{ 0.0f };
    int selectedMenuItem{ 0 };
    ID3D11ShaderResourceView* bgTexture{ nullptr };
    int bgWidth{ 0 };
    int bgHeight{ 0 };
    float productsHoverAlpha{ 0.0f };
    float updatesHoverAlpha{ 0.0f };
    float viewButton1HoverAlpha{ 0.0f };
    float viewButton2HoverAlpha{ 0.0f };
    int selectedProductIndex{ -1 };
    float launchButtonHoverAlpha{ 0.0f };
    float backButtonHoverAlpha{ 0.0f };
    bool showLaunchNotification{ false };
    float launchNotificationAlpha{ 0.0f };
    float launchNotificationY{ 0.0f };
    float launchNotificationStartTime{ 0.0f };
    int injectionStage{ 0 };
    int previousInjectionStage{ -1 };
    float stageStartTime{ 0.0f };

    void ApplyStyle() {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 0.0f;
        style.FrameRounding = 5.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 4.0f;

        style.FramePadding = ImVec2(14.0f, 10.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
        style.WindowPadding = ImVec2(0.0f, 0.0f);

        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 1.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_FrameBg] = Colors::InputBg;
        colors[ImGuiCol_FrameBgHovered] = Colors::InputBg;
        colors[ImGuiCol_FrameBgActive] = Colors::InputBg;
        colors[ImGuiCol_Border] = Colors::InputBorder;
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_Text] = Colors::Primary;
        colors[ImGuiCol_TextSelectedBg] = Colors::InputBg;
        colors[ImGuiCol_TextDisabled] = Colors::Secondary;
        colors[ImGuiCol_Button] = Colors::ButtonBg;
        colors[ImGuiCol_ButtonHovered] = Colors::ButtonHover;
        colors[ImGuiCol_ButtonActive] = Colors::ButtonBg;
    }

public:
    bool Initialize(HINSTANCE instance) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"ModernLoginApp";

        ::RegisterClassExW(&wc);

        RECT desktop;
        GetWindowRect(GetDesktopWindow(), &desktop);
        const int posX = (desktop.right - 400) / 2;
        const int posY = (desktop.bottom - 400) / 2;

        hwnd = ::CreateWindowExW(
            0,
            wc.lpszClassName,
            L"MEHDIFFER",
            WS_POPUP,
            posX, posY, 400, 400,
            nullptr, nullptr, wc.hInstance, this
        );

        if (!hwnd || !renderer.Initialize(hwnd)) {
            Cleanup();
            return false;
        }

        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

        COLORREF borderColor = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hwnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));

        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
        mediumFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 21.0f);
        boldFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 38.0f);

        std::string iconPath = exeDir + "\\icons.ttf";
        ImFontConfig config;
        config.MergeMode = false;
        config.GlyphMinAdvanceX = 13.0f;
        iconFont = io.Fonts->AddFontFromFileTTF(iconPath.c_str(), 13.0f, &config, NULL);
        if (iconFont) {
            OutputDebugStringA("Drip Icons (menu) loaded\n");
        }
        else {
            OutputDebugStringA("Failed to load Drip Icons (menu)\n");
        }

        ImFontConfig exitConfig;
        exitConfig.MergeMode = false;
        exitConfig.GlyphMinAdvanceX = 18.0f;
        exitIconFont = io.Fonts->AddFontFromFileTTF(iconPath.c_str(), 18.0f, &exitConfig, NULL);
        if (exitIconFont) {
            OutputDebugStringA("Drip Icons (exit) loaded\n");
        }
        else {
            OutputDebugStringA("Failed to load Drip Icons (exit)\n");
        }

        std::string interPath = exeDir + "\\Inter-Medium.ttf";
        interMedium = io.Fonts->AddFontFromFileTTF(interPath.c_str(), 15.0f);
        if (interMedium) {
            OutputDebugStringA("Inter Medium loaded\n");
        }
        else {
            OutputDebugStringA("Failed to load Inter Medium\n");
        }

        ApplyStyle();

        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(renderer.GetDevice(), renderer.GetDeviceContext());

        std::string bgPath = exeDir + "\\background.png";

        bool loaded = renderer.LoadTextureFromFile(bgPath.c_str(), &bgTexture, &bgWidth, &bgHeight);
        if (loaded) {
            OutputDebugStringA("Background texture loaded\n");
        }
        else {
            OutputDebugStringA("Failed to load texture\n");
        }

        return true;
    }

    void Run() {
        bool running = true;

        while (running) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) running = false;
            }

            if (!running) break;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            RenderUI();

            ImGui::Render();
            renderer.Render(Colors::Background);
        }
    }

    void RenderUI() {
        // Window animation
        if (isLoading && targetWindowWidth == 400.0f) {
            targetWindowWidth = 600.0f;
        }

        if (showMenu && targetWindowWidth != 600.0f) {
            targetWindowWidth = 600.0f;
        }

        if (!isLoading && !showMenu && targetWindowWidth == 600.0f) {
            targetWindowWidth = 400.0f;
        }

        if (fabs(currentWindowWidth - targetWindowWidth) > 0.5f) {
            currentWindowWidth += (targetWindowWidth - currentWindowWidth) * 0.08f;

            RECT rect;
            GetWindowRect(hwnd, &rect);
            const int centerX = (rect.left + rect.right) / 2;
            const int newWidth = (int)currentWindowWidth;
            SetWindowPos(hwnd, nullptr, centerX - newWidth / 2, rect.top, newWidth, 400, SWP_NOZORDER);

            renderer.ResizeBuffers(newWidth, 400);
        }
        else {
            currentWindowWidth = targetWindowWidth;
        }

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar;

        // Menu page
        if (showMenu) {
            RenderMenu(flags);
        }
        // Login page
        else if (!isLoading) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f));

            ImGui::Begin("Login", nullptr, flags);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 windowPos = ImGui::GetWindowPos();
            const ImVec2 actualWindowSize = ImGui::GetWindowSize();

            if (bgTexture) {
                const float windowAspect = actualWindowSize.x / actualWindowSize.y;
                const float imageAspect = (float)bgWidth / (float)bgHeight;

                float uvMinX = 0.0f, uvMinY = 0.0f, uvMaxX = 1.0f, uvMaxY = 1.0f;

                if (windowAspect > imageAspect) {
                    const float scale = actualWindowSize.x / bgWidth;
                    const float scaledHeight = bgHeight * scale;
                    const float heightRatio = actualWindowSize.y / scaledHeight;
                    uvMinY = (1.0f - heightRatio) * 0.5f;
                    uvMaxY = 1.0f - uvMinY;
                }
                else {
                    const float scale = actualWindowSize.y / bgHeight;
                    const float scaledWidth = bgWidth * scale;
                    const float widthRatio = actualWindowSize.x / scaledWidth;
                    uvMinX = (1.0f - widthRatio) * 0.5f;
                    uvMaxX = 1.0f - uvMinX;
                }

                drawList->AddImage(
                    (void*)bgTexture,
                    windowPos,
                    ImVec2(windowPos.x + actualWindowSize.x, windowPos.y + actualWindowSize.y),
                    ImVec2(uvMinX, uvMinY), ImVec2(uvMaxX, uvMaxY),
                    IM_COL32(255, 255, 255, 255)
                );
            }

            const ImVec2 titleBarMin(windowPos.x, windowPos.y);
            const ImVec2 titleBarMax(windowPos.x + actualWindowSize.x, windowPos.y + 35);

            if (ImGui::IsMouseHoveringRect(titleBarMin, titleBarMax)) {
                if (ImGui::IsMouseClicked(0)) {
                    isDragging = true;
                    POINT cursorPos;
                    GetCursorPos(&cursorPos);
                    RECT windowRect;
                    GetWindowRect(hwnd, &windowRect);
                    dragOffset.x = cursorPos.x - windowRect.left;
                    dragOffset.y = cursorPos.y - windowRect.top;
                }
            }

            if (isDragging) {
                if (ImGui::IsMouseDown(0)) {
                    POINT cursorPos;
                    GetCursorPos(&cursorPos);
                    SetWindowPos(hwnd, nullptr,
                        cursorPos.x - dragOffset.x,
                        cursorPos.y - dragOffset.y,
                        0, 0, SWP_NOSIZE | SWP_NOZORDER);
                }
                else {
                    isDragging = false;
                }
            }

            constexpr float logoY = 60.0f;
            ImGui::SetCursorPosY(logoY);

            if (boldFont) {
                ImGui::PushFont(boldFont);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Red);
            const float redWidth = ImGui::CalcTextSize("MEH").x;
            const float engineWidth = ImGui::CalcTextSize("DIFFER").x;
            const float totalWidth = redWidth + engineWidth;
            const float centerX = (actualWindowSize.x - totalWidth) * 0.5f;

            ImGui::SetCursorPosX(centerX);
            ImGui::Text("MEH");
            ImGui::SameLine(0, 0);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            ImGui::Text("DIFFER");
            ImGui::PopStyleColor();

            if (boldFont) {
                ImGui::PopFont();
            }

            constexpr float authY = logoY + 31.0f;
            ImGui::SetCursorPosY(authY);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            const float authWidth = ImGui::CalcTextSize("Authentication Portal").x;
            ImGui::SetCursorPosX((actualWindowSize.x - authWidth) * 0.5f);
            ImGui::Text("Authentication Portal");
            ImGui::PopStyleColor();

            const float contentWidth = 320.0f;
            const float contentX = (actualWindowSize.x - contentWidth) * 0.5f;
            constexpr float contentStartY = 150.0f;

            constexpr float usernameLabelY = contentStartY;
            ImGui::SetCursorPos(ImVec2(contentX, usernameLabelY));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("Username");
            ImGui::PopStyleColor();

            constexpr float usernameInputY = usernameLabelY + 23.0f;
            ImGui::SetCursorPos(ImVec2(contentX, usernameInputY));
            ImGui::PushItemWidth(contentWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            bool enterPressed = ImGui::InputTextWithHint("##username", "user123", username.data(), 33, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor();

            if (ImGui::IsItemActive()) {
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(Colors::InputBorderActive), 5.0f, 0, 1.0f);
            }

            constexpr float passwordLabelY = usernameInputY + 50.0f;
            ImGui::SetCursorPos(ImVec2(contentX, passwordLabelY));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("Password");
            ImGui::PopStyleColor();

            const float forgotWidth = ImGui::CalcTextSize("Forgot Password?").x;
            ImGui::SetCursorPos(ImVec2(contentX + contentWidth - forgotWidth, passwordLabelY));

            const bool forgotHovered = ImGui::IsMouseHoveringRect(
                ImVec2(windowPos.x + contentX + contentWidth - forgotWidth, windowPos.y + passwordLabelY),
                ImVec2(windowPos.x + contentX + contentWidth, windowPos.y + passwordLabelY + ImGui::GetTextLineHeight())
            );

            forgotPasswordAlpha += ((forgotHovered ? 1.0f : 0.0f) - forgotPasswordAlpha) * 0.10f;
            const ImVec4 forgotColor = ImVec4(
                Colors::LinkText.x + (Colors::LinkHover.x - Colors::LinkText.x) * forgotPasswordAlpha,
                Colors::LinkText.y + (Colors::LinkHover.y - Colors::LinkText.y) * forgotPasswordAlpha,
                Colors::LinkText.z + (Colors::LinkHover.z - Colors::LinkText.z) * forgotPasswordAlpha,
                1.0f
            );

            ImGui::PushStyleColor(ImGuiCol_Text, forgotColor);
            ImGui::Text("Forgot Password?");
            if (forgotHovered && ImGui::IsMouseClicked(0)) {
            }
            ImGui::PopStyleColor();

            constexpr float passwordInputY = passwordLabelY + 23.0f;
            ImGui::SetCursorPos(ImVec2(contentX, passwordInputY));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            enterPressed |= ImGui::InputTextWithHint("##password", "**********", password.data(), 63, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor();

            if (ImGui::IsItemActive()) {
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(Colors::InputBorderActive), 5.0f, 0, 1.0f);
            }

            constexpr float buttonY = passwordInputY + 58.0f;
            ImGui::SetCursorPos(ImVec2(contentX, buttonY));

            const bool buttonHovered = ImGui::IsMouseHoveringRect(
                ImVec2(windowPos.x + contentX, windowPos.y + buttonY),
                ImVec2(windowPos.x + contentX + contentWidth, windowPos.y + buttonY + 42)
            );

            buttonHoverAlpha += ((buttonHovered ? 1.0f : 0.0f) - buttonHoverAlpha) * 0.10f;
            const ImVec4 buttonColor = ImVec4(
                Colors::ButtonBg.x + (Colors::ButtonHover.x - Colors::ButtonBg.x) * buttonHoverAlpha,
                Colors::ButtonBg.y + (Colors::ButtonHover.y - Colors::ButtonBg.y) * buttonHoverAlpha,
                Colors::ButtonBg.z + (Colors::ButtonHover.z - Colors::ButtonBg.z) * buttonHoverAlpha,
                1.0f
            );

            ImGui::PushStyleColor(ImGuiCol_Text, Colors::ButtonText);
            ImGui::PushStyleColor(ImGuiCol_Border, Colors::ButtonBorder);
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            if (ImGui::Button("Login", ImVec2(contentWidth, 42)) || enterPressed) {
                if (strcmp(username.data(), "admin") == 0 && strcmp(password.data(), "123") == 0) {
                    isLoading = true;
                    targetWindowWidth = 600.0f;
                    loadingContentAlpha = 0.0f;
                    loadingStartTime = ImGui::GetTime();
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(5);

            ImGui::PopItemWidth();

            ImGui::End();
        }
        // Loading screen
        else {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));

            ImGui::Begin("Loading", nullptr, flags);

            const ImVec2 windowPos = ImGui::GetWindowPos();
            const ImVec2 windowSize = ImGui::GetWindowSize();
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            if (bgTexture) {
                const float windowAspect = windowSize.x / windowSize.y;
                const float imageAspect = (float)bgWidth / (float)bgHeight;

                float uvMinX = 0.0f, uvMinY = 0.0f, uvMaxX = 1.0f, uvMaxY = 1.0f;

                if (windowAspect > imageAspect) {
                    const float scale = windowSize.x / bgWidth;
                    const float scaledHeight = bgHeight * scale;
                    const float heightRatio = windowSize.y / scaledHeight;
                    uvMinY = (1.0f - heightRatio) * 0.5f;
                    uvMaxY = 1.0f - uvMinY;
                }
                else {
                    const float scale = windowSize.y / bgHeight;
                    const float scaledWidth = bgWidth * scale;
                    const float widthRatio = windowSize.x / scaledWidth;
                    uvMinX = (1.0f - widthRatio) * 0.5f;
                    uvMaxX = 1.0f - uvMinX;
                }

                drawList->AddImage(
                    (void*)bgTexture,
                    windowPos,
                    ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
                    ImVec2(uvMinX, uvMinY), ImVec2(uvMaxX, uvMaxY),
                    IM_COL32(255, 255, 255, 255)
                );
            }

            const ImVec2 titleBarMin(windowPos.x, windowPos.y);
            const ImVec2 titleBarMax(windowPos.x + windowSize.x, windowPos.y + 35);

            if (ImGui::IsMouseHoveringRect(titleBarMin, titleBarMax)) {
                if (ImGui::IsMouseClicked(0)) {
                    isDragging = true;
                    POINT cursorPos;
                    GetCursorPos(&cursorPos);
                    RECT windowRect;
                    GetWindowRect(hwnd, &windowRect);
                    dragOffset.x = cursorPos.x - windowRect.left;
                    dragOffset.y = cursorPos.y - windowRect.top;
                }
            }

            if (isDragging) {
                if (ImGui::IsMouseDown(0)) {
                    POINT cursorPos;
                    GetCursorPos(&cursorPos);
                    SetWindowPos(hwnd, nullptr,
                        cursorPos.x - dragOffset.x,
                        cursorPos.y - dragOffset.y,
                        0, 0, SWP_NOSIZE | SWP_NOZORDER);
                }
                else {
                    isDragging = false;
                }
            }

            const ImVec2 center(
                windowPos.x + windowSize.x * 0.5f,
                windowPos.y + windowSize.y * 0.5f
            );

            loadingRotation += ImGui::GetIO().DeltaTime * 3.0f;

            const float elapsedTime = ImGui::GetTime() - loadingStartTime;
            const bool showContent = (elapsedTime >= 0.3f);

            if (elapsedTime >= 2.5f) {
                isLoading = false;
                showMenu = true;
                targetWindowWidth = 600.0f;
            }

            if (showContent) {
                loadingContentAlpha += (1.0f - loadingContentAlpha) * 0.15f;

                const float radius = 18.0f;
                const float thickness = 3.0f;
                const int segments = 36;
                const float arcLength = 4.71238898f;

                for (int i = 0; i < segments; i++) {
                    const float t = i / (float)segments;
                    const float a1 = loadingRotation + t * arcLength;
                    const float a2 = loadingRotation + ((i + 1) / (float)segments) * arcLength;

                    const ImVec2 p1(
                        center.x + cosf(a1) * radius,
                        center.y - 20.0f + sinf(a1) * radius
                    );
                    const ImVec2 p2(
                        center.x + cosf(a2) * radius,
                        center.y - 20.0f + sinf(a2) * radius
                    );

                    const ImU32 spinnerColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                        Colors::Secondary.x,
                        Colors::Secondary.y,
                        Colors::Secondary.z,
                        loadingContentAlpha
                    ));
                    drawList->AddLine(p1, p2, spinnerColor, thickness);
                }

                const char* loadingText = "Verifying credentials";
                const ImVec2 textSize = ImGui::CalcTextSize(loadingText);

                ImGui::SetCursorPosX((windowSize.x - textSize.x) * 0.5f);
                ImGui::SetCursorPosY(windowSize.y * 0.5f + 20.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
                    Colors::Secondary.x,
                    Colors::Secondary.y,
                    Colors::Secondary.z,
                    loadingContentAlpha
                ));
                ImGui::Text("%s", loadingText);
                ImGui::PopStyleColor();
            }

            ImGui::End();
        }
    }

    // Menu rendering
    void RenderMenu(ImGuiWindowFlags flags) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));

        ImGui::Begin("Menu", nullptr, flags);

        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (bgTexture) {
            const float windowAspect = windowSize.x / windowSize.y;
            const float imageAspect = (float)bgWidth / (float)bgHeight;

            float uvMinX = 0.0f, uvMinY = 0.0f, uvMaxX = 1.0f, uvMaxY = 1.0f;

            if (windowAspect > imageAspect) {
                const float scale = windowSize.x / bgWidth;
                const float scaledHeight = bgHeight * scale;
                const float heightRatio = windowSize.y / scaledHeight;
                uvMinY = (1.0f - heightRatio) * 0.5f;
                uvMaxY = 1.0f - uvMinY;
            }
            else {
                const float scale = windowSize.y / bgHeight;
                const float scaledWidth = bgWidth * scale;
                const float widthRatio = windowSize.x / scaledWidth;
                uvMinX = (1.0f - widthRatio) * 0.5f;
                uvMaxX = 1.0f - uvMinX;
            }

            drawList->AddImage(
                (void*)bgTexture,
                windowPos,
                ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
                ImVec2(uvMinX, uvMinY), ImVec2(uvMaxX, uvMaxY),
                IM_COL32(255, 255, 255, 255)
            );
        }

        const ImVec2 titleBarMin(windowPos.x, windowPos.y);
        const ImVec2 titleBarMax(windowPos.x + windowSize.x, windowPos.y + 35);

        if (ImGui::IsMouseHoveringRect(titleBarMin, titleBarMax)) {
            if (ImGui::IsMouseClicked(0)) {
                isDragging = true;
                POINT cursorPos;
                GetCursorPos(&cursorPos);
                RECT windowRect;
                GetWindowRect(hwnd, &windowRect);
                dragOffset.x = cursorPos.x - windowRect.left;
                dragOffset.y = cursorPos.y - windowRect.top;
            }
        }

        if (isDragging) {
            if (ImGui::IsMouseDown(0)) {
                POINT cursorPos;
                GetCursorPos(&cursorPos);
                SetWindowPos(hwnd, nullptr,
                    cursorPos.x - dragOffset.x,
                    cursorPos.y - dragOffset.y,
                    0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            else {
                isDragging = false;
            }
        }

        ImGui::SetCursorPos(ImVec2(14.5f, 12.0f));

        if (mediumFont) ImGui::PushFont(mediumFont);

        ImGui::PushStyleColor(ImGuiCol_Text, Colors::Red);
        ImGui::Text("MEH");
        ImGui::SameLine(0, 0);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
        ImGui::Text("DIFFER");
        ImGui::PopStyleColor();

        if (mediumFont) ImGui::PopFont();

        if (exitIconFont) ImGui::PushFont(exitIconFont);
        const ImVec2 logoutSize = ImGui::CalcTextSize("=");
        const float logoutX = windowSize.x - logoutSize.x - 14.5f;
        const float logoutY = 15.5f;

        const ImVec2 logoutMin(windowPos.x + logoutX - 5.0f, windowPos.y + logoutY - 5.0f);
        const ImVec2 logoutMax(windowPos.x + logoutX + logoutSize.x + 5.0f, windowPos.y + logoutY + logoutSize.y + 5.0f);
        bool logoutHovered = ImGui::IsMouseHoveringRect(logoutMin, logoutMax);

        ImGui::SetCursorPos(ImVec2(logoutX, logoutY));

        ImVec4 baseColor = ImVec4(216.0f / 255.0f, 20.0f / 255.0f, 59.0f / 255.0f, 1.0f);
        ImVec4 hoverColor = ImVec4(186.0f / 255.0f, 15.0f / 255.0f, 49.0f / 255.0f, 1.0f);

        static float hoverAlpha = 0.0f;
        hoverAlpha += ((logoutHovered ? 1.0f : 0.0f) - hoverAlpha) * 0.15f;

        ImVec4 logoutColor = ImVec4(
            baseColor.x + (hoverColor.x - baseColor.x) * hoverAlpha,
            baseColor.y + (hoverColor.y - baseColor.y) * hoverAlpha,
            baseColor.z + (hoverColor.z - baseColor.z) * hoverAlpha,
            1.0f
        );

        ImGui::PushStyleColor(ImGuiCol_Text, logoutColor);
        ImGui::Text("=");
        ImGui::PopStyleColor();

        if (logoutHovered && ImGui::IsMouseClicked(0)) {
            PostQuitMessage(0);
        }

        if (exitIconFont) ImGui::PopFont();

        const float separatorY = 47.0f;
        drawList->AddLine(
            ImVec2(windowPos.x, windowPos.y + separatorY),
            ImVec2(windowPos.x + windowSize.x, windowPos.y + separatorY),
            IM_COL32(14, 14, 14, 255), 1.0f
        );

        if (mediumFont) ImGui::PushFont(mediumFont);
        const float mehdifferWidth = ImGui::CalcTextSize("MEHDIFFER").x;
        if (mediumFont) ImGui::PopFont();
        const float verticalSeparatorX = 14.5f + mehdifferWidth + 13.5f;
        drawList->AddLine(
            ImVec2(windowPos.x + verticalSeparatorX, windowPos.y + separatorY),
            ImVec2(windowPos.x + verticalSeparatorX, windowPos.y + windowSize.y),
            IM_COL32(14, 14, 14, 255), 1.0f
        );

        const float menuStartY = 66.0f;
        const float menuItemHeight = 32.0f;
        const float menuItemX = 23.0f;
        const float iconTextSpacing = 8.0f;

        const ImVec2 productsMin(windowPos.x + menuItemX, windowPos.y + menuStartY - 5.0f);
        const ImVec2 productsMax(windowPos.x + verticalSeparatorX, windowPos.y + menuStartY + menuItemHeight - 5.0f);
        bool productsHovered = ImGui::IsMouseHoveringRect(productsMin, productsMax);

        if (productsHovered && ImGui::IsMouseClicked(0)) {
            selectedMenuItem = 0;
        }

        productsHoverAlpha += ((productsHovered ? 1.0f : 0.0f) - productsHoverAlpha) * 0.15f;

        ImVec4 productsBaseColor = selectedMenuItem == 0 ? Colors::Primary : Colors::Secondary;
        ImVec4 productsHoverColor = Colors::Primary;
        ImVec4 productsTextColor = ImVec4(
            productsBaseColor.x + (productsHoverColor.x - productsBaseColor.x) * productsHoverAlpha * 0.5f,
            productsBaseColor.y + (productsHoverColor.y - productsBaseColor.y) * productsHoverAlpha * 0.5f,
            productsBaseColor.z + (productsHoverColor.z - productsBaseColor.z) * productsHoverAlpha * 0.5f,
            1.0f
        );

        ImGui::PushStyleColor(ImGuiCol_Text, productsTextColor);

        ImGui::SetCursorPos(ImVec2(menuItemX, menuStartY - 3.0f));
        if (iconFont) ImGui::PushFont(iconFont);
        const float iconWidth = ImGui::CalcTextSize(reinterpret_cast<const char*>(u8"\ue05b")).x;
        ImGui::Text("%s", reinterpret_cast<const char*>(u8"\ue05b"));
        if (iconFont) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(menuItemX + iconWidth + iconTextSpacing, menuStartY - 5.0f));
        if (interMedium) ImGui::PushFont(interMedium);
        ImGui::Text("Products");
        if (interMedium) ImGui::PopFont();
        ImGui::PopStyleColor();

        const ImVec2 updatesMin(windowPos.x + menuItemX, windowPos.y + menuStartY + menuItemHeight - 8.0f);
        const ImVec2 updatesMax(windowPos.x + verticalSeparatorX, windowPos.y + menuStartY + 2 * menuItemHeight - 8.0f);
        bool updatesHovered = ImGui::IsMouseHoveringRect(updatesMin, updatesMax);

        if (updatesHovered && ImGui::IsMouseClicked(0)) {
            selectedMenuItem = 1;
        }

        updatesHoverAlpha += ((updatesHovered ? 1.0f : 0.0f) - updatesHoverAlpha) * 0.15f;

        ImVec4 updatesBaseColor = selectedMenuItem == 1 ? Colors::Primary : Colors::Secondary;
        ImVec4 updatesHoverColor = Colors::Primary;
        ImVec4 updatesTextColor = ImVec4(
            updatesBaseColor.x + (updatesHoverColor.x - updatesBaseColor.x) * updatesHoverAlpha * 0.5f,
            updatesBaseColor.y + (updatesHoverColor.y - updatesBaseColor.y) * updatesHoverAlpha * 0.5f,
            updatesBaseColor.z + (updatesHoverColor.z - updatesBaseColor.z) * updatesHoverAlpha * 0.5f,
            1.0f
        );

        ImGui::PushStyleColor(ImGuiCol_Text, updatesTextColor);

        ImGui::SetCursorPos(ImVec2(menuItemX, menuStartY + menuItemHeight - 6.0f));
        if (iconFont) ImGui::PushFont(iconFont);
        const float iconWidth2 = ImGui::CalcTextSize("x").x;
        ImGui::Text("x");
        if (iconFont) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(menuItemX + iconWidth2 + iconTextSpacing, menuStartY + menuItemHeight - 8.0f));
        if (interMedium) ImGui::PushFont(interMedium);
        ImGui::Text("Updates");
        if (interMedium) ImGui::PopFont();
        ImGui::PopStyleColor();

        // Products page
        if (selectedMenuItem == 0) {
            const float contentX = verticalSeparatorX + 20.0f;
            const float contentY = separatorY + 20.0f;
            const float contentWidth = windowSize.x - verticalSeparatorX - 40.0f;

            // Product detail view
            if (selectedProductIndex >= 0) {
                const char* productNames[] = { "TK - Toolkit", "DM - Device Modifier" };
                const char* productStatuses[] = { "Undetected", "USE AT OWN RISK" };
                ImVec4 statusColors[] = { ImVec4(0.0f, 0.8f, 0.0f, 1.0f), ImVec4(1.0f, 0.6f, 0.0f, 1.0f) };

                const float titleCardY = contentY - 4.0f;
                const float titleCardHeight = 70.0f;
                const float titleCardX = contentX - 1.0f;
                const ImVec2 titleCardMin(windowPos.x + titleCardX - 4.0f, windowPos.y + titleCardY);
                const ImVec2 titleCardMax(windowPos.x + titleCardX + contentWidth + 6.0f, windowPos.y + titleCardY + titleCardHeight);
                drawList->AddRect(titleCardMin, titleCardMax, IM_COL32(14, 14, 14, 255), 5.0f, 0, 1.0f);

                const char* productName = productNames[selectedProductIndex];
                if (mediumFont) ImGui::PushFont(mediumFont);
                const ImVec2 titleSize = ImGui::CalcTextSize(productName);
                if (mediumFont) ImGui::PopFont();

                const float titleX = contentX + (contentWidth - titleSize.x) * 0.5f;
                const float titleY = titleCardY + 15.0f;

                ImGui::SetCursorPos(ImVec2(titleX, titleY));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                if (mediumFont) ImGui::PushFont(mediumFont);
                ImGui::Text("%s", productName);
                if (mediumFont) ImGui::PopFont();
                ImGui::PopStyleColor();

                const ImVec2 versionSize = ImGui::CalcTextSize("v1.2.3");
                const float versionX = contentX + (contentWidth - versionSize.x) * 0.5f;
                const float versionY = titleY + titleSize.y + 5.0f;

                ImGui::SetCursorPos(ImVec2(versionX, versionY));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
                ImGui::Text("v1.2.3");
                ImGui::PopStyleColor();

                const float cardY = titleCardY + titleCardHeight + 8.0f;
                const float cardWidth = (contentWidth - 4.0f) * 0.5f;
                const float cardHeight = 60.0f;
                const float cardPadding = 15.0f;

                const float cardsOffsetX = contentX - 1.0f;
                const ImVec2 subCardMin(windowPos.x + cardsOffsetX - 4.0f, windowPos.y + cardY);
                const ImVec2 subCardMax(windowPos.x + cardsOffsetX + cardWidth, windowPos.y + cardY + cardHeight);
                drawList->AddRect(subCardMin, subCardMax, IM_COL32(14, 14, 14, 255), 5.0f, 0, 1.0f);

                const float labelHeight = 18.0f;
                const float dateHeight = 18.0f;
                const float textSpacing = 4.0f;
                const float totalTextHeight = labelHeight + textSpacing + dateHeight;
                const float verticalPadding = (cardHeight - totalTextHeight) * 0.5f;

                ImGui::SetCursorPos(ImVec2(cardsOffsetX + cardPadding, cardY + verticalPadding));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
                ImGui::Text("Subscription");
                ImGui::PopStyleColor();

                ImGui::SetCursorPos(ImVec2(cardsOffsetX + cardPadding, cardY + verticalPadding + labelHeight + textSpacing));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("2025-11-01");
                ImGui::PopStyleColor();

                const float expCardX = cardsOffsetX + cardWidth + 10.0f;
                const ImVec2 expCardMin(windowPos.x + expCardX - 4.0f, windowPos.y + cardY);
                const ImVec2 expCardMax(windowPos.x + expCardX + cardWidth, windowPos.y + cardY + cardHeight);
                drawList->AddRect(expCardMin, expCardMax, IM_COL32(14, 14, 14, 255), 5.0f, 0, 1.0f);

                ImGui::SetCursorPos(ImVec2(expCardX + cardPadding, cardY + verticalPadding));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
                ImGui::Text("Expiration");
                ImGui::PopStyleColor();

                ImGui::SetCursorPos(ImVec2(expCardX + cardPadding, cardY + verticalPadding + labelHeight + textSpacing));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("2026-11-01");
                ImGui::PopStyleColor();

                const float featuresCardY = cardY + cardHeight + 8.0f;
                const float featuresCardHeight = 110.0f;
                const float featuresCardPadding = 15.0f;
                const float featuresCardX = contentX - 1.0f;

                const ImVec2 featuresCardMin(windowPos.x + featuresCardX - 4.0f, windowPos.y + featuresCardY);
                const ImVec2 featuresCardMax(windowPos.x + featuresCardX + contentWidth + 6.0f, windowPos.y + featuresCardY + featuresCardHeight);
                drawList->AddRect(featuresCardMin, featuresCardMax, IM_COL32(14, 14, 14, 255), 5.0f, 0, 1.0f);

                ImGui::SetCursorPos(ImVec2(featuresCardX + featuresCardPadding, featuresCardY + featuresCardPadding));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("Features");
                ImGui::PopStyleColor();

                const float featureStartY = featuresCardY + featuresCardPadding + 25.0f;
                ImGui::SetCursorPos(ImVec2(featuresCardX + featuresCardPadding, featureStartY));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);

                if (selectedProductIndex == 0) {
                    ImGui::Text("• Advanced Module");
                    ImGui::SetCursorPos(ImVec2(featuresCardX + featuresCardPadding, featureStartY + 20.0f));
                    ImGui::Text("• Auto-update system");
                    ImGui::SetCursorPos(ImVec2(featuresCardX + featuresCardPadding, featureStartY + 40.0f));
                    ImGui::Text("• 24/7 support");
                } else {
                    ImGui::Text("• Hardware ID spoofing");
                    ImGui::SetCursorPos(ImVec2(featuresCardX + featuresCardPadding, featureStartY + 20.0f));
                    ImGui::Text("• Registry protection");
                    ImGui::SetCursorPos(ImVec2(featuresCardX + featuresCardPadding, featureStartY + 40.0f));
                    ImGui::Text("• HWID cleaner");
                }

                ImGui::PopStyleColor();

                const float bottomY = windowPos.y + windowSize.y - 50.0f;
                const float btnWidth = 100.0f;
                const float btnHeight = 35.0f;
                const float launchBtnX = windowSize.x - btnWidth - 15.0f;

                const float detailSeparatorY = bottomY - 16.0f;
                drawList->AddLine(
                    ImVec2(windowPos.x + verticalSeparatorX, detailSeparatorY),
                    ImVec2(windowPos.x + windowSize.x, detailSeparatorY),
                    IM_COL32(14, 14, 14, 255), 1.0f
                );

                const ImVec2 launchBtnMin(windowPos.x + launchBtnX, bottomY);
                const ImVec2 launchBtnMax(windowPos.x + launchBtnX + btnWidth, bottomY + btnHeight);

                bool launchHovered = ImGui::IsMouseHoveringRect(launchBtnMin, launchBtnMax);
                launchButtonHoverAlpha += ((launchHovered ? 1.0f : 0.0f) - launchButtonHoverAlpha) * 0.15f;
                
                const int baseColor = 14;
                const int hoverColor = 16;
                const int currentColor = baseColor + (int)((hoverColor - baseColor) * launchButtonHoverAlpha);
                drawList->AddRectFilled(
                    launchBtnMin,
                    launchBtnMax,
                    IM_COL32(currentColor, currentColor, currentColor, 255),
                    5.0f
                );

                drawList->AddRect(launchBtnMin, launchBtnMax, IM_COL32(20, 20, 20, 255), 5.0f, 0, 1.0f);

                const ImVec2 launchTextSize = ImGui::CalcTextSize("Inject");
                ImGui::SetCursorPos(ImVec2(launchBtnX + (btnWidth - launchTextSize.x) * 0.5f, bottomY - windowPos.y + (btnHeight - launchTextSize.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("Inject");
                ImGui::PopStyleColor();

                if (launchHovered && ImGui::IsMouseClicked(0)) {
                    showLaunchNotification = true;
                    launchNotificationAlpha = 0.0f;
                    launchNotificationY = 0.0f;
                    launchNotificationStartTime = ImGui::GetTime();
                    injectionStage = 0;
                    previousInjectionStage = -1;
                    stageStartTime = ImGui::GetTime();
                }

                const ImVec2 statusTextSize = ImGui::CalcTextSize(productStatuses[selectedProductIndex]);
                const float statusX = launchBtnX - statusTextSize.x - 20.0f;
                ImGui::SetCursorPos(ImVec2(statusX, bottomY - windowPos.y + (btnHeight - statusTextSize.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, statusColors[selectedProductIndex]);
                ImGui::Text("%s", productStatuses[selectedProductIndex]);
                ImGui::PopStyleColor();

                const char* goBackIcon = "l";
                const char* goBackText = " Go Back";

                if (iconFont) ImGui::PushFont(iconFont);
                const ImVec2 iconSize = ImGui::CalcTextSize(goBackIcon);
                if (iconFont) ImGui::PopFont();
                const ImVec2 textSize = ImGui::CalcTextSize(goBackText);
                const ImVec2 totalSize(iconSize.x + textSize.x, btnHeight);

                const ImVec2 goBackMin(windowPos.x + contentX - 5.0f, bottomY);
                const ImVec2 goBackMax(windowPos.x + contentX - 5.0f + totalSize.x, bottomY + btnHeight);
                bool backHovered = ImGui::IsMouseHoveringRect(goBackMin, goBackMax);

                backButtonHoverAlpha += ((backHovered ? 1.0f : 0.0f) - backButtonHoverAlpha) * 0.15f;
                ImVec4 backBaseColor = Colors::Secondary;
                ImVec4 backHoverColor = Colors::Primary;
                ImVec4 backTextColor = ImVec4(
                    backBaseColor.x + (backHoverColor.x - backBaseColor.x) * backButtonHoverAlpha,
                    backBaseColor.y + (backHoverColor.y - backBaseColor.y) * backButtonHoverAlpha,
                    backBaseColor.z + (backHoverColor.z - backBaseColor.z) * backButtonHoverAlpha,
                    1.0f
                );

                ImGui::SetCursorPos(ImVec2(contentX - 5.0f, bottomY - windowPos.y + (btnHeight - iconSize.y) * 0.5f));
                if (iconFont) ImGui::PushFont(iconFont);
                ImGui::PushStyleColor(ImGuiCol_Text, backTextColor);
                ImGui::Text("%s", goBackIcon);
                ImGui::PopStyleColor();
                if (iconFont) ImGui::PopFont();

                ImGui::SetCursorPos(ImVec2(contentX - 5.0f + iconSize.x, bottomY - windowPos.y + (btnHeight - textSize.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, backTextColor);
                ImGui::Text("%s", goBackText);
                ImGui::PopStyleColor();

                if (backHovered && ImGui::IsMouseClicked(0)) {
                    selectedProductIndex = -1;
                }
            }
            // Product list
            else {
                const float cardY = contentY;
                const float cardHeight = 100.0f;
                const float cardPadding = 15.0f;

                const ImVec2 card1Min(windowPos.x + contentX, windowPos.y + cardY);
                const ImVec2 card1Max(windowPos.x + contentX + contentWidth, windowPos.y + cardY + cardHeight);

                drawList->AddRect(card1Min, card1Max, IM_COL32(14, 14, 14, 255), 8.0f, 0, 1.0f);

                ImGui::SetCursorPos(ImVec2(contentX + cardPadding, cardY + cardPadding));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                if (interMedium) ImGui::PushFont(interMedium);
                ImGui::Text("TK – Toolkit");
                if (interMedium) ImGui::PopFont();
                ImGui::PopStyleColor();

                ImGui::SetCursorPos(ImVec2(contentX + cardPadding, cardY + cardPadding + 25.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
                ImGui::Text("Working / Undetected");
                ImGui::PopStyleColor();

                const float separatorY = cardY + cardHeight - 30.0f;
                drawList->AddLine(
                    ImVec2(card1Min.x, windowPos.y + separatorY),
                    ImVec2(card1Max.x, windowPos.y + separatorY),
                    IM_COL32(14, 14, 14, 255), 1.0f
                );

                const char* lastUpdatedLabel = "Last Updated:";
                const char* lastUpdatedDate1 = " 2025-11-25T09:46:13";
                const ImVec2 labelSize = ImGui::CalcTextSize(lastUpdatedLabel);
                const float verticalCenterY = cardY + cardHeight - 15.0f - (labelSize.y * 0.5f) - 1.0f;

                ImGui::SetCursorPos(ImVec2(contentX + cardPadding, verticalCenterY));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("%s", lastUpdatedLabel);
                ImGui::PopStyleColor();

                ImGui::SameLine(0, 0);
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
                ImGui::Text("%s", lastUpdatedDate1);
                ImGui::PopStyleColor();

                const float viewBtnWidth = 60.0f;
                const float viewBtnHeight = 30.0f;
                const ImVec2 viewBtnMin(card1Max.x - viewBtnWidth - cardPadding, card1Min.y + cardPadding);
                const ImVec2 viewBtnMax(card1Max.x - cardPadding, card1Min.y + cardPadding + viewBtnHeight);

                bool viewHovered = ImGui::IsMouseHoveringRect(viewBtnMin, viewBtnMax);

                viewButton1HoverAlpha += ((viewHovered ? 1.0f : 0.0f) - viewButton1HoverAlpha) * 0.15f;
                const int viewBaseColor = 14;
                const int viewHoverColor = 16;
                const int viewCurrentColor = viewBaseColor + (int)((viewHoverColor - viewBaseColor) * viewButton1HoverAlpha);
                ImU32 viewBtnBg = IM_COL32(viewCurrentColor, viewCurrentColor, viewCurrentColor, 255);

                drawList->AddRectFilled(viewBtnMin, viewBtnMax, viewBtnBg, 5.0f);
                drawList->AddRect(viewBtnMin, viewBtnMax, IM_COL32(20, 20, 20, 255), 5.0f, 0, 1.0f);

                const ImVec2 viewTextSize = ImGui::CalcTextSize("View");
                ImGui::SetCursorPos(ImVec2(contentX + contentWidth - cardPadding - viewBtnWidth + (viewBtnWidth - viewTextSize.x) * 0.5f,
                    cardY + cardPadding + (viewBtnHeight - viewTextSize.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("View");
                ImGui::PopStyleColor();

                if (viewHovered && ImGui::IsMouseClicked(0)) {
                    selectedProductIndex = 0;
                }

                const float card2Y = cardY + cardHeight + 15.0f;
                const ImVec2 card2Min(windowPos.x + contentX, windowPos.y + card2Y);
                const ImVec2 card2Max(windowPos.x + contentX + contentWidth, windowPos.y + card2Y + cardHeight);

                drawList->AddRect(card2Min, card2Max, IM_COL32(14, 14, 14, 255), 8.0f, 0, 1.0f);

                ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                if (interMedium) ImGui::PushFont(interMedium);
                ImGui::Text("DM – Device Modifier");
                if (interMedium) ImGui::PopFont();
                ImGui::PopStyleColor();

                ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding + 25.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
                ImGui::Text("USE AT OWN RISK");
                ImGui::PopStyleColor();

                const float separator2Y = card2Y + cardHeight - 30.0f;
                drawList->AddLine(
                    ImVec2(card2Min.x, windowPos.y + separator2Y),
                    ImVec2(card2Max.x, windowPos.y + separator2Y),
                    IM_COL32(14, 14, 14, 255), 1.0f
                );

                const char* lastUpdatedDate2 = " 2025-11-28T15:32:47";
                const float verticalCenterY2 = card2Y + cardHeight - 15.0f - (labelSize.y * 0.5f) - 1.0f;

                ImGui::SetCursorPos(ImVec2(contentX + cardPadding, verticalCenterY2));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("%s", lastUpdatedLabel);
                ImGui::PopStyleColor();

                ImGui::SameLine(0, 0);
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
                ImGui::Text("%s", lastUpdatedDate2);
                ImGui::PopStyleColor();

                const ImVec2 view2BtnMin(card2Max.x - viewBtnWidth - cardPadding, card2Min.y + cardPadding);
                const ImVec2 view2BtnMax(card2Max.x - cardPadding, card2Min.y + cardPadding + viewBtnHeight);

                bool view2Hovered = ImGui::IsMouseHoveringRect(view2BtnMin, view2BtnMax);

                viewButton2HoverAlpha += ((view2Hovered ? 1.0f : 0.0f) - viewButton2HoverAlpha) * 0.15f;
                const int view2BaseColor = 14;
                const int view2HoverColor = 16;
                const int view2CurrentColor = view2BaseColor + (int)((view2HoverColor - view2BaseColor) * viewButton2HoverAlpha);
                ImU32 view2BtnBg = IM_COL32(view2CurrentColor, view2CurrentColor, view2CurrentColor, 255);

                drawList->AddRectFilled(view2BtnMin, view2BtnMax, view2BtnBg, 5.0f);
                drawList->AddRect(view2BtnMin, view2BtnMax, IM_COL32(20, 20, 20, 255), 5.0f, 0, 1.0f);

                const ImVec2 view2TextSize = ImGui::CalcTextSize("View");
                ImGui::SetCursorPos(ImVec2(contentX + contentWidth - cardPadding - viewBtnWidth + (viewBtnWidth - view2TextSize.x) * 0.5f,
                    card2Y + cardPadding + (viewBtnHeight - view2TextSize.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
                ImGui::Text("View");
                ImGui::PopStyleColor();

                if (view2Hovered && ImGui::IsMouseClicked(0)) {
                    selectedProductIndex = 1;
                }
            }
        }
        // Updates page
        else if (selectedMenuItem == 1) {
            const float contentX = verticalSeparatorX + 20.0f;
            const float contentY = separatorY + 20.0f;
            const float contentWidth = windowSize.x - verticalSeparatorX - 40.0f;

            const float cardHeight = 135.0f;
            const float cardPadding = 15.0f;

            const float card1Y = contentY;
            const ImVec2 updateCard1Min(windowPos.x + contentX, windowPos.y + card1Y);
            const ImVec2 updateCard1Max(windowPos.x + contentX + contentWidth, windowPos.y + card1Y + cardHeight);

            drawList->AddRect(updateCard1Min, updateCard1Max, IM_COL32(14, 14, 14, 255), 8.0f, 0, 1.0f);

            const char* latestBadge = "LATEST";
            const ImVec2 badgeSize = ImGui::CalcTextSize(latestBadge);
            const float badgePadX = 10.0f;
            const float badgePadY = 5.0f;
            const ImVec2 badgeMin(updateCard1Max.x - badgeSize.x - badgePadX * 2 - cardPadding, updateCard1Min.y + cardPadding);
            const ImVec2 badgeMax(updateCard1Max.x - cardPadding, updateCard1Min.y + cardPadding + badgeSize.y + badgePadY * 2);

            drawList->AddRect(badgeMin, badgeMax, IM_COL32(14, 14, 14, 255), 4.0f, 0, 1.0f);

            ImGui::SetCursorPos(ImVec2(contentX + contentWidth - badgeSize.x - badgePadX - cardPadding, card1Y + cardPadding + badgePadY));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("%s", latestBadge);
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card1Y + cardPadding));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            if (interMedium) ImGui::PushFont(interMedium);
            ImGui::Text("v2.3.1");
            if (interMedium) ImGui::PopFont();
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding + 60.0f, card1Y + cardPadding));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("November 28, 2025");
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card1Y + cardPadding + 30.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            ImGui::Text("Performance Improvements & Bug Fixes");
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card1Y + cardPadding + 52.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("- Optimized rendering engine for 30%% faster performance");
            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card1Y + cardPadding + 70.0f));
            ImGui::Text("- Fixed memory leak in authentication module");
            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card1Y + cardPadding + 88.0f));
            ImGui::Text("- Enhanced security protocols");
            ImGui::PopStyleColor();

            const float card2Y = card1Y + cardHeight + 15.0f;
            const ImVec2 updateCard2Min(windowPos.x + contentX, windowPos.y + card2Y);
            const ImVec2 updateCard2Max(windowPos.x + contentX + contentWidth, windowPos.y + card2Y + cardHeight);

            drawList->AddRect(updateCard2Min, updateCard2Max, IM_COL32(14, 14, 14, 255), 8.0f, 0, 1.0f);

            const char* stableBadge = "STABLE";
            const ImVec2 stableBadgeSize = ImGui::CalcTextSize(stableBadge);
            const ImVec2 stableBadgeMin(updateCard2Max.x - stableBadgeSize.x - badgePadX * 2 - cardPadding, updateCard2Min.y + cardPadding);
            const ImVec2 stableBadgeMax(updateCard2Max.x - cardPadding, updateCard2Min.y + cardPadding + stableBadgeSize.y + badgePadY * 2);

            drawList->AddRect(stableBadgeMin, stableBadgeMax, IM_COL32(14, 14, 14, 255), 4.0f, 0, 1.0f);

            ImGui::SetCursorPos(ImVec2(contentX + contentWidth - stableBadgeSize.x - badgePadX - cardPadding, card2Y + cardPadding + badgePadY));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("%s", stableBadge);
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            if (interMedium) ImGui::PushFont(interMedium);
            ImGui::Text("v2.2.0");
            if (interMedium) ImGui::PopFont();
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding + 60.0f, card2Y + cardPadding));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("November 15, 2025");
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding + 30.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);
            ImGui::Text("New Feature Release");
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding + 52.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Secondary);
            ImGui::Text("- Added dark mode support across all components");
            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding + 70.0f));
            ImGui::Text("- Implemented auto-update functionality");
            ImGui::SetCursorPos(ImVec2(contentX + cardPadding, card2Y + cardPadding + 88.0f));
            ImGui::Text("- New dashboard analytics");
            ImGui::PopStyleColor();
        }

        // Injection notification
        if (showLaunchNotification) {
            const float elapsedTime = ImGui::GetTime() - launchNotificationStartTime;

            if (elapsedTime < 1.5f) {
                injectionStage = 0;
            } else if (elapsedTime < 3.0f) {
                injectionStage = 1;
            } else if (elapsedTime < 4.5f) {
                injectionStage = 2;
            } else if (elapsedTime < 6.0f) {
                injectionStage = 3;
            } else {
                showLaunchNotification = false;
            }

            if (injectionStage != previousInjectionStage) {
                previousInjectionStage = injectionStage;
                launchNotificationAlpha = 0.0f;
                launchNotificationY = 0.0f;
                stageStartTime = ImGui::GetTime();
            }

            if (showLaunchNotification) {
                const float stageElapsed = ImGui::GetTime() - stageStartTime;

                if (stageElapsed < 0.2f) {
                    launchNotificationAlpha = stageElapsed / 0.2f;
                } else if (stageElapsed < 1.3f) {
                    launchNotificationAlpha = 1.0f;
                } else if (stageElapsed < 1.5f) {
                    launchNotificationAlpha = (1.5f - stageElapsed) / 0.2f;
                }

                const float targetY = 20.0f;
                if (launchNotificationY < targetY) {
                    launchNotificationY += (targetY - launchNotificationY) * 0.15f;
                }

                const char* notifText;
                const char* notifIcon;
                switch (injectionStage) {
                    case 0:
                        notifText = "Connecting..";
                        notifIcon = reinterpret_cast<const char*>(u8"\ue064");
                        break;
                    case 1:
                        notifText = "Resolving imports..";
                        notifIcon = reinterpret_cast<const char*>(u8"\ue007");
                        break;
                    case 2:
                        notifText = "Resolved imports [100%]";
                        notifIcon = "S";
                        break;
                    case 3:
                        notifText = "Injected successfully!";
                        notifIcon = "S";
                        break;
                    default:
                        notifText = "Injecting..";
                        notifIcon = reinterpret_cast<const char*>(u8"\ue009");
                        break;
                }

                const ImVec2 iconSize = ImGui::CalcTextSize(notifIcon);
                const ImVec2 textSize = ImGui::CalcTextSize(notifText);
                const float leftPadding = 15.0f;
                const float iconTextSpacing = 8.0f;
                const float rightPadding = 15.0f;
                const float notifWidth = leftPadding + iconSize.x + iconTextSpacing + textSize.x + rightPadding;
                const float notifHeight = 50.0f;
                const float notifX = 5.0f;
                const float notifY = windowSize.y - notifHeight + 15.0f - launchNotificationY;

                const ImVec2 notifMin(windowPos.x + notifX, windowPos.y + notifY);
                const ImVec2 notifMax(windowPos.x + notifX + notifWidth, windowPos.y + notifY + notifHeight);

                if (bgTexture) {
                    const float uvMinX = notifX / windowSize.x;
                    const float uvMinY = notifY / windowSize.y;
                    const float uvMaxX = (notifX + notifWidth) / windowSize.x;
                    const float uvMaxY = (notifY + notifHeight) / windowSize.y;

                    drawList->AddImage(
                        (void*)bgTexture,
                        notifMin,
                        notifMax,
                        ImVec2(uvMinX, uvMinY),
                        ImVec2(uvMaxX, uvMaxY),
                        IM_COL32(255, 255, 255, (int)(255 * launchNotificationAlpha))
                    );
                }

                const ImU32 notifBorder = IM_COL32(14, 14, 14, (int)(255 * launchNotificationAlpha));
                drawList->AddRect(notifMin, notifMax, notifBorder, 8.0f, 0, 1.0f);

                if (iconFont) ImGui::PushFont(iconFont);
                const float iconX = notifX + leftPadding;
                const float iconY = notifY + (notifHeight - iconSize.y) * 0.5f;

                ImGui::SetCursorPos(ImVec2(iconX, iconY));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
                    Colors::Primary.x,
                    Colors::Primary.y,
                    Colors::Primary.z,
                    launchNotificationAlpha
                ));
                ImGui::Text("%s", notifIcon);
                ImGui::PopStyleColor();
                if (iconFont) ImGui::PopFont();

                const float extraSpacing = (injectionStage == 2 || injectionStage == 3) ? 2.0f : 0.0f;
                const float textX = iconX + iconSize.x + iconTextSpacing + extraSpacing;
                const float textY = notifY + (notifHeight - textSize.y) * 0.5f - 2.0f;

                ImGui::SetCursorPos(ImVec2(textX, textY));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(
                    Colors::Primary.x,
                    Colors::Primary.y,
                    Colors::Primary.z,
                    launchNotificationAlpha
                ));
                ImGui::Text("%s", notifText);
                ImGui::PopStyleColor();
            }
        }

        ImGui::End();
    }

    void Cleanup() {
        if (bgTexture) {
            bgTexture->Release();
            bgTexture = nullptr;
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (hwnd) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            if (::GetClassInfoExW(::GetModuleHandle(nullptr), L"ModernLoginApp", &wc)) {
                ::DestroyWindow(hwnd);
                ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            }
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        ImGuiApp* app = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = static_cast<ImGuiApp*>(cs->lpCreateParams);
            ::SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        else {
            app = reinterpret_cast<ImGuiApp*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
        }

        switch (msg) {
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

int main(int, char**) {
    ImGuiApp app;
    if (!app.Initialize(::GetModuleHandle(nullptr)))
        return 1;

    app.Run();
    app.Cleanup();
    return 0;
}