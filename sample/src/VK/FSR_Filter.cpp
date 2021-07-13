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
#include "FSR_Filter.h"
#include "SampleRenderer.h"

// CAS
#define A_CPU
#include "ffx_a.h"
#include "ffx_fsr1.h"

#include <DirectXMath.h>
using namespace DirectX;

struct FSRConstants
{
	XMUINT4 Const0;
	XMUINT4 Const1;
	XMUINT4 Const2;
	XMUINT4 Const3;
	XMUINT4 Sample;
};

void FSR_Filter::OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps, DynamicBufferRing * pConstantBufferRing, bool glsl)
{
	m_pDevice = pDevice;
	m_pResourceViewHeaps = pResourceViewHeaps;
	{
		VkSamplerCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.minLod = -1000;
		info.maxLod = 1000;
		info.maxAnisotropy = 1.0f;
		VkResult res = vkCreateSampler(pDevice->GetDevice(), &info, NULL, &m_sampler);
		assert(res == VK_SUCCESS);
	}
	{
		std::vector<VkDescriptorSetLayoutBinding> layoutBindings(4);
		layoutBindings[0].binding = 0;
		layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		layoutBindings[0].descriptorCount = 1;
		layoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layoutBindings[0].pImmutableSamplers = NULL;

		layoutBindings[1].binding = 1;
		layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		layoutBindings[1].descriptorCount = 1;
		layoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layoutBindings[1].pImmutableSamplers = NULL;

		layoutBindings[2].binding = 2;
		layoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		layoutBindings[2].descriptorCount = 1;
		layoutBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layoutBindings[2].pImmutableSamplers = NULL;

		layoutBindings[3].binding = 3;
		layoutBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		layoutBindings[3].descriptorCount = 1;
		layoutBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layoutBindings[3].pImmutableSamplers = &m_sampler;

		pResourceViewHeaps->CreateDescriptorSetLayoutAndAllocDescriptorSet(&layoutBindings, &m_descriptorSetLayout, &m_easuDescriptorSet);
		pConstantBufferRing->SetDescriptorSet(0, sizeof(uint32_t) * 16, m_easuDescriptorSet);
		pResourceViewHeaps->AllocDescriptor(m_descriptorSetLayout, &m_rcasDescriptorSet);
		pConstantBufferRing->SetDescriptorSet(0, sizeof(uint32_t) * 16, m_rcasDescriptorSet);
	}

	char *source, *flags;
	if (glsl)
	{
		source = "FSR_Pass.glsl";
		flags = "";
	} else
	{
		source = "FSR_Pass.hlsl";
		flags = "-T cs_6_2 -enable-16bit-types";
	}
	DefineList defines;
	if (pDevice->IsFp16Supported())
		defines["SAMPLE_SLOW_FALLBACK"] = "0";
	else
		defines["SAMPLE_SLOW_FALLBACK"] = "1";
	defines["SAMPLE_BILINEAR"] = "0";
	defines["SAMPLE_RCAS"] = "0";
	defines["SAMPLE_EASU"] = "1";
	m_easu.OnCreate(pDevice, source, "main", flags, m_descriptorSetLayout, 64, 1, 1, &defines);
	defines["SAMPLE_EASU"] = "0";
	defines["SAMPLE_RCAS"] = "1";
	m_rcas.OnCreate(pDevice, source, "main", flags, m_descriptorSetLayout, 64, 1, 1, &defines);
	defines["SAMPLE_RCAS"] = "0";
	defines["SAMPLE_BILINEAR"] = "1";
	m_bilinear.OnCreate(pDevice, source, "main", flags, m_descriptorSetLayout, 64, 1, 1, &defines);
	defines["SAMPLE_BILINEAR"] = "0";
}

void FSR_Filter::OnCreateWindowSizeDependentResources(Device* pDevice, VkImage input, VkImage output, VkFormat outputFormat, int displayWidth, int displayHeight, State* pState, bool hdr)
{
	VkFormat fmt = (hdr ? VK_FORMAT_A2R10G10B10_UNORM_PACK32 : VK_FORMAT_R8G8B8A8_UNORM);
	m_intermediary.InitRenderTarget(pDevice, displayWidth, displayHeight, fmt, VK_SAMPLE_COUNT_1_BIT, (VkImageUsageFlags)(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT), false, "FSR Intermediary");

	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = output;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.subresourceRange.layerCount = 1;
	info.format = outputFormat;
	info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	VkResult res = vkCreateImageView(m_pDevice->GetDevice(), &info, NULL, &m_outputTextureUav);
	assert(res == VK_SUCCESS);
	info.image = m_intermediary.Resource();
	info.format = fmt;
	res = vkCreateImageView(m_pDevice->GetDevice(), &info, NULL, &m_intermediaryUav);
	assert(res == VK_SUCCESS);
	info.image = input;
	res = vkCreateImageView(m_pDevice->GetDevice(), &info, NULL, &m_inputTextureSrv);
	assert(res == VK_SUCCESS);
}

