#include <cstdlib>
#include <iostream>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

namespace {

const char *openGLString(GLenum name)
{
    const GLubyte *value = glGetString(name);
    return value == nullptr ? "" : reinterpret_cast<const char *>(value);
}

} // namespace

int main()
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
        std::cerr << "Unable to initialize EGL display.\n";
        return EXIT_FAILURE;
    }

    const EGLint configurationAttributes[] = {
        EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        0x0040, // EGL_OPENGL_ES3_BIT_KHR
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_NONE
    };
    EGLConfig configuration = nullptr;
    EGLint configurationCount = 0;
    if (eglChooseConfig(
            display,
            configurationAttributes,
            &configuration,
            1,
            &configurationCount
        ) != EGL_TRUE ||
        configurationCount < 1) {
        std::cerr << "No EGL OpenGL ES 3 configuration is available.\n";
        eglTerminate(display);
        return EXIT_FAILURE;
    }

    const EGLint surfaceAttributes[] = {
        EGL_WIDTH,
        32,
        EGL_HEIGHT,
        32,
        EGL_NONE
    };
    EGLSurface surface =
        eglCreatePbufferSurface(display, configuration, surfaceAttributes);
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION,
        3,
        EGL_NONE
    };
    EGLContext context =
        eglCreateContext(display, configuration, EGL_NO_CONTEXT, contextAttributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
        std::cerr << "Unable to create the EGL pbuffer context.\n";
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
        return EXIT_FAILURE;
    }

    std::cout << "vendor=" << openGLString(GL_VENDOR) << '\n'
              << "renderer=" << openGLString(GL_RENDERER) << '\n'
              << "version=" << openGLString(GL_VERSION) << '\n';
    glViewport(0, 0, 32, 32);
    glClearColor(0.08F, 0.24F, 0.42F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    const bool succeeded =
        openGLString(GL_VENDOR)[0] != '\0' &&
        openGLString(GL_RENDERER)[0] != '\0' &&
        openGLString(GL_VERSION)[0] != '\0' &&
        eglSwapBuffers(display, surface) == EGL_TRUE;

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
