/* Copyright (2021-2023) Bytedance Ltd. and/or its affiliates, All rights reserved. */
#include <dirent.h>
#include "pch.h"
#include "common.h"
#include "options.h"
#include "application.h"
#include "controller.h"
#include "hand.h"
#include <common/xr_linear.h>
#include "logger.h"
#include "gui.h"
#include "ray.h"
#include "text.h"
#include "player.h"
#include "utils.h"
#include "graphicsplugin.h"
#include "cube.h"
#include "eva_bridge.h"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

class Application : public IApplication {
public:
    Application(const std::shared_ptr<struct Options>& options, const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin);
    virtual ~Application() override;
    virtual bool initialize(const XrInstance instance, const XrSession session, Extentions* extentions) override;
    virtual void setHapticCallback(void* arg, hapticCallback hapticCb) override;
    virtual void setControllerPose(int leftright, const XrPosef& pose) override;
    virtual void setControllerPower(int leftright, int power) override;
    virtual void setGazeLocation(XrSpaceLocation& gazeLocation, std::vector<XrView>& views, float ipd, XrResult result = XR_SUCCESS) override;
    virtual void setHandJointLocation(XrHandJointLocationEXT* location) override;
    virtual void inputEvent(int leftright, const ApplicationEvent& event) override;
    virtual void renderFrame(const XrPosef& pose, const glm::mat4& project, const glm::mat4& view, int32_t eye) override;
private:
    void layout();
    void showDashboard(const glm::mat4& project, const glm::mat4& view);
    void showDashboardController();
    void showPoseStatus();
    void showDeviceInformation(const glm::mat4& project, const glm::mat4& view);
    void renderEyeTracking(const glm::mat4& project, const glm::mat4& view, int32_t eye);
    void renderHandTracking(const glm::mat4& project, const glm::mat4& view);
    void getAllVideoFiles(const std::string& path, std::vector<std::string>& files);
    void startPlayVideo(const std::string& file);
    // Calculate the angle between the vector v and the plane normal vector n
    float angleBetweenVectorAndPlane(const glm::vec3& vector, const glm::vec3& normal);


    hapticCallback mHapticCallback;
    void* mHapticCallbackArg;

private:
    std::shared_ptr<IGraphicsPlugin> mGraphicsPlugin;
    std::shared_ptr<Controller> mController;
    std::shared_ptr<Ray> mEyeTrackingRay;
    std::shared_ptr<Gui> mPanel;
    std::shared_ptr<Text> mTextRender;
    std::shared_ptr<Player> mPlayer;
    glm::mat4 mControllerModel;
    XrPosef mControllerPose[HAND_COUNT];
    XrPosef mHeadPose{};
    ApplicationEvent mControllerState[HAND_COUNT]{};
    std::shared_ptr<CubeRender> mCubeRender;

    //openxr
    XrInstance m_instance;          //Keep the same naming as openxr_program.cpp
    XrSession m_session;
    Extentions* m_extentions;
    XrSpaceLocation m_gazeLocation;
    std::vector<XrView> m_views;
    float mIpd;
    XrHandJointLocationEXT m_jointLocations[HAND_COUNT][XR_HAND_JOINT_COUNT_EXT];

    //app data
    std::string mDeviceModel;
    std::string mDeviceOS;

    bool mIsShowDashboard = true;

    std::vector<std::string> mAllVideoFiles;
    int32_t mCount = 0;

    const ApplicationEvent *mControllerEvent[HAND_COUNT] = {nullptr, nullptr};

};

std::shared_ptr<IApplication> createApplication(const std::shared_ptr<struct Options>& options, const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin) {
    return std::make_shared<Application>(options, graphicsPlugin);
}

Application::Application(const std::shared_ptr<struct Options>& options, const std::shared_ptr<IGraphicsPlugin>& graphicsPlugin) {
    mGraphicsPlugin = graphicsPlugin;
    mController = std::make_shared<Controller>();
    mEyeTrackingRay = std::make_shared<Ray>();
    mPanel = std::make_shared<Gui>("dashboard");
    mTextRender = std::make_shared<Text>();
    mPlayer = std::make_shared<Player>();
    mHapticCallback = nullptr;
    memset(mControllerPose, 0, sizeof(mControllerPose));
    mCubeRender = std::make_shared<CubeRender>();
}

