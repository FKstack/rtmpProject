#include <cstdlib>
#include <iostream>

#include <Windows.h>
#include <GL/gl.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"RtmpMonitorOpenGLSmoke";

bool hasOpenGLString(GLenum name)
{
    const GLubyte *value = glGetString(name);
    return value != nullptr && value[0] != '\0';
}

const char *openGLString(GLenum name)
{
    return reinterpret_cast<const char *>(glGetString(name));
}

} // namespace

int main()
{
    WNDCLASSW windowClass {};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassW(&windowClass) == 0) {
        std::cerr << "RegisterClassW failed: " << GetLastError() << '\n';
        return EXIT_FAILURE;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        L"RtmpMonitor OpenGL smoke",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        32,
        32,
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr
    );
    if (window == nullptr) {
        std::cerr << "CreateWindowExW failed: " << GetLastError() << '\n';
        UnregisterClassW(kWindowClassName, windowClass.hInstance);
        return EXIT_FAILURE;
    }

    HDC deviceContext = GetDC(window);
    PIXELFORMATDESCRIPTOR pixelFormat {};
    pixelFormat.nSize = sizeof(pixelFormat);
    pixelFormat.nVersion = 1;
    pixelFormat.dwFlags =
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixelFormat.iPixelType = PFD_TYPE_RGBA;
    pixelFormat.cColorBits = 24;
    pixelFormat.cDepthBits = 24;
    pixelFormat.iLayerType = PFD_MAIN_PLANE;

    const int formatIndex = ChoosePixelFormat(deviceContext, &pixelFormat);
    if (formatIndex == 0 ||
        SetPixelFormat(deviceContext, formatIndex, &pixelFormat) == FALSE) {
        std::cerr << "OpenGL pixel format setup failed.\n";
        ReleaseDC(window, deviceContext);
        DestroyWindow(window);
        UnregisterClassW(kWindowClassName, windowClass.hInstance);
        return EXIT_FAILURE;
    }

    HGLRC renderContext = wglCreateContext(deviceContext);
    if (renderContext == nullptr ||
        wglMakeCurrent(deviceContext, renderContext) == FALSE) {
        std::cerr << "WGL context creation failed.\n";
        if (renderContext != nullptr) {
            wglDeleteContext(renderContext);
        }
        ReleaseDC(window, deviceContext);
        DestroyWindow(window);
        UnregisterClassW(kWindowClassName, windowClass.hInstance);
        return EXIT_FAILURE;
    }

    const bool hasRuntimeInformation =
        hasOpenGLString(GL_VENDOR) &&
        hasOpenGLString(GL_RENDERER) &&
        hasOpenGLString(GL_VERSION);
    if (hasRuntimeInformation) {
        std::cout << "vendor=" << openGLString(GL_VENDOR) << '\n'
                  << "renderer=" << openGLString(GL_RENDERER) << '\n'
                  << "version=" << openGLString(GL_VERSION) << '\n';
        glViewport(0, 0, 32, 32);
        glClearColor(0.08F, 0.24F, 0.42F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        if (SwapBuffers(deviceContext) == FALSE) {
            std::cerr << "SwapBuffers failed: " << GetLastError() << '\n';
        }
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(renderContext);
    ReleaseDC(window, deviceContext);
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, windowClass.hInstance);

    return hasRuntimeInformation ? EXIT_SUCCESS : EXIT_FAILURE;
}
