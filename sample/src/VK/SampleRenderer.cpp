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

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreate(Device *pDevice, SwapChain *pSwapChain, float fontSize, bool glsl)
{
    m_pDevice = pDevice;

    // Initialize helpers

    // Create all the heaps for the resources views
    const uint32_t cbvDescriptorCount = 2000;
    const uint32_t srvDescriptorCount = 8000;
    const uint32_t uavDescriptorCount = 1000;
    const uint32_t samplerDescriptorCount = 20;
    m_resourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, samplerDescriptorCount);

    // Create a commandlist ring for the Direct queue
    uint32_t commandListsPerBackBuffer = 8;
    m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer);

    // Create a 'dynamic' constant buffer
    const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
    m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, "Uniforms");

    // Create a 'static' pool for vertices and indices 
    const uint32_t staticGeometryMemSize = (1 * 128) * 1024 * 1024;
    m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, true, "StaticGeom");

    // Create a 'static' pool for vertices and indices in system memory
    const uint32_t systemGeometryMemSize = 32 * 1024;
    m_SysMemBufferPool.OnCreate(pDevice, systemGeometryMemSize, false, "PostProcGeom");

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
                { GBUFFER_DEPTH, VK_FORMAT_D32_SFLOAT},
                { GBUFFER_FORWARD, VK_FORMAT_R16G16B16A16_SFLOAT},
                { GBUFFER_MOTION_VECTORS, VK_FORMAT_R16G16_SFLOAT},
            },
            1
        );

        GBufferFlags fullGBuffer = GBUFFER_DEPTH | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS;
        bool bClear = true;
        m_renderPassFullGBufferWithClear.OnCreate(&m_GBuffer, fullGBuffer, bClear,"m_renderPassFullGBufferWithClear");
        m_renderPassFullGBuffer.OnCreate(&m_GBuffer, fullGBuffer, !bClear, "m_renderPassFullGBuffer");
        m_renderPassJustDepthAndHdr.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD, !bClear, "m_renderPassJustDepthAndHdr");
    }

    // Create a 2Kx2K Shadowmap atlas to hold 4 cascades/spotlights
    m_shadowMap.InitDepthStencil(m_pDevice, 2 * 1024, 2 * 1024, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, "ShadowMap");
    m_shadowMap.CreateSRV(&m_shadowMapSRV);
    m_shadowMap.CreateDSV(&m_shadowMapDSV);

    // Create render pass shadow, will clear contents
    //
    {
        VkAttachmentDescription depthAttachments;
        AttachClearBeforeUse(m_shadowMap.GetFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachments);
        m_render_pass_shadow = CreateRenderPassOptimal(m_pDevice->GetDevice(), 0, NULL, &depthAttachments);

        // Create frame buffer, its size is now window dependant so we can do this here.
        //
        VkImageView attachmentViews[1] = { m_shadowMapDSV };
        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.pNext = NULL;
        fb_info.renderPass = m_render_pass_shadow;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = attachmentViews;
        fb_info.width = m_shadowMap.GetWidth();
        fb_info.height = m_shadowMap.GetHeight();
        fb_info.layers = 1;
        VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_pFrameBuffer_shadow);
        assert(res == VK_SUCCESS);
    }

    m_skyDome.OnCreate(pDevice, m_renderPassJustDepthAndHdr.GetRenderPass(), &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\cauldron-media\\envmaps\\papermill\\diffuse.dds", "..\\media\\cauldron-media\\envmaps\\papermill\\specular.dds", VK_SAMPLE_COUNT_1_BIT);
    m_skyDomeProc.OnCreate(pDevice, m_renderPassJustDepthAndHdr.GetRenderPass(), &m_UploadHeap, VK_FORMAT_R16G16B16A16_SFLOAT, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_1_BIT);
    m_wireframe.OnCreate(pDevice, m_renderPassJustDepthAndHdr.GetRenderPass(), &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_SAMPLE_COUNT_1_BIT);
    m_wireframeBox.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
    m_downSample.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_bloom.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_TAA.OnCreate(pDevice, &m_resourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing, false);
	m_FSR.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, glsl);
    m_magnifierPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, VK_FORMAT_R16G16B16A16_SFLOAT);

    // Create tonemapping pass
    m_toneMappingCS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing);
    m_toneMappingPS.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_resourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing, 2, "FSR_Tonemapping.glsl");
    m_colorConversionPS.OnCreate(pDevice, pSwapChain->GetRenderPass(), &m_resourceViewHeaps, &m_VidMemBufferPool, &m_ConstantBufferRing);

    // Initialize UI rendering resources
    m_ImGUI.OnCreate(m_pDevice, pSwapChain->GetRenderPass(), &m_UploadHeap, &m_ConstantBufferRing, fontSize);
    m_blueNoise.InitFromFile(pDevice, &m_UploadHeap, "..\\media\\cauldron-media\\noise\\temporal_blue_noise.dds");
    m_blueNoise.CreateSRV(&m_blueNoiseSRV);

    // Make sure upload heap has finished uploading before continuing
    m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
    m_UploadHeap.FlushAndFinish();
}

