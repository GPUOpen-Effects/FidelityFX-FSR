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
#include "PostProc/PostProcCS.h"

struct State;

class FSR_Filter
{
public:
	void OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps, DynamicBufferRing* pConstantBufferRing, bool glsl);
	void OnCreateWindowSizeDependentResources(Device* pDevice, VkImage input, VkImage output, VkFormat outputFormat, int displayWidth, int displayHeight, State* pState, bool hdr);
	void OnDestroyWindowSizeDependentResources();
	void OnDestroy();
	void Upscale(VkCommandBuffer cmd_buf, int displayWidth, int displayHeight, State *pState, DynamicBufferRing* pConstantBufferRing, bool hdr);

private:
	Device							*m_pDevice = 0;
	ResourceViewHeaps				*m_pResourceViewHeaps = 0;
	PostProcCS                      m_easu;
	PostProcCS                      m_rcas;
	PostProcCS                      m_bilinear;
	VkImageView                     m_outputTextureUav;
	VkImageView                     m_inputTextureSrv;
	Texture							m_intermediary;
	VkImageView                     m_intermediaryUav;
	VkSampler						m_sampler;
	VkDescriptorSet					m_easuDescriptorSet;
	VkDescriptorSet					m_rcasDescriptorSet;
	VkDescriptorSetLayout			m_descriptorSetLayout;
};
