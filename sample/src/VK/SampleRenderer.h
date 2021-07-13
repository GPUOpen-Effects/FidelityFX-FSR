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

using namespace CAULDRON_VK;

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
	bool  bIsValidationLayerEnabled;
	bool  bVSyncIsOn;

	bool  bUseMagnifier;
	bool  bLockMagnifierPosition;
	bool  bLockMagnifierPositionHistory;
	int   LockedMagnifiedScreenPositionX;
	int   LockedMagnifiedScreenPositionY;
	MagnifierPS::PassParameters magnifierParams;

	bool  bDrawLightFrustum;
	bool  bShowMilliseconds;

	uint32_t renderWidth = 0;
	uint32_t renderHeight = 0;

    int m_nUpscaleType = 1;
    float mipBias = 0.0f;
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

    void OnCreate(Device *pDevice, SwapChain *pSwapChain, float fontSize, bool glsl);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, int displayWidth, int displayHeight, State *pState);
    void OnDestroyWindowSizeDependentResources();
    
    void OnUpdateLocalDimmingChangedResources(SwapChain *pSwapChain, State *pState);

    int LoadScene(GLTFCommon *pGLTFCommon, int stage = 0);
    void UnloadScene();

    const std::vector<TimeStamp> &GetTimingValues() { return m_TimeStamps; }

    void OnRender(int displayWidth, int displayHeight, State *pState, SwapChain *pSwapChain);

private:
    Device *m_pDevice;

    // Initialize helper classes
    ResourceViewHeaps               m_resourceViewHeaps;
    UploadHeap                      m_UploadHeap;
    DynamicBufferRing               m_ConstantBufferRing;
    StaticBufferPool                m_VidMemBufferPool;
    StaticBufferPool                m_SysMemBufferPool;
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
    ToneMappingCS                   m_toneMappingCS;
    ColorConversionPS               m_colorConversionPS;
    TAA                             m_TAA;
	FSR_Filter						m_FSR;
    MagnifierPS                     m_magnifierPS;
    VkRenderPass                    m_magnifierRenderPass;
    VkFramebuffer                   m_magnifierFramebuffer;

    // GUI
    ImGUI                           m_ImGUI;

    // GBuffer and render passes
    GBuffer                         m_GBuffer;
    GBufferRenderPass               m_renderPassFullGBufferWithClear;
    GBufferRenderPass               m_renderPassJustDepthAndHdr;
    GBufferRenderPass               m_renderPassFullGBuffer;

    // shadowmaps
    Texture                         m_shadowMap;
    VkImageView                     m_shadowMapDSV;
    VkImageView                     m_shadowMapSRV;

	Texture							m_renderOutput;
    VkRenderPass                    m_renderOutputRenderPass;
    VkFramebuffer                   m_renderOutputFramebuffer;
	Texture							m_displayOutput;
    VkImageView                     m_displayOutputSRV;
    VkRenderPass                    m_displayOutputRenderPass;
    VkRenderPass                    m_displayOutputGuiRenderPass;
    VkFramebuffer                   m_displayOutputFramebuffer;
    Texture                         m_blueNoise;
    VkImageView                     m_blueNoiseSRV;

    // widgets
    Wireframe                       m_wireframe;
    WireframeBox                    m_wireframeBox;

    VkRenderPass                    m_render_pass_shadow;
    VkFramebuffer                   m_pFrameBuffer_shadow;

    std::vector<TimeStamp>          m_TimeStamps;

    AsyncPool                       m_asyncPool;
};

