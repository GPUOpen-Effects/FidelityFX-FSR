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

#include "SampleRenderer.h"
#include "base\\SaveTexture.h"


//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreate(Device* pDevice, SwapChain *pSwapChain, float fontSize, bool slowFallback)
{
    m_pDevice = pDevice;

    // Initialize helpers

    // Create all the heaps for the resources views
    const uint32_t cbvDescriptorCount = 4000;
    const uint32_t srvDescriptorCount = 8000;
    const uint32_t uavDescriptorCount = 10;
    const uint32_t dsvDescriptorCount = 10;
    const uint32_t rtvDescriptorCount = 60;
    const uint32_t samplerDescriptorCount = 20;
    m_resourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, dsvDescriptorCount, rtvDescriptorCount, samplerDescriptorCount);

    // Create a commandlist ring for the Direct queue
    uint32_t commandListsPerBackBuffer = 8;
    m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer, pDevice->GetGraphicsQueue()->GetDesc());

    // Create a 'dynamic' constant buffer
    const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
    m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, &m_resourceViewHeaps);

    // Create a 'static' pool for vertices, indices and constant buffers
    const uint32_t staticGeometryMemSize = (5 * 128) * 1024 * 1024;
    m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, true, "StaticGeom");

    // initialize the GPU time stamps module
    m_GPUTimer.OnCreate(pDevice, backBufferCount);

    // Quick helper to upload resources, it has it's own commandList and uses suballocation.
    const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
    m_UploadHeap.OnCreate(pDevice, uploadHeapMemSize);    // initialize an upload heap (uses suballocation for faster results)

    // Create GBuffer and render passes
    //
    {
        m_GBuffer.OnCreate(
            pDevice,
            &m_resourceViewHeaps,
            {
                { GBUFFER_DEPTH, DXGI_FORMAT_D32_FLOAT},
                { GBUFFER_FORWARD, DXGI_FORMAT_R16G16B16A16_FLOAT},
                { GBUFFER_MOTION_VECTORS, DXGI_FORMAT_R16G16_FLOAT},
            },
            1
        );

        GBufferFlags fullGBuffer = GBUFFER_DEPTH | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS;
        m_renderPassFullGBuffer.OnCreate(&m_GBuffer, fullGBuffer);
        m_renderPassJustDepthAndHdr.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD);
    }