Application::~Application() {
}

void Application::getAllVideoFiles(const std::string& path, std::vector<std::string>& allFiles) {
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        errorf("opendir %s error %d", path.c_str(), errno);
        return;
    }
    struct dirent *file;
    while ((file = readdir(dir)) != nullptr) {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            continue;
        }
        if (file->d_type == DT_DIR) {
            std::string path_next = path + "/" + file->d_name;
            getAllVideoFiles(path_next, allFiles);
        } else {
            std::string fileFullName = path + "/" + file->d_name;
            std::string extension = fileFullName.substr(fileFullName.find_last_of('.') + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](char& c) {
                return std::tolower(c);
            });
            if (extension == "mp4" || extension == "mkv" || extension == "avi") {
                allFiles.push_back(fileFullName);
            }
            //infof("count:%d file:%s", mCount++, fileFullName.c_str());
        }
    }
}

bool Application::initialize(const XrInstance instance, const XrSession session, Extentions* extentions) {
    m_instance = instance;
    m_session = session;
    m_extentions = extentions;

    // get device model
    char buffer[64] = {0};
    __system_property_get("sys.pxr.product.name", buffer);
    mDeviceModel = buffer;

    //get OS version
    __system_property_get("ro.build.id", buffer);
    //__system_property_get("ro.system.build.id", buffer); // You can also call this function, the result is the same
    mDeviceOS = buffer;

    mController->initialize(mDeviceModel);
    mEyeTrackingRay->initialize();
    mPanel->initialize(600, 800);  //set resolution
    mTextRender->initialize();
    mCubeRender->initialize();

    const XrGraphicsBindingOpenGLESAndroidKHR *binding = reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(mGraphicsPlugin->GetGraphicsBinding());
    mPlayer->initialize(binding->display);

    getAllVideoFiles("/sdcard", mAllVideoFiles);

    //copyFile("/sdcard/Pictures/Screenshots/20230426-105301.jpg", "/sdcard/Pictures/2.jpg");
    //refreshMedia("/sdcard/Pictures/");

    return true;
}

void Application::setHapticCallback(void* arg, hapticCallback hapticCb) {
    mHapticCallbackArg = arg;
    mHapticCallback = hapticCb;
}

void Application::setControllerPower(int leftright, int power) {
    mController->setPowerValue(leftright, power);
}

void Application::setControllerPose(int leftright, const XrPosef& pose) {
    XrMatrix4x4f model{};
    XrVector3f scale{1.0f, 1.0f, 1.0f};
    XrMatrix4x4f_CreateTranslationRotationScale(&model, &pose.position, &pose.orientation, &scale);
    glm::mat4 m = glm::make_mat4((float*)&model);
    mController->setModel(leftright, m);
    mControllerPose[leftright] = pose;
}

void Application::setGazeLocation(XrSpaceLocation& gazeLocation, std::vector<XrView>& views, float ipd, XrResult result) {
    mIpd = ipd;
    memcpy(&m_gazeLocation, &gazeLocation, sizeof(gazeLocation));
    m_views = views;
}

void Application::setHandJointLocation(XrHandJointLocationEXT* location) {
    memcpy(&m_jointLocations, location, sizeof(m_jointLocations));
}

void Application::startPlayVideo(const std::string& file) {
    //mPlayer->stop();
    mPlayer->start(file);
}

