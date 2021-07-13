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

using namespace CAULDRON_DX12;

class FSRToneMapping : public ToneMapping
{
	public:
		void Draw(ID3D12GraphicsCommandList* pCommandList, CBV_SRV_UAV* pHDRSRV, bool hdr, State *pState)
		{
            UserMarker marker(pCommandList, "Tonemapping");

            D3D12_GPU_VIRTUAL_ADDRESS cbTonemappingHandle;
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
            m_toneMapping.Draw(pCommandList, 1, pHDRSRV, cbTonemappingHandle);
		}

    protected:
        struct FSRToneMappingConsts { float exposure; int toneMapper; int width; int height; int hdr; int frame; };
};