#if USE_SHADOWMASK    
    m_shadowResolve.OnCreate(m_pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing);

    // Create the shadow mask descriptors
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMaskUAV);
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMaskSRV);
#endif

    // Create a Shadowmap atlas to hold 4 cascades/spotlights
    m_shadowMap.InitDepthStencil(pDevice, "m_pShadowMap", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, 2 * 1024, 2 * 1024, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
    m_resourceViewHeaps.AllocDSVDescriptor(1, &m_ShadowMapDSV);
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMapSRV);
	m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_displayOutputSRV);
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(2, &m_TonemappingSrvs);
	m_resourceViewHeaps.AllocRTVDescriptor(1, &m_renderOutputRTV);
	m_resourceViewHeaps.AllocRTVDescriptor(1, &m_displayOutputRTV);
    m_shadowMap.CreateDSV(0, &m_ShadowMapDSV);
    m_shadowMap.CreateSRV(0, &m_ShadowMapSRV);
    m_skyDome.OnCreate(pDevice, &m_UploadHeap, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\cauldron-media\\envmaps\\papermill\\diffuse.dds", "..\\media\\cauldron-media\\envmaps\\papermill\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 4);
    m_skyDomeProc.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    m_wireframe.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    m_wireframeBox.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
    m_downSample.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_bloom.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_TAA.OnCreate(pDevice, &m_resourceViewHeaps, &m_VidMemBufferPool, false);
	m_FSR.OnCreate(pDevice, &m_resourceViewHeaps, slowFallback);
    m_magnifierPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R8G8B8A8_UNORM);

    // Create tonemapping pass
    m_toneMappingPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat(), 2, "FSR_Tonemapping.hlsl");
    m_colorConversionPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());

    // Initialize UI rendering resources
    m_ImGUI.OnCreate(pDevice, &m_UploadHeap, &m_resourceViewHeaps, &m_ConstantBufferRing, pSwapChain->GetFormat(), fontSize);

    // Make sure upload heap has finished uploading before continuing
    m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
    m_UploadHeap.FlushAndFinish();
    m_blueNoise.InitFromFile(pDevice, &m_UploadHeap, "..\\media\\cauldron-media\\noise\\temporal_blue_noise.dds");
    m_blueNoise.CreateSRV(1, &m_TonemappingSrvs);
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroy()
{
    m_asyncPool.Flush();

    m_blueNoise.OnDestroy();
    m_ImGUI.OnDestroy();
    m_colorConversionPS.OnDestroy();
    m_toneMappingPS.OnDestroy();
	m_FSR.OnDestroy();
    m_TAA.OnDestroy();
    m_bloom.OnDestroy();
    m_downSample.OnDestroy();
    m_magnifierPS.OnDestroy();
    m_wireframeBox.OnDestroy();
    m_wireframe.OnDestroy();
    m_skyDomeProc.OnDestroy();
    m_skyDome.OnDestroy();
    m_shadowMap.OnDestroy();
#if USE_SHADOWMASK
    m_shadowResolve.OnDestroy();
#endif
    m_GBuffer.OnDestroy();

    m_UploadHeap.OnDestroy();
    m_GPUTimer.OnDestroy();
    m_VidMemBufferPool.OnDestroy();
    m_ConstantBufferRing.OnDestroy();
    m_resourceViewHeaps.OnDestroy();
    m_CommandListRing.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnCreateWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, int displayWidth, int displayHeight, State *pState)
{
#if USE_SHADOWMASK
    // Create shadow mask
    //
    m_ShadowMask.Init(m_pDevice, "shadowbuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    m_ShadowMask.CreateUAV(0, &m_ShadowMaskUAV);
    m_ShadowMask.CreateSRV(0, &m_ShadowMaskSRV);
#endif

    // Create GBuffer
    //
    m_GBuffer.OnCreateWindowSizeDependentResources(pSwapChain, pState->renderWidth, pState->renderHeight);
    m_GBuffer.m_HDR.CreateSRV(0, &m_TonemappingSrvs);
    m_renderPassFullGBuffer.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight);
    m_renderPassJustDepthAndHdr.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight);
    
    m_TAA.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight, &m_GBuffer);
    // update bloom and downscaling effect
    //
    m_downSample.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight, &m_GBuffer.m_HDR, 1);
    m_bloom.OnCreateWindowSizeDependentResources(pState->renderWidth / 2, pState->renderHeight / 2, m_downSample.GetTexture(), 1, &m_GBuffer.m_HDR);
    bool renderNative = (pState->m_nUpscaleType == 2);
    bool hdr = (pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR);
    DXGI_FORMAT uiFormat = (hdr ? m_GBuffer.m_HDR.GetFormat() : pSwapChain->GetFormat());
    DXGI_FORMAT dFormat = (hdr ? uiFormat : DXGI_FORMAT_R8G8B8A8_UNORM);
    DXGI_FORMAT rFormat = (hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : pSwapChain->GetFormat());
    // Update pipelines in case the format of the RTs changed (this happens when going HDR)
    m_colorConversionPS.UpdatePipelines(pSwapChain->GetFormat(), pSwapChain->GetDisplayMode());
    m_toneMappingPS.UpdatePipelines(renderNative ? (hdr ? dFormat : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) : rFormat);
    m_ImGUI.UpdatePipeline(uiFormat);

	FLOAT cc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	m_renderOutput.InitRenderTarget(m_pDevice, "RenderOutput", &CD3DX12_RESOURCE_DESC::Tex2D(rFormat, pState->renderWidth, pState->renderHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, cc);
	m_renderOutput.CreateRTV(0, &m_renderOutputRTV);
	m_displayOutput.InitRenderTarget(m_pDevice, "DisplayOutput", &CD3DX12_RESOURCE_DESC::Tex2D(dFormat, displayWidth, displayHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), renderNative ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	m_displayOutput.CreateSRV(0, &m_displayOutputSRV);
    if( hdr )
        m_displayOutput.CreateRTV(0, &m_displayOutputRTV, -1, -1, -1);
    else
	    m_displayOutput.CreateRTV(0, &m_displayOutputRTV, -1, -1, -1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	m_FSR.OnCreateWindowSizeDependentResources(m_pDevice, m_renderOutput.GetResource(), m_displayOutput.GetResource(), displayWidth, displayHeight, pState, hdr);
	m_magnifierPS.OnCreateWindowSizeDependentResources(&m_displayOutput);
    m_magnifierPS.UpdatePipeline(dFormat);

}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroyWindowSizeDependentResources()
{
    m_displayOutput.OnDestroy();
    m_renderOutput.OnDestroy();
    m_bloom.OnDestroyWindowSizeDependentResources();
    m_downSample.OnDestroyWindowSizeDependentResources();

    m_GBuffer.OnDestroyWindowSizeDependentResources();

    m_TAA.OnDestroyWindowSizeDependentResources();
    m_FSR.OnDestroyWindowSizeDependentResources();
    m_magnifierPS.OnDestroyWindowSizeDependentResources();

#if USE_SHADOWMASK
    m_ShadowMask.OnDestroy();
#endif
    
}


//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int SampleRenderer::LoadScene(GLTFCommon *pGLTFCommon, int stage)
{
    // show loading progress
    //
    ImGui::OpenPopup("Loading");
    if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        float progress = (float)stage / 13.0f;
        ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
        ImGui::EndPopup();
    }

    // use multithreading
    AsyncPool *pAsyncPool = &m_asyncPool;

    // Loading stages
    //
    if (stage == 0)
    {
    }
    else if (stage == 5)
    {
        Profile p("m_pGltfLoader->Load");

        m_pGLTFTexturesAndBuffers = new GLTFTexturesAndBuffers();
        m_pGLTFTexturesAndBuffers->OnCreate(m_pDevice, pGLTFCommon, &m_UploadHeap, &m_VidMemBufferPool, &m_ConstantBufferRing);
    }
    else if (stage == 6)
    {
        Profile p("LoadTextures");

        // here we are loading onto the GPU all the textures and the inverse matrices
        // this data will be used to create the PBR and Depth passes       
        m_pGLTFTexturesAndBuffers->LoadTextures(pAsyncPool);
    }
    else if (stage == 7)
    {
        Profile p("m_gltfDepth->OnCreate");

        //create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
        m_gltfDepth = new GltfDepthPass();
        m_gltfDepth->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            pAsyncPool
        );
    }
    else if (stage == 9)
    {
        Profile p("m_gltfPBR->OnCreate");

        // same thing as above but for the PBR pass
        m_gltfPBR = new GltfPbrPass();
        m_gltfPBR->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            m_pGLTFTexturesAndBuffers,
            &m_skyDome,
            false,                  // use a SSAO mask
            USE_SHADOWMASK,
            &m_renderPassFullGBuffer,
            pAsyncPool
        );

    }
    else if (stage == 10)
    {
        Profile p("m_gltfBBox->OnCreate");

        // just a bounding box pass that will draw boundingboxes instead of the geometry itself
        m_gltfBBox = new GltfBBoxPass();
        m_gltfBBox->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            &m_wireframe
        );

        // we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());

    }
    else if (stage == 11)
    {
        Profile p("Flush");

        m_UploadHeap.FlushAndFinish();

        //once everything is uploaded we dont need he upload heaps anymore
        m_VidMemBufferPool.FreeUploadHeap();

        // tell caller that we are done loading the map
        return 0;
    }

    stage++;
    return stage;
}

