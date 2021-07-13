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

#pragma once

#include "SampleRenderer.h"

// This is the main class, it manages the state of the sample and does all the high level work without touching the GPU directly.
// This class uses the GPU via the the SampleRenderer class. We would have a SampleRenderer instance for each GPU.
//
// This class takes care of:
//
//    - loading a scene (just the CPU data)
//    - updating the camera
//    - keeping track of time
//    - handling the keyboard
//    - updating the animation
//    - building the UI (but do not renders it)
//    - uses the SampleRenderer to update all the state to the GPU and do the rendering

class FSRSample : public FrameworkWindows
{
public:
    FSRSample(LPCSTR name);
    void OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight) override;
    void OnCreate() override;
    void OnDestroy() override;
    void OnRender() override;
    bool OnEvent(MSG msg) override;
    
    void BuildUI();
    void LoadScene(int sceneIndex);
    
    void OnUpdate();
    void OnResize(bool resizeRender) override;
    void OnUpdateDisplay() override {}

    void HandleInput(const ImGuiIO& io);
    void UpdateCamera(Camera& cam, const ImGuiIO& io);

    bool UseSlowFallback(bool deviceSupport)
    {
        assert(!m_bEnableHalf || !m_bDisableHalf);
        if (m_bEnableHalf)
            return false;
        else
            if (m_bDisableHalf)
                return true;
            else
                return !deviceSupport;
    }
    
private:
	void RefreshRenderResolution()
	{
		if (m_state.m_nUpscaleType == 2)
		{
			m_state.renderWidth = m_Width;
			m_state.renderHeight = m_Height;
		} else
		{
			float r = m_fUpscaleRatio;
			switch (m_nUpscaleRatio)
			{
			case 0:
				r = 1.3f;
				break;
			case 1:
				r = 1.5f;
				break;
			case 2:
				r = 1.7f;
				break;
			case 3:
				r = 2.0f;
				break;
			}
			m_state.renderWidth = uint32_t(m_Width / r);
			m_state.renderHeight = uint32_t(m_Height / r);
		}
	}

    GLTFCommon                 *m_pGltfLoader = NULL;
    bool                        m_loadingScene = false;

    SampleRenderer             *m_Node = NULL;
    State       m_state;

    float                       m_distance;
    float                       m_roll;
    float                       m_pitch;

    float                       m_fontSize;

    float                       m_time; // Time accumulator in seconds, used for animation.

    // json config file
    json                        m_jsonConfigFile;
    std::vector<std::string>    m_sceneNames;
    int                         m_activeScene;
    int                         m_activeCamera;

    bool                        m_bPlay;
    bool                        m_bDisableHalf = false;
    bool                        m_bEnableHalf = false;
    bool                        m_bDisplayHalf = false;
    float mipBias[5];
	int m_nUpscaleRatio = 1;
	float m_fUpscaleRatio = 1.5f;
};
