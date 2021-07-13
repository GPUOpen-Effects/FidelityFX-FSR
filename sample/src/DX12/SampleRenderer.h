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
#include "base/GBuffer.h"
#include "PostProc/MagnifierPS.h"
#include "FSR_Filter.h"

// We are queuing (backBufferCount + 0.5) frames, so we need to triple buffer the resources that get modified each frame
static const int backBufferCount = 3;

#define USE_SHADOWMASK false

using namespace CAULDRON_DX12;

struct State
{
	bool bShowControlsWindow;
	bool bShowProfilerWindow;

	float time;
	Camera camera;

	float exposure;
	float iblFactor;
	float emmisiveFactor;
    
	int   toneMapper;
	int   skyDomeType;
	bool  bDrawBoundingBoxes;

	bool  bUseTAA;
    bool  bUseRcas = true;
    float rcasAttenuation = 0.25f;
	bool  bIsBenchmarking;

	bool  bDrawLightFrustum;

	bool  bShowMilliseconds;

	// magnifier state
	bool  bUseMagnifier;
	bool  bLockMagnifierPosition;
	bool  bLockMagnifierPositionHistory;
	int   LockedMagnifiedScreenPositionX;
	int   LockedMagnifiedScreenPositionY;
	MagnifierPS::PassParameters magnifierParams;

	uint32_t renderWidth = 0;
	uint32_t renderHeight = 0;

	int m_nUpscaleType = 1;
	float mipBias = 0.0f;
    uint32_t jitterSample = 0;
};

#include "FSRTonemapping.h"

//
// This class deals with the GPU side of the sample.
//
class SampleRenderer
{
public:
    struct Spotlight
    {
        Camera light;
        math::Vector4 color;
        float intensity;
    };

    void OnCreate(Device* pDevice, SwapChain *pSwapChain, float fontSize, bool slowFallback = false);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, int displayWidth, int displayHeight, State *pState);
    void OnDestroyWindowSizeDependentResources();

    int LoadScene(GLTFCommon *pGLTFCommon, int stage = 0);
    void UnloadScene();

    bool GetHasTAA() const { return m_HasTAA; }
    void SetHasTAA(bool hasTAA) { m_HasTAA = hasTAA; }

    const std::vector<TimeStamp> &GetTimingValues() { return m_TimeStamps; }
    std::string& GetScreenshotFileName() { return m_pScreenShotName; }

    void OnRender(int displayWidth, int displayHeight, State *pState, SwapChain *pSwapChain);

private:
    Device                         *m_pDevice;

    bool                            m_HasTAA = false;

    // Initialize helper classes
    ResourceViewHeaps               m_resourceViewHeaps;
    UploadHeap                      m_UploadHeap;
    DynamicBufferRing               m_ConstantBufferRing;
    StaticBufferPool                m_VidMemBufferPool;
    CommandListRing                 m_CommandListRing;
    GPUTimestamps                   m_GPUTimer;

    //gltf passes
    GltfPbrPass                    *m_gltfPBR;
    GltfBBoxPass                   *m_gltfBBox;
    GltfDepthPass                  *m_gltfDepth;
    GLTFTexturesAndBuffers         *m_pGLTFTexturesAndBuffers;

    // effects
    Bloom                           m_bloom;
    SkyDome                         m_skyDome;
    DownSamplePS                    m_downSample;
    SkyDomeProc                     m_skyDomeProc;
    FSRToneMapping                  m_toneMappingPS;
    ColorConversionPS               m_colorConversionPS;
    TAA                             m_TAA;
	FSR_Filter						m_FSR;
    MagnifierPS                     m_magnifierPS;

    // GUI
    ImGUI                           m_ImGUI;

    // Temporary render targets
    GBuffer                         m_GBuffer;
    GBufferRenderPass               m_renderPassFullGBuffer;
    GBufferRenderPass               m_renderPassJustDepthAndHdr;
    Texture                         m_blueNoise;
    CBV_SRV_UAV                     m_TonemappingSrvs;
    Texture                         m_MotionVectorsDepthMap;
    DSV                             m_MotionVectorsDepthMapDSV;
    CBV_SRV_UAV                     m_MotionVectorsDepthMapSRV;

#if USE_SHADOWMASK
    // shadow mask
    Texture                         m_ShadowMask;
    CBV_SRV_UAV                     m_ShadowMaskUAV;
    CBV_SRV_UAV                     m_ShadowMaskSRV;
    ShadowResolvePass               m_shadowResolve;
#endif

    // shadowmaps
    Texture                         m_shadowMap;
    DSV                             m_ShadowMapDSV;
    CBV_SRV_UAV                     m_ShadowMapSRV;

	Texture							m_renderOutput;
	RTV								m_renderOutputRTV;
	Texture							m_displayOutput;
	CBV_SRV_UAV						m_displayOutputSRV;
	RTV								m_displayOutputRTV;
    // widgets
    Wireframe                       m_wireframe;
    WireframeBox                    m_wireframeBox;

    std::vector<TimeStamp>          m_TimeStamps;

    // screen shot
    std::string                     m_pScreenShotName = "";
    SaveTexture                     m_saveTexture;
    AsyncPool                       m_asyncPool;
};
