// FidelityFX Super Resolution Sample
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "stdafx.h"
#include <intrin.h>

#include "FSRSample.h"

static constexpr float MagnifierBorderColor_Locked[3] = { 0.002f, 0.72f, 0.0f };
static constexpr float MagnifierBorderColor_Free[3]   = { 0.72f, 0.002f, 0.0f };

FSRSample::FSRSample(LPCSTR name) : FrameworkWindows(name)
{
    m_time = 0;
    m_bPlay = true;

    m_pGltfLoader = NULL;
    mipBias[0] = -0.38f;
    mipBias[1] = -0.585f;
    mipBias[2] = -0.75f;
    mipBias[3] = -1.0f;
    mipBias[4] = 0.0f;
}

//--------------------------------------------------------------------------------------
//
// OnParseCommandLine
//
//--------------------------------------------------------------------------------------
void FSRSample::OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight)
{
    // set some default values
    *pWidth = 1920; 
    *pHeight = 1080; 
    m_activeScene = 0; //load the first one by default
    m_VsyncEnabled = false;
    m_state.bIsBenchmarking = false;
    m_fontSize = 13.f; // default value overridden by a json file if available
    m_isCpuValidationLayerEnabled = false;
    m_isGpuValidationLayerEnabled = false;
    m_activeCamera = 0;
    m_stablePowerState = false;

    //read globals
    auto process = [&](json jData)
    {
        *pWidth = jData.value("width", *pWidth);
        *pHeight = jData.value("height", *pHeight);
        m_fullscreenMode = jData.value("presentationMode", m_fullscreenMode);
        m_activeScene = jData.value("activeScene", m_activeScene);
        m_activeCamera = jData.value("activeCamera", m_activeCamera);
        m_isCpuValidationLayerEnabled = jData.value("CpuValidationLayerEnabled", m_isCpuValidationLayerEnabled);
        m_isGpuValidationLayerEnabled = jData.value("GpuValidationLayerEnabled", m_isGpuValidationLayerEnabled);
        m_VsyncEnabled = jData.value("vsync", m_VsyncEnabled);
        m_FreesyncHDROptionEnabled = jData.value("FreesyncHDROptionEnabled", m_FreesyncHDROptionEnabled);
        if (m_FreesyncHDROptionEnabled)
            m_initializeAGS = true;
        m_state.bIsBenchmarking = jData.value("benchmark", m_state.bIsBenchmarking);
        m_stablePowerState = jData.value("stablePowerState", m_stablePowerState);
        m_fontSize = jData.value("fontsize", m_fontSize);
        m_bDisableHalf = jData.value("disableHalf", m_bDisableHalf);
        m_bEnableHalf = jData.value("enableHalf", m_bEnableHalf);
        m_bDisplayHalf = jData.value("displayHalf", m_bDisplayHalf);
    };

    //read json globals from commandline
    //
    try
    {
        if (strlen(lpCmdLine) > 0)
        {
            auto j3 = json::parse(lpCmdLine);
            process(j3);
        }
    }
    catch (json::parse_error)
    {
        Trace("Error parsing commandline\n");
        exit(0);
    }

    // read config file (and override values from commandline if so)
    //
    {
        std::ifstream f("FSRSample.json");
        if (!f)
        {
            MessageBox(NULL, "Config file not found!\n", "Cauldron Panic!", MB_ICONERROR);
            exit(0);
        }

        try
        {
            f >> m_jsonConfigFile;
        }
        catch (json::parse_error)
        {
            MessageBox(NULL, "Error parsing FSRSample.json!\n", "Cauldron Panic!", MB_ICONERROR);
            exit(0);
        }
    }


    json globals = m_jsonConfigFile["globals"];
    process(globals);

    // get the list of scenes
    for (const auto & scene : m_jsonConfigFile["scenes"])
        m_sceneNames.push_back(scene["name"]);
}

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void FSRSample::OnCreate()
{
    //init the shader compiler
    InitDirectXCompiler();
    CreateShaderCache();

    // Create a instance of the renderer and initialize it, we need to do that for each GPU
    m_Node = new SampleRenderer();
    m_Node->OnCreate(&m_device, &m_swapChain, m_fontSize, UseSlowFallback(m_device.IsFp16Supported()));

    // init GUI (non gfx stuff)
    ImGUI_Init((void *)m_windowHwnd);

    // Init Camera, looking at the origin
    //
    m_roll = 0.0f;
    m_pitch = 0.0f;
    m_distance = 3.5f;

    // init magnifier params
    for (int ch = 0; ch < 3; ++ch) m_state.magnifierParams.fBorderColorRGB[ch] = MagnifierBorderColor_Free[ch]; // start at free state

    // init GUI state
    m_state.toneMapper = 0;
    m_state.bUseTAA = true;
    m_state.bUseMagnifier = false;
    m_state.bLockMagnifierPosition = m_state.bLockMagnifierPositionHistory = false;
    m_state.skyDomeType = 0;
    m_state.exposure = 1.0f;
    m_state.iblFactor = 2.0f;
    m_state.emmisiveFactor = 1.0f;
    m_state.bDrawLightFrustum = false;
    m_state.bDrawBoundingBoxes = false;
    m_state.camera.LookAt(m_roll, m_pitch, m_distance, math::Vector4(0, 0, 0, 0));
    m_state.bShowControlsWindow = true;
    m_state.bShowProfilerWindow = true;
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void FSRSample::OnDestroy()
{
    ImGUI_Shutdown();

    m_device.GPUFlush();

    m_Node->UnloadScene();
    m_Node->OnDestroyWindowSizeDependentResources();
    m_Node->OnDestroy();

    delete m_Node;

    //shut down the shader compiler 
    DestroyShaderCache(&m_device);

    if (m_pGltfLoader)
    {
        delete m_pGltfLoader;
        m_pGltfLoader = NULL;
    }
}

//--------------------------------------------------------------------------------------
//
// OnEvent, win32 sends us events and we forward them to ImGUI
//
//--------------------------------------------------------------------------------------
bool FSRSample::OnEvent(MSG msg)
{
    if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
        return true;

    // handle function keys (F1, F2...) here, rest of the input is handled
    // by imGUI later in HandleInput() function
    const WPARAM& KeyPressed = msg.wParam;
    switch (msg.message)
    {
    case WM_KEYUP:
    case WM_SYSKEYUP:
        /* WINDOW TOGGLES */
        if (KeyPressed == VK_F1) m_state.bShowControlsWindow ^= 1;
        if (KeyPressed == VK_F2) m_state.bShowProfilerWindow ^= 1;
        break;
    }

    return true;
}

//--------------------------------------------------------------------------------------
//
// OnResize
//
//--------------------------------------------------------------------------------------
void FSRSample::OnResize(bool resizeRender)
{
    RefreshRenderResolution();
    m_state.jitterSample = 0;
    if (resizeRender && m_Width && m_Height && m_Node)
    {
        m_Node->OnDestroyWindowSizeDependentResources();
        m_Node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, &m_state);
    }

    m_state.camera.SetFov(AMD_PI_OVER_4, m_Width, m_Height, 0.1f, 1000.0f);
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
void FSRSample::LoadScene(int sceneIndex)
{
    json scene = m_jsonConfigFile["scenes"][sceneIndex];

    // release everything and load the GLTF, just the light json data, the rest (textures and geometry) will be done in the main loop
    if (m_pGltfLoader != NULL)
    {
        m_Node->UnloadScene();
        m_Node->OnDestroyWindowSizeDependentResources();
        m_Node->OnDestroy();
        m_pGltfLoader->Unload();
        m_Node->OnCreate(&m_device, &m_swapChain, m_fontSize, UseSlowFallback(m_device.IsFp16Supported()));
        m_Node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, &m_state);
    }

    delete(m_pGltfLoader);
    m_pGltfLoader = new GLTFCommon();
    if (m_pGltfLoader->Load(scene["directory"], scene["filename"]) == false)
    {
        MessageBox(NULL, "The selected model couldn't be found, please check the documentation", "Cauldron Panic!", MB_ICONERROR);
        exit(0);
    }

    // Load the UI settings, and also some defaults cameras and lights, in case the GLTF has none
    {
#define LOAD(j, key, val) val = j.value(key, val)

        // global settings
        LOAD(scene, "TAA", m_state.bUseTAA);
        LOAD(scene, "toneMapper", m_state.toneMapper);
        LOAD(scene, "skyDomeType", m_state.skyDomeType);
        LOAD(scene, "exposure", m_state.exposure);
        LOAD(scene, "iblFactor", m_state.iblFactor);
        LOAD(scene, "emmisiveFactor", m_state.emmisiveFactor);
        LOAD(scene, "skyDomeType", m_state.skyDomeType);

        // Add a default light in case there are none
        //
        if (m_pGltfLoader->m_lights.size() == 0)
        {
            tfNode n;
            n.m_tranform.LookAt(PolarToVector(AMD_PI_OVER_2, 0.58f) * 3.5f, math::Vector4(0, 0, 0, 0));

            tfLight l;
            l.m_type = tfLight::LIGHT_SPOTLIGHT;
            l.m_intensity = scene.value("intensity", 1.0f);
            l.m_color = math::Vector4(1.0f, 1.0f, 1.0f, 0.0f);
            l.m_range = 15;
            l.m_outerConeAngle = AMD_PI_OVER_4;
            l.m_innerConeAngle = AMD_PI_OVER_4 * 0.9f;

            m_pGltfLoader->AddLight(n, l);
        }
        
        // set default camera
        //
        json camera = scene["camera"];
        m_activeCamera = scene.value("activeCamera", m_activeCamera);
        math::Vector4 from = GetVector(GetElementJsonArray(camera, "defaultFrom", { 0.0, 0.0, 10.0 }));
        math::Vector4 to = GetVector(GetElementJsonArray(camera, "defaultTo", { 0.0, 0.0, 0.0 }));
        m_state.camera.LookAt(from, to);
        m_roll = m_state.camera.GetYaw();
        m_pitch = m_state.camera.GetPitch();
        m_distance = m_state.camera.GetDistance();

        // set benchmarking state if enabled 
        //
        if (m_state.bIsBenchmarking)
        {
            std::string deviceName;
            std::string driverVersion;
            m_device.GetDeviceInfo(&deviceName, &driverVersion);
            BenchmarkConfig(scene["BenchmarkSettings"], m_activeCamera, m_pGltfLoader, deviceName, driverVersion);
        } 

        // indicate the mainloop we started loading a GLTF and it needs to load the rest (textures and geometry)
        m_loadingScene = true;
    }
}


