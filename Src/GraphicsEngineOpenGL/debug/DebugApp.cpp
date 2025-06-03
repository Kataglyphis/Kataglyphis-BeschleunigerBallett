#include "DebugApp.hpp"

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <iostream>
// stolen from: https://learnopengl.com/In-Practice/Debugging
void APIENTRY glDebugOutput(GLenum source,
  GLenum type,
  unsigned int id,
  GLenum severity,
  GLsizei length,
  const char *message,
  const void *userParam)
{
    // ignore non-significant error/warning codes
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

    std::cout << "---------------" << std::endl;
    std::cout << "Debug message (" << id << "): " << message << std::endl;

    switch (source) {
    case GL_DEBUG_SOURCE_API:
        std::cout << "Source: API";
        break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        std::cout << "Source: Window System";
        break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
        std::cout << "Source: Shader Compiler";
        break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:
        std::cout << "Source: Third Party";
        break;
    case GL_DEBUG_SOURCE_APPLICATION:
        std::cout << "Source: Application";
        break;
    case GL_DEBUG_SOURCE_OTHER:
        std::cout << "Source: Other";
        break;
    }
    std::cout << std::endl;

    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        std::cout << "Type: Error";
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        std::cout << "Type: Deprecated Behaviour";
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        std::cout << "Type: Undefined Behaviour";
        break;
    case GL_DEBUG_TYPE_PORTABILITY:
        std::cout << "Type: Portability";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE:
        std::cout << "Type: Performance";
        break;
    case GL_DEBUG_TYPE_MARKER:
        std::cout << "Type: Marker";
        break;
    case GL_DEBUG_TYPE_PUSH_GROUP:
        std::cout << "Type: Push Group";
        break;
    case GL_DEBUG_TYPE_POP_GROUP:
        std::cout << "Type: Pop Group";
        break;
    case GL_DEBUG_TYPE_OTHER:
        std::cout << "Type: Other";
        break;
    }
    std::cout << std::endl;

    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        std::cout << "Severity: high";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        std::cout << "Severity: medium";
        break;
    case GL_DEBUG_SEVERITY_LOW:
        std::cout << "Severity: low";
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        std::cout << "Severity: notification";
        break;
    }
    std::cout << std::endl;
    std::cout << std::endl;
}
DebugApp::DebugApp()
{
#ifdef NDEBUG
    // nondebug
#else
    int flags;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        // initialize debug output
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugOutput, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }
#endif
}

bool DebugApp::areErrorPrintAll(const std::string &AdditionalArrayMessage, const char *file, int line)
{
#ifdef NDEBUG
    // nondebug
    return false;
#else
    GLenum err;
    bool isError = false;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::string errorCode;
        switch (err) {
        case GL_INVALID_ENUM:
            errorCode = "INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            errorCode = "INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            errorCode = "INVALID_OPERATION";
            break;
        case GL_STACK_OVERFLOW:
            errorCode = "STACK_OVERFLOW";
            break;
        case GL_STACK_UNDERFLOW:
            errorCode = "STACK_UNDERFLOW";
            break;
        case GL_OUT_OF_MEMORY:
            errorCode = "OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            errorCode = "INVALID_FRAMEBUFFER_OPERATION";
            break;
        }
        std::cout << "Error, " << errorCode << " | "
                  << "\nAdditional Error Message: " << AdditionalArrayMessage << " "
                  << "In File: " << file << ", Line: " << line << std::endl;
        isError = true;
    }

    return isError;
    // debug code
#endif
}

bool DebugApp::arePreError(const std::string &AdditionalArrayMessage, const char *file, int line)
{
#ifdef NDEBUG
    // nondebug
    return false;
#else
    // debug code
    GLenum err;
    bool isError = false;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::string errorCode;
        switch (err) {
        case GL_INVALID_ENUM:
            errorCode = "INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            errorCode = "INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            errorCode = "INVALID_OPERATION";
            break;
        case GL_STACK_OVERFLOW:
            errorCode = "STACK_OVERFLOW";
            break;
        case GL_STACK_UNDERFLOW:
            errorCode = "STACK_UNDERFLOW";
            break;
        case GL_OUT_OF_MEMORY:
            errorCode = "OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            errorCode = "INVALID_FRAMEBUFFER_OPERATION";
            break;
        }
        std::cout << errorCode << " Error appears befor executing the function, "
                  << " | "
                  << "\nAdditional Error Message: " << AdditionalArrayMessage << " "
                  << "In File: " << file << ", Line: " << line << std::endl;
        isError = true;
    }

    return isError;

#endif
}

DebugApp::~DebugApp() {}