//--------------------------------------------------------------------------------------
//
// OnDestroy 
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroy()
{   
    m_asyncPool.Flush();

    m_ImGUI.OnDestroy();
    m_colorConversionPS.OnDestroy();
    m_toneMappingPS.OnDestroy();
    m_toneMappingCS.OnDestroy();
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

    m_renderPassFullGBufferWithClear.OnDestroy();
    m_renderPassJustDepthAndHdr.OnDestroy();
    m_renderPassFullGBuffer.OnDestroy();
    m_GBuffer.OnDestroy();

    vkDestroyImageView(m_pDevice->GetDevice(), m_shadowMapDSV, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_shadowMapSRV, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_blueNoiseSRV, nullptr);
    
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_render_pass_shadow, nullptr);
    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_pFrameBuffer_shadow, nullptr);
    m_blueNoise.OnDestroy();
    m_UploadHeap.OnDestroy();
    m_GPUTimer.OnDestroy();
    m_VidMemBufferPool.OnDestroy();
    m_SysMemBufferPool.OnDestroy();
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
    // Create GBuffer
    //
    m_GBuffer.OnCreateWindowSizeDependentResources(pSwapChain, pState->renderWidth, pState->renderHeight);

    // Create frame buffers for the GBuffer render passes
    //
    m_renderPassFullGBufferWithClear.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight);
    m_renderPassJustDepthAndHdr.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight);
    m_renderPassFullGBuffer.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight);

    // Update PostProcessing passes
    //
    m_downSample.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight, &m_GBuffer.m_HDR, 1);
    m_bloom.OnCreateWindowSizeDependentResources(pState->renderWidth / 2, pState->renderHeight / 2, m_downSample.GetTexture(), 1, &m_GBuffer.m_HDR);
    m_TAA.OnCreateWindowSizeDependentResources(pState->renderWidth, pState->renderHeight, &m_GBuffer);
    
    bool hdr = (pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR);
    VkFormat uiFormat = (hdr ? m_GBuffer.m_HDR.GetFormat() : pSwapChain->GetFormat());
    VkFormat dFormat = (hdr ? uiFormat : VK_FORMAT_R8G8B8A8_UNORM);
    VkFormat rFormat = (hdr ? VK_FORMAT_A2R10G10B10_UNORM_PACK32 : pSwapChain->GetFormat());
    // Update the pipelines if the swapchain render pass has changed (for example when the format of the swapchain changes)
    //
    m_colorConversionPS.UpdatePipelines(pSwapChain->GetRenderPass(), pSwapChain->GetDisplayMode());

	m_renderOutput.InitRenderTarget(m_pDevice, pState->renderWidth, pState->renderHeight, rFormat, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT), false, "RenderOutput", VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
    VkImageView rortv, dortv, mortv;
    m_renderOutput.CreateRTV(&rortv);
    VkAttachmentDescription atd = {};
    atd.format = rFormat;
    atd.samples = VK_SAMPLE_COUNT_1_BIT;
    atd.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atd.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atd.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atd.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atd.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atd.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_renderOutputRenderPass = CreateRenderPassOptimal(m_pDevice->GetDevice(), 1, &atd, 0);
    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = m_renderOutputRenderPass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &rortv;
    fb_info.width = pState->renderWidth;
    fb_info.height = pState->renderHeight;
    fb_info.layers = 1;
    VkResult res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_renderOutputFramebuffer);
    assert(res == VK_SUCCESS);
	m_displayOutput.InitRenderTarget(m_pDevice, displayWidth, displayHeight, dFormat, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT), false, "DisplayOutput", VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
    m_magnifierPS.OnCreateWindowSizeDependentResources(&m_displayOutput);
    if (hdr)
    {
        m_displayOutput.CreateRTV(&dortv);
        m_magnifierPS.GetPassOutput().CreateRTV(&mortv);
    } else
    {
        m_displayOutput.CreateRTV(&dortv, -1, pSwapChain->GetFormat());
        m_magnifierPS.GetPassOutput().CreateRTV(&mortv, -1, pSwapChain->GetFormat());
    }
    m_displayOutput.CreateSRV(&m_displayOutputSRV);
    atd.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    atd.format = uiFormat;
    m_displayOutputRenderPass = CreateRenderPassOptimal(m_pDevice->GetDevice(), 1, &atd, 0);
    fb_info.renderPass = m_displayOutputRenderPass;
    fb_info.pAttachments = &dortv;
    fb_info.width = displayWidth;
    fb_info.height = displayHeight;
    res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_displayOutputFramebuffer);
    assert(res == VK_SUCCESS);
    atd.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    atd.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    m_displayOutputGuiRenderPass = CreateRenderPassOptimal(m_pDevice->GetDevice(), 1, &atd, 0);
    m_ImGUI.UpdatePipeline(m_displayOutputGuiRenderPass);
    m_FSR.OnCreateWindowSizeDependentResources(m_pDevice, m_renderOutput.Resource(), m_displayOutput.Resource(), dFormat, displayWidth, displayHeight, pState, hdr);
    if( pState->m_nUpscaleType == 2 )
        m_toneMappingPS.UpdatePipelines(m_displayOutputRenderPass);
    else
        m_toneMappingPS.UpdatePipelines(m_renderOutputRenderPass);
    atd.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atd.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_magnifierRenderPass = CreateRenderPassOptimal(m_pDevice->GetDevice(), 1, &atd, 0);
    fb_info.renderPass = m_magnifierRenderPass;
    fb_info.pAttachments = &mortv;
    res = vkCreateFramebuffer(m_pDevice->GetDevice(), &fb_info, NULL, &m_magnifierFramebuffer);
    assert(res == VK_SUCCESS);
    m_magnifierPS.UpdatePipelines(m_magnifierRenderPass);
}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroyWindowSizeDependentResources()
{
    m_bloom.OnDestroyWindowSizeDependentResources();
    m_downSample.OnDestroyWindowSizeDependentResources();
    m_TAA.OnDestroyWindowSizeDependentResources();
    m_FSR.OnDestroyWindowSizeDependentResources();
    m_magnifierPS.OnDestroyWindowSizeDependentResources();

    m_renderPassFullGBufferWithClear.OnDestroyWindowSizeDependentResources();
    m_renderPassJustDepthAndHdr.OnDestroyWindowSizeDependentResources();
    m_renderPassFullGBuffer.OnDestroyWindowSizeDependentResources();
    m_GBuffer.OnDestroyWindowSizeDependentResources();
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_renderOutputRenderPass, nullptr);
    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_renderOutputFramebuffer, nullptr);
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_displayOutputRenderPass, nullptr);
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_displayOutputGuiRenderPass, nullptr);
    vkDestroyRenderPass(m_pDevice->GetDevice(), m_magnifierRenderPass, nullptr);
    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_displayOutputFramebuffer, nullptr);
    vkDestroyFramebuffer(m_pDevice->GetDevice(), m_magnifierFramebuffer, nullptr);
    vkDestroyImageView(m_pDevice->GetDevice(), m_displayOutputSRV, nullptr);
    m_displayOutput.OnDestroy();
    m_renderOutput.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnUpdateLocalDimmingChangedResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnUpdateLocalDimmingChangedResources(SwapChain *pSwapChain, State *pState)
{
    m_colorConversionPS.UpdatePipelines(pSwapChain->GetRenderPass(), pSwapChain->GetDisplayMode());
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
        float progress = (float)stage / 12.0f;
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
            m_render_pass_shadow,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            pAsyncPool
        );

        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
        m_UploadHeap.FlushAndFinish();
    }
    else if (stage == 8)
    {
        Profile p("m_gltfPBR->OnCreate");

        // same thing as above but for the PBR pass
        m_gltfPBR = new GltfPbrPass();
        m_gltfPBR->OnCreate(
            m_pDevice,
            &m_UploadHeap,
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            &m_skyDome,
            false, // use SSAO mask
            std::vector<VkImageView>({ m_shadowMapSRV }),
            &m_renderPassFullGBufferWithClear,
            pAsyncPool
        );

        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
        m_UploadHeap.FlushAndFinish();
    }
    else if (stage == 9)
    {
        Profile p("m_gltfBBox->OnCreate");

        // just a bounding box pass that will draw boundingboxes instead of the geometry itself
        m_gltfBBox = new GltfBBoxPass();
            m_gltfBBox->OnCreate(
            m_pDevice,
            m_renderPassJustDepthAndHdr.GetRenderPass(),
            &m_resourceViewHeaps,
            &m_ConstantBufferRing,
            &m_VidMemBufferPool,
            m_pGLTFTexturesAndBuffers,
            &m_wireframe
        );

        // we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());

    }
    else if (stage == 10)
    {
        Profile p("Flush");

        m_UploadHeap.FlushAndFinish();

        //once everything is uploaded we dont need the upload heaps anymore
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
    // Let our resource managers do some house keeping 
    //
    m_ConstantBufferRing.OnBeginFrame();

    // command buffer calls
    //    
    VkCommandBuffer cmdBuf1 = m_CommandListRing.GetNewCommandList();

    {
        VkCommandBufferBeginInfo cmd_buf_info;
        cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmd_buf_info.pNext = NULL;
        cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cmd_buf_info.pInheritanceInfo = NULL;
        VkResult res = vkBeginCommandBuffer(cmdBuf1, &cmd_buf_info);
        assert(res == VK_SUCCESS);
    }

    m_GPUTimer.OnBeginFrame(cmdBuf1, &m_TimeStamps);

    if (pState->bUseTAA)
    {
        static uint32_t Seed;
        pState->camera.SetProjectionJitter(pState->renderWidth, pState->renderHeight, Seed);
    } else
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
    bool renderNative = (pState->m_nUpscaleType == 2);
	{
		VkImageMemoryBarrier barrier[3] = {};
		barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier[0].subresourceRange.baseMipLevel = 0;
		barrier[0].subresourceRange.levelCount = 1;
		barrier[0].subresourceRange.baseArrayLayer = 0;
		barrier[0].subresourceRange.layerCount = 1;
		barrier[0].image = m_renderOutput.Resource();
		barrier[1] = barrier[0];
		barrier[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier[1].image = m_displayOutput.Resource();
		barrier[2] = barrier[0];
		barrier[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier[2].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier[2].image = pSwapChain->GetCurrentBackBuffer();
		vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 3, barrier);
	}

    // Render spot lights shadow map atlas  ------------------------------------------
    //
    if (m_gltfDepth && pPerFrame != NULL)
    {
        SetPerfMarkerBegin(cmdBuf1, "ShadowPass");

        VkClearValue depth_clear_values[1];
        depth_clear_values[0].depthStencil.depth = 1.0f;
        depth_clear_values[0].depthStencil.stencil = 0;

        {
            VkRenderPassBeginInfo rp_begin;
            rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin.pNext = NULL;
            rp_begin.renderPass = m_render_pass_shadow;
            rp_begin.framebuffer = m_pFrameBuffer_shadow;
            rp_begin.renderArea.offset.x = 0;
            rp_begin.renderArea.offset.y = 0;
            rp_begin.renderArea.extent.width = m_shadowMap.GetWidth();
            rp_begin.renderArea.extent.height = m_shadowMap.GetHeight();
            rp_begin.clearValueCount = 1;
            rp_begin.pClearValues = depth_clear_values;

            vkCmdBeginRenderPass(cmdBuf1, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            m_GPUTimer.GetTimeStamp(cmdBuf1, "Clear Shadow Map");
        }

        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if (!(pPerFrame->lights[i].type == LightType_Spot || pPerFrame->lights[i].type == LightType_Directional))
                continue;

            // Set the RT's quadrant where to render the shadomap (these viewport offsets need to match the ones in shadowFiltering.h)
            uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
            uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
            uint32_t viewportWidth = m_shadowMap.GetWidth() / 2;
            uint32_t viewportHeight = m_shadowMap.GetHeight() / 2;
            SetViewportAndScissor(cmdBuf1, viewportOffsetsX[shadowMapIndex] * viewportWidth, viewportOffsetsY[shadowMapIndex] * viewportHeight, viewportWidth, viewportHeight);

            //set per frame constant buffer values
            GltfDepthPass::per_frame *cbPerFrame = m_gltfDepth->SetPerFrameConstants();
            cbPerFrame->mViewProj = pPerFrame->lights[i].mLightViewProj;

            m_gltfDepth->Draw(cmdBuf1);

            m_GPUTimer.GetTimeStamp(cmdBuf1, "Shadow maps");
            shadowMapIndex++;
        }
        vkCmdEndRenderPass(cmdBuf1);
        
        SetPerfMarkerEnd(cmdBuf1);
    }

    // Render Scene to the GBuffer ------------------------------------------------
    //
    SetPerfMarkerBegin(cmdBuf1, "Color pass");

    VkRect2D renderArea = { 0, 0, pState->renderWidth, pState->renderHeight };

    if (pPerFrame != NULL && m_gltfPBR)
    {
        std::vector<GltfPbrPass::BatchList> opaque, transparent;
        m_gltfPBR->BuildBatchLists(&opaque, &transparent);

        // Render opaque 
        //
        {
            m_renderPassFullGBufferWithClear.BeginPass(cmdBuf1, renderArea);

            m_gltfPBR->DrawBatchList(cmdBuf1, &opaque);
            m_GPUTimer.GetTimeStamp(cmdBuf1, "PBR Opaque");

            m_renderPassFullGBufferWithClear.EndPass(cmdBuf1);
        }

        // Render skydome
        //
        {
            m_renderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);

            if (pState->skyDomeType == 1)
            {
                math::Matrix4 clipToView = math::inverse(pPerFrame->mCameraCurrViewProj);
                m_skyDome.Draw(cmdBuf1, clipToView);

                m_GPUTimer.GetTimeStamp(cmdBuf1, "Skydome cube");
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
                m_skyDomeProc.Draw(cmdBuf1, skyDomeConstants);

                m_GPUTimer.GetTimeStamp(cmdBuf1, "Skydome Proc");
            }

            m_renderPassJustDepthAndHdr.EndPass(cmdBuf1);
        }

        // draw transparent geometry
        //
        {
            m_renderPassFullGBuffer.BeginPass(cmdBuf1, renderArea);

            std::sort(transparent.begin(), transparent.end());
            m_gltfPBR->DrawBatchList(cmdBuf1, &transparent);
            m_GPUTimer.GetTimeStamp(cmdBuf1, "PBR Transparent");

            //m_GBuffer.EndPass(cmdBuf1);
            m_renderPassFullGBuffer.EndPass(cmdBuf1);
        }

        // draw object's bounding boxes
        //
        {
            m_renderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);

            if (m_gltfBBox)
            {
                if (pState->bDrawBoundingBoxes)
                {
                    m_gltfBBox->Draw(cmdBuf1, pPerFrame->mCameraCurrViewProj);

                    m_GPUTimer.GetTimeStamp(cmdBuf1, "Bounding Box");
                }
            }

            // draw light's frustums
            //
            if (pState->bDrawLightFrustum && pPerFrame != NULL)
            {
                SetPerfMarkerBegin(cmdBuf1, "light frustums");

                math::Vector4 vCenter = math::Vector4(0.0f, 0.0f, 0.5f, 0.0f);
                math::Vector4 vRadius = math::Vector4(1.0f, 1.0f, 0.5f, 0.0f);
                math::Vector4 vColor = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
                {
                    math::Matrix4 spotlightMatrix = math::inverse(pPerFrame->lights[i].mLightViewProj);
                    math::Matrix4 worldMatrix = pPerFrame->mCameraCurrViewProj * spotlightMatrix;
                    m_wireframeBox.Draw(cmdBuf1, &m_wireframe, worldMatrix, vCenter, vRadius, vColor);
                }

                m_GPUTimer.GetTimeStamp(cmdBuf1, "Light's frustum");

                SetPerfMarkerEnd(cmdBuf1);
            }

            m_renderPassJustDepthAndHdr.EndPass(cmdBuf1);
        }
    }
    else
    {
        m_renderPassFullGBufferWithClear.BeginPass(cmdBuf1, renderArea);
        m_renderPassFullGBufferWithClear.EndPass(cmdBuf1);
        m_renderPassJustDepthAndHdr.BeginPass(cmdBuf1, renderArea);
        m_renderPassJustDepthAndHdr.EndPass(cmdBuf1);
    }

    VkImageMemoryBarrier barrier[1] = {};
    barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[0].pNext = NULL;
    barrier[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier[0].subresourceRange.baseMipLevel = 0;
    barrier[0].subresourceRange.levelCount = 1;
    barrier[0].subresourceRange.baseArrayLayer = 0;
    barrier[0].subresourceRange.layerCount = 1;
    barrier[0].image = m_GBuffer.m_HDR.Resource();
    vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, barrier);

    SetPerfMarkerEnd(cmdBuf1);

    // Post proc---------------------------------------------------------------------------
    //

    // Bloom, takes HDR as input and applies bloom to it.
    //
    {
        SetPerfMarkerBegin(cmdBuf1, "PostProcess");
        
        // Downsample pass
        m_downSample.Draw(cmdBuf1);
        m_GPUTimer.GetTimeStamp(cmdBuf1, "Downsample");

        // Bloom pass (needs the downsampled data)
        m_bloom.Draw(cmdBuf1);
        m_GPUTimer.GetTimeStamp(cmdBuf1, "Bloom");

        SetPerfMarkerEnd(cmdBuf1);
    }

    // Apply TAA & Sharpen to m_HDR
    //
    if (pState->bUseTAA)
    {
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.pNext = NULL;
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.image = m_GBuffer.m_DepthBuffer.Resource();
            
            VkImageMemoryBarrier barriers[2];
            barriers[0] = barrier;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            barriers[0].image = m_GBuffer.m_DepthBuffer.Resource();

            barriers[1] = barrier;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[1].image = m_GBuffer.m_MotionVectors.Resource();

            vkCmdPipelineBarrier(cmdBuf1, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
        }

        m_TAA.Draw(cmdBuf1);
        m_GPUTimer.GetTimeStamp(cmdBuf1, "TAA");
    }

	VkRect2D rsd = {};
	rsd.extent.width = displayWidth;
	rsd.extent.height = displayHeight;
	VkViewport vpd = {};
	vpd.y = (float)displayHeight;
	vpd.width = (float)displayWidth;
	vpd.height = -(float)(displayHeight);
	vpd.maxDepth = (float)1.0f;
    VkRect2D rsr = {};
    rsr.extent.width = pState->renderWidth;
    rsr.extent.height = pState->renderHeight;
    VkViewport vpr = {};
    vpr.y = (float)pState->renderHeight;
    vpr.width = (float)pState->renderWidth;
    vpr.height = -(float)(pState->renderHeight);
    vpr.maxDepth = (float)1.0f;

    // submit command buffer
    {
        VkResult res = vkEndCommandBuffer(cmdBuf1);
        assert(res == VK_SUCCESS);

        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pWaitSemaphores = NULL;
        submit_info.pWaitDstStageMask = NULL;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmdBuf1;
        submit_info.signalSemaphoreCount = 0;
        submit_info.pSignalSemaphores = NULL;
        res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
        assert(res == VK_SUCCESS);
    }

    // Wait for swapchain (we are going to render to it) -----------------------------------
    //
    int imageIndex = pSwapChain->WaitForSwapChain();

    m_CommandListRing.OnBeginFrame();

    VkCommandBuffer cmdBuf2 = m_CommandListRing.GetNewCommandList();

    {
        VkCommandBufferBeginInfo cmd_buf_info;
        cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmd_buf_info.pNext = NULL;
        cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cmd_buf_info.pInheritanceInfo = NULL;
        VkResult res = vkBeginCommandBuffer(cmdBuf2, &cmd_buf_info);
        assert(res == VK_SUCCESS);
    }

    SetPerfMarkerBegin(cmdBuf2, "Swapchain RenderPass");
    // prepare render pass
    {
        VkRenderPassBeginInfo rp_begin = {};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.pNext = NULL;
        if (renderNative)
        {
            rp_begin.renderPass = m_displayOutputRenderPass;
            rp_begin.framebuffer = m_displayOutputFramebuffer;
            rp_begin.renderArea.extent.width = displayWidth;
            rp_begin.renderArea.extent.height = displayHeight;
            vkCmdSetScissor(cmdBuf2, 0, 1, &rsd);
            vkCmdSetViewport(cmdBuf2, 0, 1, &vpd);
        } else
        {
            rp_begin.renderPass = m_renderOutputRenderPass;
            rp_begin.framebuffer = m_renderOutputFramebuffer;
            rp_begin.renderArea.extent.width = pState->renderWidth;
            rp_begin.renderArea.extent.height = pState->renderHeight;
            vkCmdSetScissor(cmdBuf2, 0, 1, &rsr);
            vkCmdSetViewport(cmdBuf2, 0, 1, &vpr);
        }
        rp_begin.renderArea.offset.x = 0;
        rp_begin.renderArea.offset.y = 0;
        rp_begin.clearValueCount = 1;
        VkClearValue cv = { {0.0f, 0.0f, 0.0f, 0.0f}};
        rp_begin.pClearValues = &cv;
        vkCmdBeginRenderPass(cmdBuf2, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    }
    bool hdr = (pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR);
    // Tonemapping ------------------------------------------------------------------------
    {
        m_toneMappingPS.Draw(cmdBuf2, m_GBuffer.m_HDRSRV, m_blueNoiseSRV, pState->m_nUpscaleType == 1 ? hdr : false, pState);
        m_GPUTimer.GetTimeStamp(cmdBuf2, "Tonemapping");
    }
    if (!renderNative)
    {
        vkCmdEndRenderPass(cmdBuf2);
        m_FSR.Upscale(cmdBuf2, displayWidth, displayHeight, pState, &m_ConstantBufferRing, hdr);
        m_GPUTimer.GetTimeStamp(cmdBuf2, pState->m_nUpscaleType == 1 ? "FSR 1.0" : "Upscaling");
    }
    vkCmdSetScissor(cmdBuf2, 0, 1, &rsd);
    vkCmdSetViewport(cmdBuf2, 0, 1, &vpd);
    if (pState->bUseMagnifier)
    {
        if (renderNative)
            vkCmdEndRenderPass(cmdBuf2);
        VkImageMemoryBarrier barrier[1] = {};
        barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier[0].subresourceRange.baseMipLevel = 0;
        barrier[0].subresourceRange.levelCount = 1;
        barrier[0].subresourceRange.baseArrayLayer = 0;
        barrier[0].subresourceRange.layerCount = 1;
        barrier[0].image = m_displayOutput.Resource();
        vkCmdPipelineBarrier(cmdBuf2, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, barrier);
        m_magnifierPS.Draw(cmdBuf2, pState->magnifierParams);
        m_GPUTimer.GetTimeStamp(cmdBuf2, "Magnifier");
        {
            VkRenderPassBeginInfo rp_begin = {};
            rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin.pNext = NULL;
            rp_begin.renderPass = m_magnifierRenderPass;
            rp_begin.framebuffer = m_magnifierFramebuffer;
            rp_begin.renderArea.offset.x = 0;
            rp_begin.renderArea.offset.y = 0;
            rp_begin.renderArea.extent.width = displayWidth;
            rp_begin.renderArea.extent.height = displayHeight;
            rp_begin.clearValueCount = 0;
            rp_begin.pClearValues = NULL;
            vkCmdBeginRenderPass(cmdBuf2, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
            m_ImGUI.Draw(cmdBuf2);
            vkCmdEndRenderPass(cmdBuf2);
        }
        if (!hdr)
        {
            barrier[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier[0].image = m_magnifierPS.GetPassOutputResource();
            vkCmdPipelineBarrier(cmdBuf2, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, barrier);
        }
    } else
    {
        if (!renderNative)
        {
            VkRenderPassBeginInfo rp_begin = {};
            rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin.pNext = NULL;
            rp_begin.renderPass = m_displayOutputGuiRenderPass;
            rp_begin.framebuffer = m_displayOutputFramebuffer;
            rp_begin.renderArea.offset.x = 0;
            rp_begin.renderArea.offset.y = 0;
            rp_begin.renderArea.extent.width = displayWidth;
            rp_begin.renderArea.extent.height = displayHeight;
            rp_begin.clearValueCount = 0;
            rp_begin.pClearValues = NULL;
            vkCmdBeginRenderPass(cmdBuf2, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        }
        m_ImGUI.Draw(cmdBuf2);
        vkCmdEndRenderPass(cmdBuf2);
        VkImageMemoryBarrier barrier[1] = {};
        barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier[0].newLayout = hdr ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier[0].subresourceRange.baseMipLevel = 0;
        barrier[0].subresourceRange.levelCount = 1;
        barrier[0].subresourceRange.baseArrayLayer = 0;
        barrier[0].subresourceRange.layerCount = 1;
        barrier[0].image = m_displayOutput.Resource();
        vkCmdPipelineBarrier(cmdBuf2, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, barrier);
    }
    m_GPUTimer.GetTimeStamp(cmdBuf2, "ImGUI Rendering");
    {
        VkMemoryBarrier barrier[1] = {};
        barrier[0].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmdBuf2, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, barrier, 0, NULL, 0, NULL);
    }
    if (hdr)
    {
        vkCmdSetScissor(cmdBuf2, 0, 1, &rsd);
        vkCmdSetViewport(cmdBuf2, 0, 1, &vpd);
        VkRenderPassBeginInfo rp_begin = {};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.pNext = NULL;
        rp_begin.renderPass = pSwapChain->GetRenderPass();
        rp_begin.framebuffer = pSwapChain->GetFramebuffer(imageIndex);
        rp_begin.renderArea.offset.x = 0;
        rp_begin.renderArea.offset.y = 0;
        rp_begin.renderArea.extent.width = displayWidth;
        rp_begin.renderArea.extent.height = displayHeight;
        rp_begin.clearValueCount = 0;
        rp_begin.pClearValues = NULL;
        vkCmdBeginRenderPass(cmdBuf2, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        m_colorConversionPS.Draw(cmdBuf2, pState->bUseMagnifier ? m_magnifierPS.GetPassOutputSRV() : m_displayOutputSRV);
        vkCmdEndRenderPass(cmdBuf2);
    } else
    {
        VkImageCopy cc = {};
        cc.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cc.srcSubresource.layerCount = 1;
        cc.dstSubresource = cc.srcSubresource;
        cc.extent.width = displayWidth;
        cc.extent.height = displayHeight;
        cc.extent.depth = 1;
        vkCmdCopyImage(cmdBuf2, pState->bUseMagnifier ? m_magnifierPS.GetPassOutputResource() : m_displayOutput.Resource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pSwapChain->GetCurrentBackBuffer(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, & cc);
        VkImageMemoryBarrier barrier[1] = {};
        barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier[0].subresourceRange.baseMipLevel = 0;
        barrier[0].subresourceRange.levelCount = 1;
        barrier[0].subresourceRange.baseArrayLayer = 0;
        barrier[0].subresourceRange.layerCount = 1;
        barrier[0].image = pSwapChain->GetCurrentBackBuffer();
        vkCmdPipelineBarrier(cmdBuf2, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, barrier);
	}

    SetPerfMarkerEnd(cmdBuf2);

    m_GPUTimer.OnEndFrame();

   
    // Close & Submit the command list ----------------------------------------------------
    //
    {
        VkResult res = vkEndCommandBuffer(cmdBuf2);
        assert(res == VK_SUCCESS);

        VkSemaphore ImageAvailableSemaphore;
        VkSemaphore RenderFinishedSemaphores;
        VkFence CmdBufExecutedFences;
        pSwapChain->GetSemaphores(&ImageAvailableSemaphore, &RenderFinishedSemaphores, &CmdBufExecutedFences);

        VkPipelineStageFlags submitWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info2;
        submit_info2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info2.pNext = NULL;
        submit_info2.waitSemaphoreCount = 1;
        submit_info2.pWaitSemaphores = &ImageAvailableSemaphore;
        submit_info2.pWaitDstStageMask = &submitWaitStage;
        submit_info2.commandBufferCount = 1;
        submit_info2.pCommandBuffers = &cmdBuf2;
        submit_info2.signalSemaphoreCount = 1;
        submit_info2.pSignalSemaphores = &RenderFinishedSemaphores;

        res = vkQueueSubmit(m_pDevice->GetGraphicsQueue(), 1, &submit_info2, CmdBufExecutedFences);
        assert(res == VK_SUCCESS);
    }

    // Update previous camera matrices
    //
    pState->camera.UpdatePreviousMatrices();

}