//--------------------------------------------------------------------------------------
//
// UnloadScene
//
//--------------------------------------------------------------------------------------
void SampleRenderer::UnloadScene()
{
    m_asyncPool.Flush();

    m_pDevice->GPUFlush();

    if (m_gltfPBR)
    {
        m_gltfPBR->OnDestroy();
        delete m_gltfPBR;
        m_gltfPBR = NULL;
    }

    if (m_gltfDepth)
    {
        m_gltfDepth->OnDestroy();
        delete m_gltfDepth;
        m_gltfDepth = NULL;
    }

    if (m_gltfBBox)
    {
        m_gltfBBox->OnDestroy();
        delete m_gltfBBox;
        m_gltfBBox = NULL;
    }

    if (m_pGLTFTexturesAndBuffers)
    {
        m_pGLTFTexturesAndBuffers->OnDestroy();
        delete m_pGLTFTexturesAndBuffers;
        m_pGLTFTexturesAndBuffers = NULL;
    }

}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnRender(int displayWidth, int displayHeight, State *pState, SwapChain *pSwapChain)
{
    // Timing values
    //
    UINT64 gpuTicksPerSecond;
    m_pDevice->GetGraphicsQueue()->GetTimestampFrequency(&gpuTicksPerSecond);

    // Let our resource managers do some house keeping
    //
    m_CommandListRing.OnBeginFrame();
    m_ConstantBufferRing.OnBeginFrame();
    m_GPUTimer.OnBeginFrame(gpuTicksPerSecond, &m_TimeStamps);

    if (pState->bUseTAA)
        pState->camera.SetProjectionJitter(pState->renderWidth, pState->renderHeight, pState->jitterSample);
    else
        pState->camera.SetProjectionJitter(0.0f, 0.0f);

    // Sets the perFrame data 
    //
    per_frame *pPerFrame = NULL;
    if (m_pGLTFTexturesAndBuffers)
    {
        // fill as much as possible using the GLTF (camera, lights, ...)
        pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(pState->camera);

        // Set some lighting factors
        pPerFrame->iblFactor = pState->iblFactor;
        pPerFrame->emmisiveFactor = pState->emmisiveFactor;
        pPerFrame->invScreenResolution[0] = 1.0f / ((float)pState->renderWidth);
        pPerFrame->invScreenResolution[1] = 1.0f / ((float)pState->renderHeight);
		pPerFrame->lodBias = pState->mipBias;
        // Set shadowmaps bias and an index that indicates the rectangle of the atlas in which depth will be rendered
        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Spot))
            {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index
                pPerFrame->lights[i].depthBias = 70.0f / 100000.0f;
            }
            else if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Directional))
            {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index
                pPerFrame->lights[i].depthBias = 1000.0f / 100000.0f;
            }
            else
            {
                pPerFrame->lights[i].shadowMapIndex = -1;   // no shadow for this light
            }
        }

        m_pGLTFTexturesAndBuffers->SetPerFrameConstants();
        m_pGLTFTexturesAndBuffers->SetSkinningMatricesForSkeletons();
    }

    // command buffer calls
    //
    ID3D12GraphicsCommandList* pCmdLst1 = m_CommandListRing.GetNewCommandList();
	bool renderNative = (pState->m_nUpscaleType == 2);
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Begin Frame");
    bool hdr = (pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR);
	{
		D3D12_RESOURCE_BARRIER bs[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, hdr ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST),
				CD3DX12_RESOURCE_BARRIER::Transition(m_renderOutput.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		};
		pCmdLst1->ResourceBarrier(_countof(bs), bs);
	}
	
    // Render spot lights shadow map atlas  ------------------------------------------
    //
    if (m_gltfDepth && pPerFrame != NULL)
    {
        pCmdLst1->ClearDepthStencilView(m_ShadowMapDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear shadow map");

        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if (!(pPerFrame->lights[i].type == LightType_Spot || pPerFrame->lights[i].type == LightType_Directional))
                continue;

            // Set the RT's quadrant where to render the shadowmap (these viewport offsets need to match the ones in shadowFiltering.h)
            uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
            uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
            uint32_t viewportWidth = m_shadowMap.GetWidth() / 2;
            uint32_t viewportHeight = m_shadowMap.GetHeight() / 2;
            SetViewportAndScissor(pCmdLst1, viewportOffsetsX[i] * viewportWidth, viewportOffsetsY[i] * viewportHeight, viewportWidth, viewportHeight);
            pCmdLst1->OMSetRenderTargets(0, NULL, false, &m_ShadowMapDSV.GetCPU());

			per_frame*cbDepthPerFrame = m_gltfDepth->SetPerFrameConstants();
            cbDepthPerFrame->mCameraCurrViewProj = pPerFrame->lights[i].mLightViewProj;
			cbDepthPerFrame->lodBias = pState->mipBias;

            m_gltfDepth->Draw(pCmdLst1);

            m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow map");
            shadowMapIndex++;
        }
    }
	pCmdLst1->DiscardResource(m_renderOutput.GetResource(), 0);
	pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Shadow resolve ---------------------------------------------------------------------------
    //
