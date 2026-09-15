/* Copyright (2021-2023) Bytedance Ltd. and/or its affiliates, All rights reserved. */
#include "gui.h"
#include "utils.h"
#include "glm/geometric.hpp"
#include "glm/gtc/matrix_transform.hpp"

Shader Gui::mShader;
Gui::Gui(std::string name): mName(name), mFramebuffer(0), mTextureColorbuffer(0), mVAO(0), mVBO(0) {
}

Gui::~Gui() {
}

bool Gui::initShader() {
    static bool init = false;
    if (init) {
        return true;
    }
    else {
        const GLchar* vertex_shader_glsl = R"_(
            #version 320 es
            precision highp float;
            layout (location = 0) in vec3 aPos;
            layout (location = 1) in vec2 aTexCoords;
            out vec2 TexCoords;
            out vec3 FragPos;
            uniform mat4 projection;
            uniform mat4 view;
            uniform mat4 model;
            void main()
            {
                FragPos = vec3(model * vec4(aPos, 1.0));
                TexCoords = aTexCoords;
                gl_Position = projection * view * vec4(FragPos, 1.0);
            }
        )_";

        const GLchar* fragment_shader_glsl = R"_(
            #version 320 es
            precision mediump float;
            out vec4 FragColor;
            in vec2 TexCoords;
            uniform sampler2D screenTexture;
            in vec3 FragPos;
            uniform vec3 intersectionPoint;
            void main()
            {
                float distance = (FragPos.x-intersectionPoint.x) * (FragPos.x-intersectionPoint.x) + (FragPos.y-intersectionPoint.y) * (FragPos.y-intersectionPoint.y) + (FragPos.z-intersectionPoint.z) * (FragPos.z-intersectionPoint.z);
                if (distance < 0.0001) {
                    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
                } else {
                    FragColor = texture(screenTexture, TexCoords);
                }
            }
        )_";

        if (mShader.loadShader(vertex_shader_glsl, fragment_shader_glsl) == false) {
            return false;
        }
        init = true;
    }
    return true;
}

bool Gui::initialize(int32_t width, int32_t height) {
    mWidth = width;
    mHeight = height;

    GuiBase::instance().initialize();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 18.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(22.0f, 18.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.045f, 0.060f, 0.94f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.070f, 0.090f, 0.72f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.30f, 0.40f, 0.35f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.94f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.56f, 0.65f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.08f, 0.30f, 0.42f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.10f, 0.42f, 0.56f, 0.90f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.07f, 0.15f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.30f, 0.38f, 0.75f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.20f, 0.27f, 0.55f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.04f, 0.06f, 0.08f, 0.45f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.07f, 0.10f, 0.13f, 0.45f);

    if (!initShader()) {
        return false;
    }
    if (mFramebuffer) {
        return true;
    }

    GL_CALL(glGenFramebuffers(1, &mFramebuffer));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer));

    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glGenTextures(1, &mTextureColorbuffer));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, mTextureColorbuffer));
    GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mWidth, mHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CALL(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CALL(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));

    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextureColorbuffer, 0));
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        errorf("Framebuffer is not complete!");
        return false;
    }

    const float vertices[] = {
        // positions         // texCoords
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,

        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f
    };

    GL_CALL(glGenVertexArrays(1, &mVAO));
    GL_CALL(glGenBuffers(1, &mVBO));
    GL_CALL(glBindVertexArray(mVAO));
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, mVBO));
    GL_CALL(glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW));
    GL_CALL(glEnableVertexAttribArray(0));
    GL_CALL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0));
    GL_CALL(glEnableVertexAttribArray(1));
    GL_CALL(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))));

    return true;
}

void Gui::render(const glm::mat4& p, const glm::mat4& v) {

    GLenum last_framebuffer = 0; GL_CALL(glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&last_framebuffer));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer));
    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextureColorbuffer, 0));
    GL_CALL(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
    GL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    active();
    GuiBase::instance().render();

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, last_framebuffer));

    mShader.use(); 
    mShader.setUniformMat4("projection", p);
    mShader.setUniformMat4("view", v);
    mShader.setUniformMat4("model", mModel);
    mShader.setUniformVec3("intersectionPoint", mIntersectionPoint);

    GL_CALL(glDisable(GL_CULL_FACE));
    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    GL_CALL(glBindVertexArray(mVAO));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, mTextureColorbuffer));
    GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));
}