void Application::inputEvent(int leftright, const ApplicationEvent& event) {
    mControllerState[leftright] = event;
    mControllerEvent[leftright] = &mControllerState[leftright];

    if (event.controllerEventBit & CONTROLLER_EVENT_BIT_click_menu) {
        if (event.click_menu == true) {
            mIsShowDashboard = !mIsShowDashboard;
        }
    }

    if (leftright == HAND_LEFT) {
        return;
    }
    if (event.controllerEventBit & CONTROLLER_EVENT_BIT_click_trigger) {
        //infof("controllerEventBit:0x%02x, event.click_trigger:0x%d", event.controllerEventBit, event.click_trigger);
        mPanel->triggerEvent(event.click_trigger);
    }

    // The right-hand callback is the second callback for each action sync.
    // Send one complete frame after both controller states are available.
    if (leftright == HAND_RIGHT && mControllerEvent[HAND_LEFT] != nullptr) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        static uint64_t sequence = 0;
        auto controller = [](const XrPosef& pose, const ApplicationEvent& state, int hand) {
            const bool primary = hand == HAND_LEFT ? state.click_x : state.click_a;
            const bool secondary = hand == HAND_LEFT ? state.click_y : state.click_b;
            const bool valid = std::abs(pose.orientation.x) + std::abs(pose.orientation.y) +
                std::abs(pose.orientation.z) + std::abs(pose.orientation.w) > 1e-5f;
            char value[1800];
            const int n = std::snprintf(value, sizeof(value),
                "{\"valid\":%s,\"position\":[%.7g,%.7g,%.7g],\"orientation_xyzw\":[%.7g,%.7g,%.7g,%.7g],\"profiles\":[\"pico-4-ultra\"],\"mapping\":\"pico-4-ultra\",\"buttons\":["
                "{\"pressed\":%s,\"touched\":%s,\"value\":%.7g},"
                "{\"pressed\":%s,\"touched\":%s,\"value\":%.7g},"
                "{\"pressed\":false,\"touched\":false,\"value\":0},"
                "{\"pressed\":%s,\"touched\":%s,\"value\":0},"
                "{\"pressed\":%s,\"touched\":%s,\"value\":%s},"
                "{\"pressed\":%s,\"touched\":%s,\"value\":%s}],\"axes\":[%.7g,%.7g]}",
                valid ? "true" : "false",
                pose.position.x, pose.position.y, pose.position.z,
                pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                state.trigger >= 0.5f ? "true" : "false", state.touch_trigger ? "true" : "false", state.trigger,
                state.squeeze >= 0.5f ? "true" : "false", false ? "true" : "false", state.squeeze,
                state.click_thumbstck ? "true" : "false", state.touch_thumbstick ? "true" : "false",
                primary ? "true" : "false", hand == HAND_LEFT ? (state.touch_x ? "true" : "false") : (state.touch_a ? "true" : "false"), primary ? "1" : "0",
                secondary ? "true" : "false", hand == HAND_LEFT ? (state.touch_y ? "true" : "false") : (state.touch_b ? "true" : "false"), secondary ? "1" : "0",
                state.thumbstick_x, state.thumbstick_y);
            return std::string(value, n > 0 ? static_cast<size_t>(n) : 0);
        };
        std::string frame = "{\"type\":\"frame\",\"version\":1,\"seq\":" +
            std::to_string(sequence++) + ",\"client_time_ms\":" + std::to_string(now) +
            ",\"reference_space\":\"local-floor\",\"controllers\":{\"left\":" +
            controller(mControllerPose[HAND_LEFT], *mControllerEvent[HAND_LEFT], HAND_LEFT) +
            ",\"right\":" + controller(mControllerPose[HAND_RIGHT], *mControllerEvent[HAND_RIGHT], HAND_RIGHT) + "}}";
        eva_bridge_send_frame(frame);
    }

}

void Application::layout() {
    // Keep the status panel centered in front of the user's current head pose.
    // The panel is intentionally close enough to read, but far enough to avoid
    // intersecting the hands during normal controller use.
    glm::mat4 model = glm::mat4(1.0f);
    float width, height;
    const float playerScale = 1.0f;
    mPanel->getWidthHeight(width, height);
    const glm::vec3 headPosition = glm::make_vec3((float*)&mHeadPose.position);
    const glm::quat headOrientation = glm::make_quat((float*)&mHeadPose.orientation);
    const glm::vec3 forward = headOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 up = headOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 panelPosition = headPosition + forward * 1.15f + up * -0.10f;
    model = glm::translate(model, panelPosition);
    model *= glm::mat4_cast(headOrientation);
    model = glm::scale(model, glm::vec3(0.62f * (width / height), 0.62f, 1.0f));
    mPanel->setModel(model);

    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(1.0f, -0.0f, -1.5f));
    model = glm::rotate(model, glm::radians(-20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(playerScale*2, playerScale, 1.0f));
    mPlayer->setModel(model);
}