#if USE_SHADOWMASK
    if (pPerFrame != NULL)
    {
        const D3D12_RESOURCE_BARRIER preShadowResolve[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(m_MotionVectorsDepthMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        };
        pCmdLst1->ResourceBarrier(ARRAYSIZE(preShadowResolve), preShadowResolve);

        ShadowResolveFrame shadowResolveFrame;
        shadowResolveFrame.m_Width = pState->renderWidth;
        shadowResolveFrame.m_Height = pState->renderHeight;
        shadowResolveFrame.m_ShadowMapSRV = m_ShadowMapSRV;
        shadowResolveFrame.m_DepthBufferSRV = m_MotionVectorsDepthMapSRV;
        shadowResolveFrame.m_ShadowBufferUAV = m_ShadowMaskUAV;

        m_shadowResolve.Draw(pCmdLst1, m_pGLTFTexturesAndBuffers, &shadowResolveFrame);

        const D3D12_RESOURCE_BARRIER postShadowResolve[] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_MotionVectorsDepthMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
        };
        pCmdLst1->ResourceBarrier(ARRAYSIZE(postShadowResolve), postShadowResolve);
    }
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow resolve");
#endif
	D3D12_VIEWPORT vpr = { 0.0f, 0.0f, static_cast<float>(pState->renderWidth), static_cast<float>(pState->renderHeight), 0.0f, 1.0f };
	D3D12_RECT srr = { 0, 0, (LONG)pState->renderWidth, (LONG)pState->renderHeight };
    // Render Scene to the GBuffer ------------------------------------------------
    //
    if (pPerFrame != NULL)
    {
        pCmdLst1->RSSetViewports(1, &vpr);
        pCmdLst1->RSSetScissorRects(1, &srr);

        if (m_gltfPBR)
        {
            std::vector<GltfPbrPass::BatchList> opaque, transparent;
            m_gltfPBR->BuildBatchLists(&opaque, &transparent);

            // Render opaque geometry
            // 
            {
                m_renderPassFullGBuffer.BeginPass(pCmdLst1, true);
#if USE_SHADOWMASK
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_ShadowMaskSRV, &solid);
#else
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_ShadowMapSRV, &opaque);
#endif
                m_GPUTimer.GetTimeStamp(pCmdLst1, "PBR Opaque");
                m_renderPassFullGBuffer.EndPass();
            }

            // draw skydome
            // 
            {
                m_renderPassJustDepthAndHdr.BeginPass(pCmdLst1, false);

                // Render skydome
                //
                if (pState->skyDomeType == 1)
                {
                    math::Matrix4 clipToView = math::inverse(pPerFrame->mCameraCurrViewProj);
                    m_skyDome.Draw(pCmdLst1, clipToView);
                    m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome cube");
                }
                else if (pState->skyDomeType == 0)
                {
                    SkyDomeProc::Constants skyDomeConstants;
                    skyDomeConstants.invViewProj = math::inverse(pPerFrame->mCameraCurrViewProj);
                    skyDomeConstants.vSunDirection = math::Vector4(1.0f, 0.05f, 0.0f, 0.0f);
                    skyDomeConstants.turbidity = 10.0f;
                    skyDomeConstants.rayleigh = 2.0f;
                    skyDomeConstants.mieCoefficient = 0.005f;
                    skyDomeConstants.mieDirectionalG = 0.8f;
                    skyDomeConstants.luminance = 1.0f;
                    m_skyDomeProc.Draw(pCmdLst1, skyDomeConstants);

                    m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome proc");
                }

                m_renderPassJustDepthAndHdr.EndPass();
            }

            // draw transparent geometry
            //
            {
                m_renderPassFullGBuffer.BeginPass(pCmdLst1, false);

                std::sort(transparent.begin(), transparent.end());
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_ShadowMapSRV, &transparent);
                m_GPUTimer.GetTimeStamp(pCmdLst1, "PBR Transparent");

                m_renderPassFullGBuffer.EndPass();
            }
        }

        // draw object's bounding boxes
        //
        if (m_gltfBBox && pPerFrame != NULL)
        {
            if (pState->bDrawBoundingBoxes)
            {
                m_gltfBBox->Draw(pCmdLst1, pPerFrame->mCameraCurrViewProj);

                m_GPUTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
            }
        }

        // draw light's frustums
        //
        if (pState->bDrawLightFrustum && pPerFrame != NULL)
        {
            UserMarker marker(pCmdLst1, "light frustrums");

            math::Vector4 vCenter = math::Vector4(0.0f, 0.0f, 0.5f, 0.0f);
            math::Vector4 vRadius = math::Vector4(1.0f, 1.0f, 0.5f, 0.0f);
            math::Vector4 vColor = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
            {
                math::Matrix4 spotlightMatrix = math::inverse(pPerFrame->lights[i].mLightViewProj); // XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
                math::Matrix4 worldMatrix = pPerFrame->mCameraCurrViewProj * spotlightMatrix; //spotlightMatrix * pPerFrame->mCameraCurrViewProj;
                m_wireframeBox.Draw(pCmdLst1, &m_wireframe, worldMatrix, vCenter, vRadius, vColor);
            }

            m_GPUTimer.GetTimeStamp(pCmdLst1, "Light's frustum");
        }
    }
    pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    D3D12_RESOURCE_BARRIER preResolve[1] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    pCmdLst1->ResourceBarrier(1, preResolve);

    // Post proc---------------------------------------------------------------------------
    //

    // Bloom, takes HDR as input and applies bloom to it.
    //
    {
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { m_GBuffer.m_HDRRTV.GetCPU() };
        pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

        m_downSample.Draw(pCmdLst1);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample");

        m_bloom.Draw(pCmdLst1, &m_GBuffer.m_HDR);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Bloom");
    }

    // Apply TAA & Sharpen to m_HDR
    //
    if (pState->bUseTAA)
    {
        m_TAA.Draw(pCmdLst1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "TAA");
    }

    // Start tracking input/output resources at this point to handle HDR and SDR render paths 
    ID3D12Resource*              pRscCurrentInput = m_GBuffer.m_HDR.GetResource();
    D3D12_CPU_DESCRIPTOR_HANDLE  RTVCurrentOutput = m_GBuffer.m_HDRRTV.GetCPU();
    CBV_SRV_UAV                  UAVCurrentOutput = m_GBuffer.m_HDRUAV;
    
	D3D12_VIEWPORT vpd = { 0.0f, 0.0f, static_cast<float>(displayWidth), static_cast<float>(displayHeight), 0.0f, 1.0f };
	D3D12_RECT srd = { 0, 0, (LONG)displayWidth, (LONG)displayHeight };

    // submit command buffer #1
    ThrowIfFailed(pCmdLst1->Close());
    ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

    // Wait for swapchain (we are going to render to it) -----------------------------------
    //
    pSwapChain->WaitForSwapChain();

    // Keep tracking input/output resource views 
    pRscCurrentInput = m_GBuffer.m_HDR.GetResource(); // these haven't changed, re-assign as sanity check
    RTVCurrentOutput = *pSwapChain->GetCurrentBackBufferRTV();
    UAVCurrentOutput = {}; // no BackBufferUAV.


    ID3D12GraphicsCommandList* pCmdLst2 = m_CommandListRing.GetNewCommandList();

	pCmdLst2->RSSetViewports(1, &vpr);
	pCmdLst2->RSSetScissorRects(1, &srr);
	pCmdLst2->OMSetRenderTargets(1, renderNative ? &m_displayOutputRTV.GetCPU() : &m_renderOutputRTV.GetCPU(), true, NULL);

    // Tonemapping ------------------------------------------------------------------------
    //
    {
        m_toneMappingPS.Draw(pCmdLst2, &m_TonemappingSrvs, pState->m_nUpscaleType == 1 ? hdr : false, pState);
        m_GPUTimer.GetTimeStamp(pCmdLst2, "Tone mapping");
		D3D12_RESOURCE_BARRIER bs[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
				CD3DX12_RESOURCE_BARRIER::Transition(m_renderOutput.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		pCmdLst2->ResourceBarrier(_countof(bs), bs);
    }
	if (!renderNative)
	{
		m_FSR.Upscale(pCmdLst2, displayWidth, displayHeight, pState, &m_ConstantBufferRing, hdr);
		m_GPUTimer.GetTimeStamp(pCmdLst2, pState->m_nUpscaleType == 1 ? "FSR 1.0" : "Upscaling");
	}
		
	pCmdLst2->RSSetViewports(1, &vpd);
	pCmdLst2->RSSetScissorRects(1, &srd);
	if (pState->bUseMagnifier)
	{
		pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_displayOutput.GetResource(), renderNative ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		m_magnifierPS.Draw(pCmdLst2, pState->magnifierParams, m_displayOutputSRV);
        m_GPUTimer.GetTimeStamp(pCmdLst2, "Magnifier");
        pCmdLst2->OMSetRenderTargets(1, &m_magnifierPS.GetPassOutputRTVSrgb().GetCPU(), true, NULL);
        m_ImGUI.Draw(pCmdLst2);
        if( hdr )
            pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_magnifierPS.GetPassOutputResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        else
        {
            D3D12_RESOURCE_BARRIER bs[] = {
                    CD3DX12_RESOURCE_BARRIER::Transition(m_magnifierPS.GetPassOutputResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                    CD3DX12_RESOURCE_BARRIER::Transition(m_displayOutput.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            };
            pCmdLst2->ResourceBarrier(_countof(bs), bs);
        }
    } else
    {
        if (!renderNative)
        {
            pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_displayOutput.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET));
            pCmdLst2->OMSetRenderTargets(1, &m_displayOutputRTV.GetCPU(), true, NULL);
        }
        m_ImGUI.Draw(pCmdLst2);
        pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_displayOutput.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, hdr ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE));
    }
    m_GPUTimer.GetTimeStamp(pCmdLst2, "ImGUI Rendering");

    if (hdr)
    {
        pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), true, NULL);
        pCmdLst2->RSSetViewports(1, &vpd);
        pCmdLst2->RSSetScissorRects(1, &srd);
        m_colorConversionPS.Draw(pCmdLst2, pState->bUseMagnifier ? &m_magnifierPS.GetPassOutputSRV() : &m_displayOutputSRV);
    } else
    {
        pCmdLst2->CopyResource(pSwapChain->GetCurrentBackBufferResource(), pState->bUseMagnifier ? m_magnifierPS.GetPassOutputResource() : m_displayOutput.GetResource());
        pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET));
    }
    pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_displayOutput.GetResource(), hdr ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE, renderNative ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	if (pState->bUseMagnifier)
		pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_magnifierPS.GetPassOutputResource(), hdr ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));


    m_GPUTimer.GetTimeStamp(pCmdLst2, "Color conversion");

    if (!m_pScreenShotName.empty())
    {
        m_saveTexture.CopyRenderTargetIntoStagingTexture(m_pDevice->GetDevice(), pCmdLst2, pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    // Transition swapchain into present mode

    pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    m_GPUTimer.OnEndFrame();

    m_GPUTimer.CollectTimings(pCmdLst2);

    // Close & Submit the command list #2 -------------------------------------------------
    //
    ThrowIfFailed(pCmdLst2->Close());

    ID3D12CommandList* CmdListList2[] = { pCmdLst2 };
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);

    if (!m_pScreenShotName.empty())
    {
        m_saveTexture.SaveStagingTextureAsJpeg(m_pDevice->GetDevice(), m_pDevice->GetGraphicsQueue(), m_pScreenShotName.c_str());
        m_pScreenShotName.clear();
    }

    // Update previous camera matrices
    //
    pState->camera.UpdatePreviousMatrices();
}
