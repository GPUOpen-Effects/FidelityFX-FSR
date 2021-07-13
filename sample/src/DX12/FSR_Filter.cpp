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

void FSR_Filter::OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps, bool slowFallback)
{
	pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_outputTextureUav);
	pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_inputTextureSrv);
	pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_intermediarySrv);
	pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_intermediaryUav);
	
	D3D12_STATIC_SAMPLER_DESC sd = {};
	sd.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sd.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	sd.MaxAnisotropy = 1;
	sd.MaxLOD = D3D12_FLOAT32_MAX;

	DefineList defines;
	defines["SAMPLE_SLOW_FALLBACK"] = (slowFallback ? "1" : "0");
	defines["SAMPLE_BILINEAR"] = "0";
	defines["SAMPLE_RCAS"] = "0";
	defines["SAMPLE_EASU"] = "1";
	m_easu.OnCreate(pDevice, pResourceViewHeaps, "FSR_Pass.hlsl", "mainCS", 1, 1, 64, 1, 1, &defines, 1, &sd);
	defines["SAMPLE_EASU"] = "0";
	defines["SAMPLE_RCAS"] = "1";
	m_rcas.OnCreate(pDevice, pResourceViewHeaps, "FSR_Pass.hlsl", "mainCS", 1, 1, 64, 1, 1, &defines, 1, &sd);
	defines["SAMPLE_RCAS"] = "0";
	defines["SAMPLE_BILINEAR"] = "1";
	m_bilinear.OnCreate(pDevice, pResourceViewHeaps, "FSR_Pass.hlsl", "mainCS", 1, 1, 64, 1, 1, &defines, 1, &sd);
	defines["SAMPLE_BILINEAR"] = "0";
}

void FSR_Filter::OnCreateWindowSizeDependentResources(Device* pDevice, ID3D12Resource* input, ID3D12Resource* output, int displayWidth, int displayHeight, State* pState, bool hdr)
{
	DXGI_FORMAT fmt = (hdr ? DXGI_FORMAT_R10G10B10A2_UNORM: DXGI_FORMAT_R8G8B8A8_UNORM );
	m_intermediary.InitRenderTarget(pDevice, "FSR Intermediary", &CD3DX12_RESOURCE_DESC::Tex2D(fmt, displayWidth, displayHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = output->GetDesc().Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	pDevice->GetDevice()->CreateUnorderedAccessView(output, 0, &uavDesc, m_outputTextureUav.GetCPU(0));
	uavDesc.Format = fmt;
	pDevice->GetDevice()->CreateUnorderedAccessView(m_intermediary.GetResource(), 0, &uavDesc, m_intermediaryUav.GetCPU(0));
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = fmt;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	pDevice->GetDevice()->CreateShaderResourceView(input, &srvDesc, m_inputTextureSrv.GetCPU(0));
	pDevice->GetDevice()->CreateShaderResourceView(m_intermediary.GetResource(), &srvDesc, m_intermediarySrv.GetCPU(0));
}

void FSR_Filter::OnDestroyWindowSizeDependentResources()
{
	m_intermediary.OnDestroy();
}

void FSR_Filter::OnDestroy()
{
	m_easu.OnDestroy();
	m_rcas.OnDestroy();
	m_bilinear.OnDestroy();
}

void FSR_Filter::Upscale(ID3D12GraphicsCommandList* pCommandList, int displayWidth, int displayHeight, State* pState, DynamicBufferRing* pConstantBufferRing, bool hdr)
{
	D3D12_GPU_VIRTUAL_ADDRESS cbHandle = {};
	{
		FSRConstants consts = {};
		FsrEasuCon(reinterpret_cast<AU1*>(&consts.Const0), reinterpret_cast<AU1*>(&consts.Const1), reinterpret_cast<AU1*>(&consts.Const2), reinterpret_cast<AU1*>(&consts.Const3), static_cast<AF1>(pState->renderWidth), static_cast<AF1>(pState->renderHeight), static_cast<AF1>(pState->renderWidth), static_cast<AF1>(pState->renderHeight), (AF1)displayWidth, (AF1)displayHeight);
		consts.Sample.x = (hdr && !pState->bUseRcas) ? 1 : 0;
		uint32_t* pConstMem = 0;
		pConstantBufferRing->AllocConstantBuffer(sizeof(FSRConstants), (void**)&pConstMem, &cbHandle);
		memcpy(pConstMem, &consts, sizeof(FSRConstants));
	}
	// This value is the image region dimension that each thread group of the FSR shader operates on
	static const int threadGroupWorkRegionDim = 16;
	int dispatchX = (displayWidth + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
	int dispatchY = (displayHeight + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
	if (pState->m_nUpscaleType)
	{
		UserMarker marker(pCommandList, "FSR upscaling");
		if (pState->bUseRcas)
		{
			m_easu.Draw(pCommandList, cbHandle, &m_intermediaryUav, &m_inputTextureSrv, dispatchX, dispatchY, 1);
			{
				FSRConstants consts = {};
				FsrRcasCon(reinterpret_cast<AU1*>(&consts.Const0), pState->rcasAttenuation);
				consts.Sample.x = (hdr ? 1 : 0);
				uint32_t* pConstMem = 0;
				pConstantBufferRing->AllocConstantBuffer(sizeof(FSRConstants), (void**)&pConstMem, &cbHandle);
				memcpy(pConstMem, &consts, sizeof(FSRConstants));
			}
			pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_intermediary.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			m_rcas.Draw(pCommandList, cbHandle, &m_outputTextureUav, &m_intermediarySrv, dispatchX, dispatchY, 1);
			pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_intermediary.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		}
		else
			m_easu.Draw(pCommandList, cbHandle, &m_outputTextureUav, &m_inputTextureSrv, dispatchX, dispatchY, 1);
	} else
	{
		UserMarker marker(pCommandList, "Bilinear upscaling");
		m_bilinear.Draw(pCommandList, cbHandle, &m_outputTextureUav, &m_inputTextureSrv, dispatchX, dispatchY, 1);
	}
}