namespace {
struct PoseAngles {
    float roll;
    float pitch;
    float yaw;
};

PoseAngles poseAngles(const XrQuaternionf& q) {
    constexpr float radiansToDegrees = 57.29577951308232f;
    const float sinPitch = std::max(-1.0f, std::min(1.0f, 2.0f * (q.w * q.y - q.z * q.x)));
    return {
        std::atan2(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * radiansToDegrees,
        std::asin(sinPitch) * radiansToDegrees,
        std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * radiansToDegrees,
    };
}

void showPoseRow(const char* label, const XrPosef& pose) {
    const PoseAngles angles = poseAngles(pose.orientation);
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::Text("%s", label);
    ImGui::TableNextColumn(); ImGui::Text("%.3f", pose.position.x);
    ImGui::TableNextColumn(); ImGui::Text("%.3f", pose.position.y);
    ImGui::TableNextColumn(); ImGui::Text("%.3f", pose.position.z);
    ImGui::TableNextColumn(); ImGui::Text("%.1f", angles.roll);
    ImGui::TableNextColumn(); ImGui::Text("%.1f", angles.pitch);
    ImGui::TableNextColumn(); ImGui::Text("%.1f", angles.yaw);
}
}

void Application::showPoseStatus() {
    const bool connected = eva_bridge_is_connected();
    ImGui::TextColored(connected ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                       "HOST: %s", connected ? "CONNECTED" : "DISCONNECTED");
    ImGui::SameLine();
    ImGui::Text("  EVA-VR");

    if (ImGui::BeginTable("pose status", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("source");
        ImGui::TableSetupColumn("X");
        ImGui::TableSetupColumn("Y");
        ImGui::TableSetupColumn("Z");
        ImGui::TableSetupColumn("Roll");
        ImGui::TableSetupColumn("Pitch");
        ImGui::TableSetupColumn("Yaw");
        ImGui::TableHeadersRow();
        showPoseRow("Head", mHeadPose);
        showPoseRow("Left", mControllerPose[HAND_LEFT]);
        showPoseRow("Right", mControllerPose[HAND_RIGHT]);
        ImGui::EndTable();
    }
    ImGui::Separator();
}

void Application::showDashboardController() {
    if (mControllerEvent[HAND_LEFT] == nullptr || mControllerEvent[HAND_RIGHT] == nullptr) {
        ImGui::Text("Waiting for controller input...");
        return;
    }
#define HAND_BIT_LEFT HAND_LEFT+1
#define HAND_BIT_RIGHT HAND_RIGHT+1
#define SHOW_CONTROLLER_ROW_float(x)    ImGui::TableNextRow();\
                                        ImGui::TableNextColumn();\
                                        ImGui::Text("%s", MEMBER_NAME(ApplicationEvent, x));\
                                        ImGui::TableNextColumn();\
                                        ImGui::Text("%f", mControllerEvent[HAND_LEFT]->x);\
                                        ImGui::TableNextColumn();\
                                        ImGui::Text("%f", mControllerEvent[HAND_RIGHT]->x);

#define SHOW_CONTROLLER_ROW_bool(hand, x)   ImGui::TableNextRow();\
                                            ImGui::TableNextColumn();\
                                            ImGui::Text("%s", MEMBER_NAME(ApplicationEvent, x));\
                                            ImGui::TableNextColumn();\
                                            if (hand & HAND_BIT_LEFT && mControllerEvent[HAND_LEFT]->x) {\
                                                ImGui::Text("true");\
                                            }\
                                            ImGui::TableNextColumn();\
                                            if (hand & HAND_BIT_RIGHT && mControllerEvent[HAND_RIGHT]->x) {\
                                                ImGui::Text("true");\
                                            }  

    // Controller event state is always visible for live diagnostics.
    ImGui::Text("CONTROLLER INPUT");
    {
        const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
        const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
        static ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("controller event", 3, flags, ImVec2(0.0f, TEXT_BASE_HEIGHT * 19), 0.0f)) {
            ImGui::TableSetupColumn("event name",        ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed,   0.0f);
            ImGui::TableSetupColumn("left controller",   ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed,   0.0f);
            ImGui::TableSetupColumn("right controller",  ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch, 0.0f);
            ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
            ImGui::TableHeadersRow();

            SHOW_CONTROLLER_ROW_float(trigger);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, click_trigger);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, touch_trigger);

            SHOW_CONTROLLER_ROW_float(thumbstick_x);
            SHOW_CONTROLLER_ROW_float(thumbstick_y);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, click_thumbstck);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, touch_thumbstick);

            SHOW_CONTROLLER_ROW_float(squeeze);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT + HAND_BIT_RIGHT, click_squeeze);

            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, click_a);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, click_b);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, click_x);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, click_y);
            
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, touch_a);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_RIGHT, touch_b);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, touch_x);
            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, touch_y);

            SHOW_CONTROLLER_ROW_bool(HAND_BIT_LEFT, click_menu);

            ImGui::EndTable();
        }

    }
}