void Gui::setModel(const glm::mat4& m) {
    mModel = m;
}

bool Gui::isIntersectWithLine(const glm::vec3& linePoint, const glm::vec3& lineDirection) {

    glm::vec3 planePoint = glm::vec3(mModel * glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f));
    glm::vec3 planePoint1 = glm::vec3(mModel * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    glm::vec3 planePoint2 = glm::vec3(mModel * glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f));

    glm::vec3 planeNormal = glm::cross(planePoint - planePoint1, planePoint - planePoint2);
    planeNormal = glm::normalize(planeNormal);

    glm::vec3 point{};
    float vpt = lineDirection.x * planeNormal.x + lineDirection.y * planeNormal.y + lineDirection.z * planeNormal.z;
    if (vpt == 0) {
        //The direction of the line is parallel to the direction of the plane, there is no intersection point
        return false;
    } else {
        float t = ((planePoint.x - linePoint.x) * planeNormal.x + (planePoint.y - linePoint.y) * planeNormal.y + (planePoint.z - linePoint.z) * planeNormal.z) / vpt;
        point.x = linePoint.x + lineDirection.x * t;
        point.y = linePoint.y + lineDirection.y * t;
        point.z = linePoint.z + lineDirection.z * t;
    }

    if (point.x >= planePoint2.x && point.x <= planePoint1.x 
     && point.y >= planePoint2.y && point.y <= planePoint1.y) {
        //infof("t:%f, linePoint(%.3f, %.3f, %.3f), lineDirection(%.3f, %.3f, %.3f)", t, linePoint.x, linePoint.y, linePoint.z, lineDirection.x, lineDirection.y, lineDirection.z);
        //infof("cross point(%.3f, %.3f, %.3f), planePoint2(%.3f, %.3f, %.3f), planeNormal(%.3f, %.3f, %.3f)", point.x, point.y, point.z, planePoint2.x, planePoint2.y, planePoint2.z, planeNormal.x, planeNormal.y, planeNormal.z);
        float width = fabs(planePoint1.x - planePoint2.x);
        float height = fabs(planePoint1.y - planePoint2.y);

        /* gui coordinate system
        (0,0)---------------(1,0) 
          |                   |
          |                   |
        (0,1)---------------(1,1)
        */
        float x = point.x - planePoint.x;
        float y = planePoint.y - point.y;
        float posx = x / width * mWidth;
        float posy = y / height * mHeight;

        updateMousePosition(posx, posy);
        mIntersectionPoint = point;
        mHasIntersection = true;

        return true;
    } else {
        mIntersectionPoint = {100.0, 0.0, 0.0};
        mHasIntersection = false;
        return false;
    }
}

void Gui::updateMousePosition(float x, float y) {
    active();
    auto& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(x, y);
}

void Gui::triggerEvent(bool down) {
    active();
    auto& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseButtonEvent(0/*left button*/, down);
}

void Gui::getWidthHeight(float& width, float& height) {
    width = float(mWidth);
    height = float(mHeight);
}

void Gui::active() {
    auto& io = ImGui::GetIO();
    io.DisplaySize.x = float(mWidth);
    io.DisplaySize.y = float(mHeight);
    io.DeltaTime = 1.0f / 72.0f;
}

void Gui::begin() {
    active();
    ImGui::NewFrame();
    updateDrag();
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin(mName.c_str());
    ImGui::SetWindowSize(ImVec2(mWidth, mHeight));
    ImGui::SetWindowPos(ImVec2(0, 0));
    ImGui::SetWindowFocus();
}

void Gui::updateDrag() {
    auto& io = ImGui::GetIO();
    if (mHasIntersection && io.MouseDown[0]) {
        if (mHasPreviousIntersection) {
            mModel = glm::translate(mModel, mIntersectionPoint - mPreviousIntersectionPoint);
        }
        mPreviousIntersectionPoint = mIntersectionPoint;
        mHasPreviousIntersection = true;
    } else if (!io.MouseDown[0]) {
        mHasPreviousIntersection = false;
    }
}

void Gui::end() {
    ImGui::End();
    ImGui::Render();
}
