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

using namespace CAULDRON_VK;

class FSRToneMapping : public ToneMapping
{
	public:
		void Draw(VkCommandBuffer cmd_buf, VkImageView HDRSRV, VkImageView blueNoiseSRV, bool hdr, State *pState)
		{
            SetPerfMarkerBegin(cmd_buf, "tonemapping");

            VkDescriptorBufferInfo cbTonemappingHandle;
            FSRToneMappingConsts* pToneMapping;
            m_pDynamicBufferRing->AllocConstantBuffer(sizeof(FSRToneMappingConsts), (void**)&pToneMapping, &cbTonemappingHandle);
            pToneMapping->exposure = pState->exposure;
            pToneMapping->toneMapper = pState->toneMapper;
            pToneMapping->hdr = (hdr ? 1 : 0);
            pToneMapping->width = pState->renderWidth;
            pToneMapping->height = pState->renderHeight;
            static int frame = 0;
            frame = (frame + 1) % 8;
            pToneMapping->frame = frame;
            // We'll be modifying the descriptor set(DS), to prevent writing on a DS that is in use we 
            // need to do some basic buffering. Just to keep it safe and simple we'll have 10 buffers.
            VkDescriptorSet descriptorSet = m_descriptorSet[m_descriptorIndex];
            m_descriptorIndex = (m_descriptorIndex + 1) % s_descriptorBuffers;

            // modify Descriptor set
            SetDescriptorSet(m_pDevice->GetDevice(), 1, 2, {HDRSRV, blueNoiseSRV}, &m_sampler, descriptorSet);
            m_pDynamicBufferRing->SetDescriptorSet(0, sizeof(FSRToneMappingConsts), descriptorSet);

            // Draw!
            m_toneMapping.Draw(cmd_buf, &cbTonemappingHandle, descriptorSet);

            SetPerfMarkerEnd(cmd_buf);
		}

    protected:
        struct FSRToneMappingConsts { float exposure; int toneMapper; int width; int height; int hdr; int frame; };
};