void Application::showDashboard(const glm::mat4& project, const glm::mat4& view) {
    const XrPosef& controllerPose = mControllerPose[HAND_RIGHT];
    const glm::vec3 linePoint = glm::make_vec3((float*)&controllerPose.position);
    const glm::vec3 lineDirection = mController->getRayDirection(HAND_RIGHT);
    mPanel->isIntersectWithLine(linePoint, lineDirection);

    mPanel->begin();
    ImGui::Text("EVA-VR");
    ImGui::Text("Device: %s | OS: %s", mDeviceModel.c_str(), mDeviceOS.c_str());
    showPoseStatus();
    showDashboardController();
    mPanel->end();
    mPanel->render(project, view);
}

void Application::showDeviceInformation(const glm::mat4& project, const glm::mat4& view) {
    wchar_t text[1024] = {0};
    swprintf(text, 1024, L"model: %s, OS: %s", mDeviceModel.c_str(), mDeviceOS.c_str());

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.5f, -0.6f, -1.0f));
    model = glm::rotate(model, glm::radians(-30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(0.5, 0.5, 1.0f));
    mTextRender->render(project, view, model, text, wcslen(text), glm::vec3(1.0, 1.0, 1.0));
}

float Application::angleBetweenVectorAndPlane(const glm::vec3& vector, const glm::vec3& normal) {
    float dotProduct = glm::dot(vector, normal);
    float lengthVector = glm::length(vector);
    float lengthNormal = glm::length(normal);
    if (lengthNormal != 1.0f) {
        lengthNormal = 1.0f;  //normalnize
    }
    float cosAngle = dotProduct / (lengthVector * lengthNormal);
    float angleRadians = std::acos(cosAngle);
    //Convert radians to degrees
    //float angleInDegrees = glm::degrees(angleRadians);
    return PI/2 - angleRadians;
}