void FSR_Filter::OnDestroyWindowSizeDependentResources()
{
	m_intermediary.OnDestroy();
	vkDestroyImageView(m_pDevice->GetDevice(), m_outputTextureUav, 0);
	vkDestroyImageView(m_pDevice->GetDevice(), m_intermediaryUav, 0);
	vkDestroyImageView(m_pDevice->GetDevice(), m_inputTextureSrv, 0);
}

void FSR_Filter::OnDestroy()
{
	vkDestroySampler(m_pDevice->GetDevice(), m_sampler, nullptr);
	m_easu.OnDestroy();
	m_rcas.OnDestroy();
	m_bilinear.OnDestroy();
	m_pResourceViewHeaps->FreeDescriptor(m_easuDescriptorSet);
	m_pResourceViewHeaps->FreeDescriptor(m_rcasDescriptorSet);
	vkDestroyDescriptorSetLayout(m_pDevice->GetDevice(), m_descriptorSetLayout, NULL);
}

void FSR_Filter::Upscale(VkCommandBuffer cmd_buf, int displayWidth, int displayHeight, State* pState, DynamicBufferRing* pConstantBufferRing, bool hdr)
{
	VkDescriptorBufferInfo constsHandle;
	{
		FSRConstants consts = {};
		FsrEasuCon(reinterpret_cast<AU1*>(&consts.Const0), reinterpret_cast<AU1*>(&consts.Const1), reinterpret_cast<AU1*>(&consts.Const2), reinterpret_cast<AU1*>(&consts.Const3), static_cast<AF1>(pState->renderWidth), static_cast<AF1>(pState->renderHeight), static_cast<AF1>(pState->renderWidth), static_cast<AF1>(pState->renderHeight), (AF1)displayWidth, (AF1)displayHeight);
		consts.Sample.x = (hdr && !pState->bUseRcas) ? 1 : 0;
		uint32_t* pConstMem;
		pConstantBufferRing->AllocConstantBuffer(sizeof(FSRConstants), reinterpret_cast<void**>(&pConstMem), &constsHandle);
		memcpy(pConstMem, &consts, sizeof(FSRConstants));
	}
	// This value is the image region dimension that each thread group of the FSR shader operates on
	static const int threadGroupWorkRegionDim = 16;
	int dispatchX = (displayWidth + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
	int dispatchY = (displayHeight + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
	{
		VkMemoryBarrier barrier[1] = {};
		barrier[0].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
		barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, barrier, 0, NULL, 0, NULL);
	}
	if (pState->m_nUpscaleType)
	{
		SetPerfMarkerBegin(cmd_buf, "FSR upscaling");
		if (pState->bUseRcas)
		{
			{
				VkDescriptorImageInfo ImgInfos[2] = {};
				VkWriteDescriptorSet SetWrites[2] = {};

				ImgInfos[0].sampler = VK_NULL_HANDLE;
				ImgInfos[0].imageView = m_inputTextureSrv;
				ImgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				SetWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				SetWrites[0].dstSet = m_easuDescriptorSet;
				SetWrites[0].dstBinding = 1;
				SetWrites[0].descriptorCount = 1;
				SetWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				SetWrites[0].pImageInfo = ImgInfos + 0;

				// Dst img
				ImgInfos[1].sampler = VK_NULL_HANDLE;
				ImgInfos[1].imageView = m_intermediaryUav;
				ImgInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

				SetWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				SetWrites[1].dstSet = m_easuDescriptorSet;
				SetWrites[1].dstBinding = 2;
				SetWrites[1].descriptorCount = 1;
				SetWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				SetWrites[1].pImageInfo = ImgInfos + 1;

				vkUpdateDescriptorSets(m_pDevice->GetDevice(), _countof(SetWrites), SetWrites, 0, 0);
			}
			m_easu.Draw(cmd_buf, &constsHandle, m_easuDescriptorSet, dispatchX, dispatchY, 1);
			{
				FSRConstants consts = {};
				FsrRcasCon(reinterpret_cast<AU1*>(&consts.Const0), pState->rcasAttenuation);
				consts.Sample.x = (hdr ? 1 : 0);
				uint32_t* pConstMem = 0;
				pConstantBufferRing->AllocConstantBuffer(sizeof(FSRConstants), reinterpret_cast<void**>(&pConstMem), &constsHandle);
				memcpy(pConstMem, &consts, sizeof(FSRConstants));
			}
			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.image = m_intermediary.Resource();
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
			{
				VkDescriptorImageInfo ImgInfos[2] = {};
				VkWriteDescriptorSet SetWrites[2] = {};

				ImgInfos[0].sampler = VK_NULL_HANDLE;
				ImgInfos[0].imageView = m_intermediaryUav;
				ImgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				SetWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				SetWrites[0].dstSet = m_rcasDescriptorSet;
				SetWrites[0].dstBinding = 1;
				SetWrites[0].descriptorCount = 1;
				SetWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				SetWrites[0].pImageInfo = ImgInfos + 0;

				// Dst img
				ImgInfos[1].sampler = VK_NULL_HANDLE;
				ImgInfos[1].imageView = m_outputTextureUav;
				ImgInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

				SetWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				SetWrites[1].dstSet = m_rcasDescriptorSet;
				SetWrites[1].dstBinding = 2;
				SetWrites[1].descriptorCount = 1;
				SetWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				SetWrites[1].pImageInfo = ImgInfos + 1;

				vkUpdateDescriptorSets(m_pDevice->GetDevice(), _countof(SetWrites), SetWrites, 0, 0);
			}
			m_rcas.Draw(cmd_buf, &constsHandle, m_rcasDescriptorSet, dispatchX, dispatchY, 1);
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
		}
		else
		{
			VkDescriptorImageInfo ImgInfos[2] = {};
			VkWriteDescriptorSet SetWrites[2] = {};

			ImgInfos[0].sampler = VK_NULL_HANDLE;
			ImgInfos[0].imageView = m_inputTextureSrv;
			ImgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			SetWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			SetWrites[0].dstSet = m_easuDescriptorSet;
			SetWrites[0].dstBinding = 1;
			SetWrites[0].descriptorCount = 1;
			SetWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			SetWrites[0].pImageInfo = ImgInfos + 0;

			// Dst img
			ImgInfos[1].sampler = VK_NULL_HANDLE;
			ImgInfos[1].imageView = m_outputTextureUav;
			ImgInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			SetWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			SetWrites[1].dstSet = m_easuDescriptorSet;
			SetWrites[1].dstBinding = 2;
			SetWrites[1].descriptorCount = 1;
			SetWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			SetWrites[1].pImageInfo = ImgInfos + 1;

			vkUpdateDescriptorSets(m_pDevice->GetDevice(), _countof(SetWrites), SetWrites, 0, 0);
			m_easu.Draw(cmd_buf, &constsHandle, m_easuDescriptorSet, dispatchX, dispatchY, 1);
		}
		SetPerfMarkerEnd(cmd_buf);
	} else
	{
		SetPerfMarkerBegin(cmd_buf, "Bilinear upscaling");
		VkDescriptorImageInfo ImgInfos[2] = {};
		VkWriteDescriptorSet SetWrites[2] = {};

		ImgInfos[0].sampler = VK_NULL_HANDLE;
		ImgInfos[0].imageView = m_inputTextureSrv;
		ImgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		SetWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		SetWrites[0].dstSet = m_easuDescriptorSet;
		SetWrites[0].dstBinding = 1;
		SetWrites[0].descriptorCount = 1;
		SetWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		SetWrites[0].pImageInfo = ImgInfos + 0;

		// Dst img
		ImgInfos[1].sampler = VK_NULL_HANDLE;
		ImgInfos[1].imageView = m_outputTextureUav;
		ImgInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		SetWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		SetWrites[1].dstSet = m_easuDescriptorSet;
		SetWrites[1].dstBinding = 2;
		SetWrites[1].descriptorCount = 1;
		SetWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		SetWrites[1].pImageInfo = ImgInfos + 1;

		vkUpdateDescriptorSets(m_pDevice->GetDevice(), _countof(SetWrites), SetWrites, 0, 0);
		m_bilinear.Draw(cmd_buf, &constsHandle, m_easuDescriptorSet, dispatchX, dispatchY, 1);
		SetPerfMarkerEnd(cmd_buf);
	}
	{
		VkMemoryBarrier barrier[1] = {};
		barrier[0].sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
		barrier[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, barrier, 0, NULL, 0, NULL);
	}
}