//--------------------------------------------------------------------------------------
//
// OnUpdate
//
//--------------------------------------------------------------------------------------
void FSRSample::OnUpdate()
{
    ImGuiIO& io = ImGui::GetIO();

    //If the mouse was not used by the GUI then it's for the camera
    //
    if (io.WantCaptureMouse)
    {
        io.MouseDelta.x = 0;
        io.MouseDelta.y = 0;
        io.MouseWheel = 0;
    }
    UpdateCamera(m_state.camera, io);

    HandleInput(io);
}
static void ToggleMagnifierLockedState(State& state, const ImGuiIO& io)
{
    if (state.bUseMagnifier)
    {
        state.bLockMagnifierPositionHistory = state.bLockMagnifierPosition; // record histroy
        state.bLockMagnifierPosition = !state.bLockMagnifierPosition; // flip state
        const bool bLockSwitchedOn = !state.bLockMagnifierPositionHistory && state.bLockMagnifierPosition;
        const bool bLockSwitchedOff = state.bLockMagnifierPositionHistory && !state.bLockMagnifierPosition;
        if (bLockSwitchedOn)
        {
            const ImGuiIO& io = ImGui::GetIO();
            state.LockedMagnifiedScreenPositionX = static_cast<int>(io.MousePos.x);
            state.LockedMagnifiedScreenPositionY = static_cast<int>(io.MousePos.y);
            for (int ch = 0; ch < 3; ++ch) state.magnifierParams.fBorderColorRGB[ch] = MagnifierBorderColor_Locked[ch];
        }
        else if (bLockSwitchedOff)
        {
            for (int ch = 0; ch < 3; ++ch) state.magnifierParams.fBorderColorRGB[ch] = MagnifierBorderColor_Free[ch];
        }
    }
}
void FSRSample::HandleInput(const ImGuiIO& io)
{
    auto fnIsKeyTriggered = [&io](char key) { return io.KeysDown[key] && io.KeysDownDuration[key] == 0.0f; };
    
    // Handle Keyboard/Mouse input here

    /* MAGNIFIER CONTROLS */
    if (fnIsKeyTriggered('L'))                       ToggleMagnifierLockedState(m_state, io);
    if (fnIsKeyTriggered('M') || io.MouseClicked[2]) m_state.bUseMagnifier ^= 1; // middle mouse / M key toggles magnifier

    if (io.MouseClicked[1] && m_state.bUseMagnifier) // right mouse click
    {
        ToggleMagnifierLockedState(m_state, io);
    }

	if (fnIsKeyTriggered('1'))
	{
		m_state.m_nUpscaleType = 0;
        m_device.GPUFlush();
		OnResize(true);
	}
	if (fnIsKeyTriggered('2'))
	{
		m_state.m_nUpscaleType = 1;
        m_nUpscaleRatio = 3;
        m_device.GPUFlush();
		OnResize(true);
        m_state.mipBias = mipBias[m_nUpscaleRatio];
	}
    if (fnIsKeyTriggered('3'))
    {
        m_state.m_nUpscaleType = 1;
        m_nUpscaleRatio = 2;
        m_device.GPUFlush();
        OnResize(true);
        m_state.mipBias = mipBias[m_nUpscaleRatio];
    }
    if (fnIsKeyTriggered('4'))
    {
        m_state.m_nUpscaleType = 1;
        m_nUpscaleRatio = 1;
        m_device.GPUFlush();
        OnResize(true);
        m_state.mipBias = mipBias[m_nUpscaleRatio];
    }
    if (fnIsKeyTriggered('5'))
    {
        m_state.m_nUpscaleType = 1;
        m_nUpscaleRatio = 0;
        m_device.GPUFlush();
        OnResize(true);
        m_state.mipBias = mipBias[m_nUpscaleRatio];
    }
	if (fnIsKeyTriggered('0'))
	{
		m_state.m_nUpscaleType = 2;
        m_device.GPUFlush();
		OnResize(true);
	}
}
void FSRSample::UpdateCamera(Camera& cam, const ImGuiIO& io)
{
    // Sets Camera based on UI selection (WASD, Orbit or any of the GLTF cameras)
    //
    if ((io.KeyCtrl == false) && (io.MouseDown[0] == true))
    {
        m_roll -= io.MouseDelta.x / 100.f;
        m_pitch += io.MouseDelta.y / 100.f;
    }

    // Choose camera movement depending on setting
    //
    if (m_activeCamera == 0)
    {
        // If nothing has changed, don't calculate an update (we are getting micro changes in view causing bugs)
        if (!io.MouseWheel && (!io.MouseDown[0] || (!io.MouseDelta.x && !io.MouseDelta.y)))
            return;
        //  Orbiting
        //
        m_distance -= (float)io.MouseWheel / 3.0f;
        m_distance = std::max<float>(m_distance, 0.1f);

        bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);

        cam.UpdateCameraPolar(m_roll, m_pitch, panning ? -io.MouseDelta.x / 100.0f : 0.0f, panning ? io.MouseDelta.y / 100.0f : 0.0f, m_distance);
    }
    else if (m_activeCamera == 1)
    {
        //  WASD
        //
        cam.UpdateCameraWASD(m_roll, m_pitch, io.KeysDown, io.DeltaTime);
    }
    else if (m_activeCamera > 1)
    {
        // Use a camera from the GLTF
        // 
        m_pGltfLoader->GetCamera(m_activeCamera - 2, &cam);
        m_roll = cam.GetYaw();
        m_pitch = cam.GetPitch();
    }
}
//--------------------------------------------------------------------------------------
//
// BuildUI, all UI code should be here
//
//--------------------------------------------------------------------------------------
// To use the 'disabled UI state' functionality (ImGuiItemFlags_Disabled), include internal header
// https://github.com/ocornut/imgui/issues/211#issuecomment-339241929
#include "imgui_internal.h"
void FSRSample::BuildUI()
{
    // if we haven't initialized GLTFLoader yet, don't draw UI.
    if (m_pGltfLoader == nullptr)
    {
        LoadScene(m_activeScene);
        return;
    }
    auto fnDisableUIStateBegin = [](const bool& bEnable)
    {
        if (!bEnable)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }
    };
    auto fnDisableUIStateEnd = [](const bool& bEnable)
    {
        if (!bEnable)
        {
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }
    };

    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;

    const uint32_t W = m_Width;
    const uint32_t H = m_Height;

    const uint32_t PROFILER_WINDOW_PADDIG_X = 10;
    const uint32_t PROFILER_WINDOW_PADDIG_Y = 10;
    const uint32_t PROFILER_WINDOW_SIZE_X = 330;
    const uint32_t PROFILER_WINDOW_SIZE_Y = 400;
    const uint32_t PROFILER_WINDOW_POS_X = W - PROFILER_WINDOW_PADDIG_X - PROFILER_WINDOW_SIZE_X;
    const uint32_t PROFILER_WINDOW_POS_Y = PROFILER_WINDOW_PADDIG_Y;

    const uint32_t CONTROLS_WINDOW_POS_X = 10;
    const uint32_t CONTROLS_WINDOW_POS_Y = 10;
    const uint32_t CONTROLW_WINDOW_SIZE_X = 350;
    const uint32_t CONTROLW_WINDOW_SIZE_Y = 650;

    // Render CONTROLS window
    //
    ImGui::SetNextWindowPos(ImVec2(CONTROLS_WINDOW_POS_X, CONTROLS_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(CONTROLW_WINDOW_SIZE_X, CONTROLW_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

    if (m_state.bShowControlsWindow)
    {
        ImGui::Begin("CONTROLS (F1)", &m_state.bShowControlsWindow);
        if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Play", &m_bPlay);
            ImGui::SliderFloat("Time", &m_time, 0, 30);
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
        {
            char* cameraControl[] = { "Orbit", "WASD", "cam #0", "cam #1", "cam #2", "cam #3" , "cam #4", "cam #5" };

            if (m_activeCamera >= m_pGltfLoader->m_cameras.size() + 2)
                m_activeCamera = 0;
            ImGui::Combo("Camera", &m_activeCamera, cameraControl, min((int)(m_pGltfLoader->m_cameras.size() + 2), _countof(cameraControl)));

            auto getterLambda = [](void* data, int idx, const char** out_str)->bool { *out_str = ((std::vector<std::string> *)data)->at(idx).c_str(); return true; };
            if (ImGui::Combo("Model", &m_activeScene, getterLambda, &m_sceneNames, (int)m_sceneNames.size()))
            {
                LoadScene(m_activeScene);

                //bail out as we need to reload everything
                ImGui::End();
                ImGui::EndFrame();
                ImGui::NewFrame();
                return;
            }

            ImGui::SliderFloat("Emissive Intensity", &m_state.emmisiveFactor, 1.0f, 1000.0f, NULL, 1.0f);

            const char* skyDomeType[] = { "Procedural Sky", "cubemap", "Simple clear" };
            ImGui::Combo("Skydome", &m_state.skyDomeType, skyDomeType, _countof(skyDomeType));

            ImGui::SliderFloat("IBL Factor", &m_state.iblFactor, 0.0f, 3.0f);
            for (int i = 0; i < m_pGltfLoader->m_lights.size(); i++)
            {
                ImGui::SliderFloat(format("Light %i Intensity", i).c_str(), &m_pGltfLoader->m_lights[i].m_intensity, 0.0f, 50.0f);
            }
            if (ImGui::Button("Set Spot Light 0 to Camera's View"))
            {
                int idx = m_pGltfLoader->m_lightInstances[0].m_nodeIndex;
                m_pGltfLoader->m_nodes[idx].m_tranform.LookAt(m_state.camera.GetPosition(), m_state.camera.GetPosition() - m_state.camera.GetDirection());
                m_pGltfLoader->m_animatedMats[idx] = m_pGltfLoader->m_nodes[idx].m_tranform.GetWorldMat();
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

		if (ImGui::CollapsingHeader("Upscaling ", ImGuiTreeNodeFlags_DefaultOpen)) //blank space needed, otherwise the identically named 'combo' drop down below won't work
		{
			const char* modes[] = { "Bilinear [Hotkey 1]", "FSR 1.0 [Hotkeys 2-5]", "Native (no upscaling) [Hotkey 0]" };
            if (ImGui::Combo("Upscaling", &m_state.m_nUpscaleType, modes, _countof(modes)))
            {
                m_device.GPUFlush();
                OnResize(true);
            }
			if (m_state.m_nUpscaleType < 2)
			{
				const char* ratios[] = { "Ultra Quality (1.3x) [Hotkey 5]", "Quality (1.5x) [Hotkey 4]", "Balanced (1.7x) [Hotkey 3]", "Performance (2x) [Hotkey 2]", "Custom" };
                if (ImGui::Combo("Scale mode", &m_nUpscaleRatio, ratios, _countof(ratios)))
                {
                    m_device.GPUFlush();
                    OnResize(true);
                }
                if (m_state.m_nUpscaleType == 1 && m_nUpscaleRatio < 4)
                    m_state.mipBias = mipBias[m_nUpscaleRatio];
                if (m_nUpscaleRatio == 4 && ImGui::SliderFloat("Custom factor", &m_fUpscaleRatio, 1.0f, 2.0f))
                {
                    m_device.GPUFlush();
                    OnResize(true);
                }
            } else
                m_state.mipBias = mipBias[4];
            if (m_state.m_nUpscaleType == 1)
            {
                ImGui::Checkbox("FSR 1.0 Sharpening", &m_state.bUseRcas);
                if( m_state.bUseRcas )
                    ImGui::SliderFloat("Sharpening attenuation", &m_state.rcasAttenuation, 0.0f, 2.0f);
            }
            if (m_state.m_nUpscaleType)
            {
                ImGui::SliderFloat("Mip LOD bias", &m_state.mipBias, -3.0f, 0.0f);
                if (m_state.m_nUpscaleType == 2)
                    mipBias[4] = m_state.mipBias;
                else
                    if( m_nUpscaleRatio < 4 )
                        mipBias[m_nUpscaleRatio] = m_state.mipBias;
            } else
                m_state.mipBias = 0.0f;
            ImGui::Text("Render resolution: %dx%d", m_state.renderWidth, m_state.renderHeight);
            ImGui::Text("Display resolution: %dx%d", m_Width, m_Height);
            if (m_bDisplayHalf)
            {
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Text(UseSlowFallback(m_device.IsFp16Supported()) ? "Slow fallback" : "Fast path");
            }
		}

		ImGui::Spacing();
		ImGui::Spacing();

        if (ImGui::CollapsingHeader("PostProcessing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* tonemappers[] = { "Timothy", "DX11DSK", "Reinhard", "Uncharted2Tonemap", "ACES", "No tonemapper" };
            ImGui::Combo("Tonemapper", &m_state.toneMapper, tonemappers, _countof(tonemappers));

            ImGui::SliderFloat("Exposure", &m_state.exposure, 0.0f, 4.0f);

            ImGui::Checkbox("TAA", &m_state.bUseTAA);
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Magnifier", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static constexpr float MAGNIFICATION_AMOUNT_MIN = 1.0f;
            static constexpr float MAGNIFICATION_AMOUNT_MAX = 32.0f;
            static constexpr float MAGNIFIER_RADIUS_MIN = 0.01f;
            static constexpr float MAGNIFIER_RADIUS_MAX = 0.85f;

            // read in Magnifier pass parameters from the UI & app state
            MagnifierPS::PassParameters& params = m_state.magnifierParams;
            params.uImageHeight = m_Height;
            params.uImageWidth = m_Width;
            params.iMousePos[0] = m_state.bLockMagnifierPosition ? m_state.LockedMagnifiedScreenPositionX : static_cast<int>(io.MousePos.x);
            params.iMousePos[1] = m_state.bLockMagnifierPosition ? m_state.LockedMagnifiedScreenPositionY : static_cast<int>(io.MousePos.y);

            ImGui::Checkbox("Show Magnifier (M)", &m_state.bUseMagnifier);

            fnDisableUIStateBegin(m_state.bUseMagnifier);
            {
                // use a local bool state here to track locked state through the UI widget,
                // and then call ToggleMagnifierLockedState() to update the persistent state (m_state).
                // the keyboard input for toggling lock directly operates on the persistent state.
                const bool bIsMagnifierCurrentlyLocked = m_state.bLockMagnifierPosition;
                bool bMagnifierToggle = bIsMagnifierCurrentlyLocked;
                ImGui::Checkbox("Lock Position (L)", &bMagnifierToggle);
                if (bMagnifierToggle != bIsMagnifierCurrentlyLocked)
                {
                    ToggleMagnifierLockedState(m_state, io);
                }
                ImGui::SliderFloat("Screen Size", &params.fMagnifierScreenRadius, MAGNIFIER_RADIUS_MIN, MAGNIFIER_RADIUS_MAX);
                ImGui::SliderFloat("Magnification", &params.fMagnificationAmount, MAGNIFICATION_AMOUNT_MIN, MAGNIFICATION_AMOUNT_MAX);
                ImGui::SliderInt("OffsetX", &params.iMagnifierOffset[0], -(int)m_Width, m_Width);
                ImGui::SliderInt("OffsetY", &params.iMagnifierOffset[1], -(int)m_Height, m_Height);
            }
            fnDisableUIStateEnd(m_state.bUseMagnifier);
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Presentation Mode", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Spacing();
            ImGui::Spacing();
            if (ImGui::Checkbox("VSync", &m_VsyncEnabled))
                m_swapChain.SetVSync(m_VsyncEnabled);
            ImGui::Spacing();
            ImGui::Spacing();
            const char* fullscreenModes[] = { "Windowed", "BorderlessFullscreen", "ExclusiveFullscreen" };
            if (ImGui::Combo("Fullscreen Mode", (int*)&m_fullscreenMode, fullscreenModes, _countof(fullscreenModes)))
            {
                if (m_previousFullscreenMode != m_fullscreenMode)
                {
                    HandleFullScreen();
                    m_previousFullscreenMode = m_fullscreenMode;
                }
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (m_FreesyncHDROptionEnabled && ImGui::CollapsingHeader("FreeSync HDR", ImGuiTreeNodeFlags_DefaultOpen))
        {

            static bool openWarning = false;
            const char** displayModeNames = &m_displayModesNamesAvailable[0];
            DisplayMode old = m_currentDisplayModeNamesIndex;
            if (ImGui::Combo("Display Mode", (int*)&m_currentDisplayModeNamesIndex, displayModeNames, (int)m_displayModesNamesAvailable.size()))
            {
                if (m_fullscreenMode != PRESENTATIONMODE_WINDOWED)
                {
                    UpdateDisplay(m_disableLocalDimming);
                    OnResize(old != m_currentDisplayModeNamesIndex);
                    m_previousDisplayModeNamesIndex = m_currentDisplayModeNamesIndex;
                }
                else if (CheckIfWindowModeHdrOn() &&
                    (m_displayModesAvailable[m_currentDisplayModeNamesIndex] == DISPLAYMODE_SDR ||
                        m_displayModesAvailable[m_currentDisplayModeNamesIndex] == DISPLAYMODE_HDR10_2084 ||
                        m_displayModesAvailable[m_currentDisplayModeNamesIndex] == DISPLAYMODE_HDR10_SCRGB))
                {
                    UpdateDisplay(m_disableLocalDimming);
                    OnResize(old != m_currentDisplayModeNamesIndex);
                    m_previousDisplayModeNamesIndex = m_currentDisplayModeNamesIndex;
                }
                else
                {
                    openWarning = true;
                    m_currentDisplayModeNamesIndex = m_previousDisplayModeNamesIndex;
                }
            }

            if (openWarning)
            {
                ImGui::OpenPopup("Display Modes Warning");
                ImGui::BeginPopupModal("Display Modes Warning", NULL, ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::Text("\nChanging display modes is only available either using HDR toggle in windows display setting for HDR10 modes or in fullscreen for FS HDR modes\n\n");
                if (ImGui::Button("Cancel", ImVec2(120, 0))) { openWarning = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            if (m_displayModesAvailable[m_currentDisplayModeNamesIndex] == DisplayMode::DISPLAYMODE_FSHDR_Gamma22 ||
                m_displayModesAvailable[m_currentDisplayModeNamesIndex] == DisplayMode::DISPLAYMODE_FSHDR_SCRGB)
            {
                static bool selectedDisableLocaldimmingSetting = false;
                if (ImGui::Checkbox("Disable Local Dimming", &selectedDisableLocaldimmingSetting))
                {
                    UpdateDisplay(m_disableLocalDimming);
                    OnResize(true);
                }
            }
        }


        ImGui::End(); // CONTROLS
    }


    // Render PROFILER window
    //
    if (m_state.bShowProfilerWindow)
    {
        constexpr size_t NUM_FRAMES = 128;
        static float FRAME_TIME_ARRAY[NUM_FRAMES] = { 0 };

        // track highest frame rate and determine the max value of the graph based on the measured highest value
        static float RECENT_HIGHEST_FRAME_TIME = 0.0f;
        constexpr int FRAME_TIME_GRAPH_MAX_FPS[] = { 800, 240, 120, 90, 60, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
        static float  FRAME_TIME_GRAPH_MAX_VALUES[_countof(FRAME_TIME_GRAPH_MAX_FPS)] = { 0 }; // us
        for (int i = 0; i < _countof(FRAME_TIME_GRAPH_MAX_FPS); ++i) { FRAME_TIME_GRAPH_MAX_VALUES[i] = 1000000.f / FRAME_TIME_GRAPH_MAX_FPS[i]; }

        //scrolling data and average FPS computing
        const std::vector<TimeStamp>& timeStamps = m_Node->GetTimingValues();
        const bool bTimeStampsAvailable = timeStamps.size() > 0;
        if (bTimeStampsAvailable)
        {
            RECENT_HIGHEST_FRAME_TIME = 0;
            FRAME_TIME_ARRAY[NUM_FRAMES - 1] = timeStamps.back().m_microseconds;
            for (uint32_t i = 0; i < NUM_FRAMES - 1; i++)
            {
                FRAME_TIME_ARRAY[i] = FRAME_TIME_ARRAY[i + 1];
            }
            RECENT_HIGHEST_FRAME_TIME = max(RECENT_HIGHEST_FRAME_TIME, FRAME_TIME_ARRAY[NUM_FRAMES - 1]);
        }
        
        // UI
        ImGui::SetNextWindowPos(ImVec2((float)PROFILER_WINDOW_POS_X, (float)PROFILER_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(PROFILER_WINDOW_SIZE_X, PROFILER_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);
        ImGui::Begin("PROFILER (F2)", &m_state.bShowProfilerWindow);

        static int sFps = 0;
        static float sDelta = 0.0f;
        static int sFrameCount = 0;
        sFrameCount++;
        sDelta += (float)m_deltaTime;
        if (sDelta >= 1000.0f)
        {
            if (sDelta >= 2000.0f)
                sDelta = 0.0f;
            else
                sDelta -= 1000.0f;
            sFps = sFrameCount;
            sFrameCount = 0;
        }
        ImGui::Text("API        : %s", m_systemInfo.mGfxAPI.c_str());
        ImGui::Text("GPU        : %s", m_systemInfo.mGPUName.c_str());
        ImGui::Text("CPU        : %s", m_systemInfo.mCPUName.c_str());
        ImGui::Text("FPS        : %d", sFps);

        if (ImGui::CollapsingHeader("GPU Timings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::string msOrUsButtonText = m_state.bShowMilliseconds ? "Switch to microseconds" : "Switch to milliseconds";
            if (ImGui::Button(msOrUsButtonText.c_str())) {
                m_state.bShowMilliseconds = !m_state.bShowMilliseconds;
            }            
            ImGui::Spacing();

            // find the index of the FrameTimeGraphMaxValue as the next higher-than-recent-highest-frame-time in the pre-determined value list
            size_t iFrameTimeGraphMaxValue = 0;
            for (int i = 0; i < _countof(FRAME_TIME_GRAPH_MAX_VALUES); ++i)
            {
                if (RECENT_HIGHEST_FRAME_TIME < FRAME_TIME_GRAPH_MAX_VALUES[i]) // FRAME_TIME_GRAPH_MAX_VALUES are in increasing order
                {
                    iFrameTimeGraphMaxValue = min(_countof(FRAME_TIME_GRAPH_MAX_VALUES) - 1, i + 1);
                    break;
                }
            }
            ImGui::PlotLines("", FRAME_TIME_ARRAY, NUM_FRAMES, 0, "GPU frame time (us)", 0.0f, FRAME_TIME_GRAPH_MAX_VALUES[iFrameTimeGraphMaxValue], ImVec2(0, 80));

            for (uint32_t i = 0; i < timeStamps.size(); i++)
            {
                float value = m_state.bShowMilliseconds ? timeStamps[i].m_microseconds / 1000.0f : timeStamps[i].m_microseconds;
                const char* pStrUnit = m_state.bShowMilliseconds ? "ms" : "us";
                ImGui::Text("%-18s: %7.2f %s", timeStamps[i].m_label.c_str(), value, pStrUnit);
            }
        }
        ImGui::End(); // PROFILER
    }
}

//--------------------------------------------------------------------------------------
//
// OnRender, updates the state from the UI, animates, transforms and renders the scene
//
//--------------------------------------------------------------------------------------
void FSRSample::OnRender()
{
    // Do any start of frame necessities
    BeginFrame();

    ImGUI_UpdateIO();
    ImGui::NewFrame();

    if (m_loadingScene)
    {
        // the scene loads in chunks, that way we can show a progress bar
        static int loadingStage = 0;
        loadingStage = m_Node->LoadScene(m_pGltfLoader, loadingStage);
        if (loadingStage == 0)
        {
            m_time = 0;
            m_loadingScene = false;
        }
    }
    else if (m_pGltfLoader && m_state.bIsBenchmarking)
    {
        // Benchmarking takes control of the time, and exits the app when the animation is done
        std::vector<TimeStamp> timeStamps = m_Node->GetTimingValues();

        m_time = BenchmarkLoop(timeStamps, &m_state.camera, m_Node->GetScreenshotFileName());
    }
    else
    {
        // Build the UI. Note that the rendering of the UI happens later.
        BuildUI();

        // Update camera, handle keyboard/mouse input
        OnUpdate();

        // Set animation time
        if (m_bPlay)
            m_time += (float)m_deltaTime / 1000.0f;
    }

    // Animate and transform the scene
    //
    if (m_pGltfLoader)
    {
        m_pGltfLoader->SetAnimationTime(0, m_time);
        m_pGltfLoader->TransformScene(0, math::Matrix4::identity());
    }

    m_state.time = m_time;

    // Do Render frame using AFR
    //
    m_Node->OnRender(m_Width, m_Height, &m_state, &m_swapChain);

    // Framework will handle Present and some other end of frame logic
    EndFrame();
}


//--------------------------------------------------------------------------------------
//
// WinMain
//
//--------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    LPCSTR Name = "FidelityFX Super Resolution 1.0.2";

    // create new DX sample
    return RunFramework(hInstance, lpCmdLine, nCmdShow, new FSRSample(Name));
}