void Application::renderEyeTracking(const glm::mat4& project, const glm::mat4& view, int32_t eye) {
    if (m_extentions->isSupportEyeTracking && m_extentions->activeEyeTracking) {
        if (m_views.size() == 0) {
            return;
        }

        XrMatrix4x4f m{};
        XrVector3f scale{1.0f, 1.0f, 1.0f};
        XrMatrix4x4f_CreateTranslationRotationScale(&m, &m_gazeLocation.pose.position, &m_gazeLocation.pose.orientation, &scale);
        glm::mat4 model = glm::make_mat4((float*)&m);
        float halfIpd = mIpd / 2;
        if (eye == EYE_LEFT) {
            halfIpd = 0 - halfIpd;
        }
        model = glm::translate(model, glm::vec3(halfIpd, 0.0f, -0.2f));
        model = glm::scale(model, glm::vec3(1.0, 1.0, 1.0f));
        mEyeTrackingRay->setColor(1.0f, 0.0f, 0.0f);

        //Maps the direction of eye gaze to a point on the screen (x, y) in percentage
        
        glm::vec3 direction = mEyeTrackingRay->getDirectionVector(model);

        XrMatrix4x4f m2{};
        XrMatrix4x4f_CreateTranslationRotationScale(&m2, &m_views[eye].pose.position, &m_views[eye].pose.orientation, &scale);
        glm::mat4 model2 = glm::make_mat4((float*)&m2);

        glm::vec3 pointO = glm::vec3(model2 * glm::vec4(0.0, 0.0, -1.0, 1.0f));
        glm::vec3 pointX = glm::vec3(model2 * glm::vec4(1.0, 0.0, -1.0, 1.0f));
        glm::vec3 pointY = glm::vec3(model2 * glm::vec4(0.0, 1.0, -1.0, 1.0f));
        glm::vec3 normalYOZ = pointX - pointO;
        glm::vec3 normalXOZ = pointY - pointO;

        float angleAndYOZ = angleBetweenVectorAndPlane(direction, normalYOZ);
        float angleAndXOZ = angleBetweenVectorAndPlane(direction, normalXOZ);

        float x, y;
        if (angleAndYOZ < 0) {
            x = (1 - tanf(angleAndYOZ) / tanf(m_views[eye].fov.angleLeft)) * 0.5;
        } else {
            x = (1 + tanf(angleAndYOZ) / tanf(m_views[eye].fov.angleRight)) * 0.5;
        }
        if (angleAndXOZ) {
            y = (1 - tanf(angleAndXOZ) / tanf(m_views[eye].fov.angleUp)) * 0.5;
        } else {
            y = (1 + tanf(angleAndXOZ) / tanf(m_views[eye].fov.angleDown)) * 0.5;
        }

        //infof("angleAndYOZ:%f, angleAndXOZ:%f", angleAndYOZ, angleAndXOZ);

        mEyeTrackingRay->render(project, view, model);

        // show the coordinates
        wchar_t text[1024] = {0};
        swprintf(text, 1024, L"x:%0.2f, y:%0.2f", x, y);
        model = glm::translate(model, glm::vec3(-0.2f, 0.0f, -1.5f));
        model = glm::scale(model, glm::vec3(0.5, 0.5, 0.5f));
        mTextRender->render(project, view, model, text, wcslen(text), glm::vec3(1.0, 1.0, 1.0));
    }
}

void Application::renderHandTracking(const glm::mat4& project, const glm::mat4& view) {
    std::vector<CubeRender::Cube> cubes;
    for (auto hand = 0; hand < HAND_COUNT; hand++) {
        for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++) {
            XrHandJointLocationEXT& jointLocation = m_jointLocations[hand][i];
            if (jointLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT && jointLocation.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) {

                XrMatrix4x4f m{};
                XrVector3f scale{1.0f, 1.0f, 1.0f};
                XrMatrix4x4f_CreateTranslationRotationScale(&m, &jointLocation.pose.position, &jointLocation.pose.orientation, &scale);
                glm::mat4 model = glm::make_mat4((float*)&m);

                CubeRender::Cube cube;
                cube.model = model;
                cube.scale = 0.01f;
                cubes.push_back(cube);
            }
        }
    }
    mCubeRender->render(project, view, cubes);
}

void Application::renderFrame(const XrPosef& pose, const glm::mat4& project, const glm::mat4& view, int32_t eye) {
    if (eye == 0) mHeadPose = pose;
    layout();
    showDeviceInformation(project, view);

    mPlayer->render(project, view, eye);

    if (mIsShowDashboard) {
        showDashboard(project, view);
    }

    renderEyeTracking(project, view, eye);
    
    mController->render(project, view);

    renderHandTracking(project, view);

}
