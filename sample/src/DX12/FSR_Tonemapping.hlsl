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

#include "tonemappers.hlsl"


#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
#include "ffx_fsr1.h"

//--------------------------------------------------------------------------------------
// Constant Buffer
//--------------------------------------------------------------------------------------
cbuffer cbPerFrame : register(b0)
{
    float u_exposure : packoffset(c0.x);
    int   u_toneMapper : packoffset(c0.y);
    int   u_width : packoffset(c0.z);
    int   u_height : packoffset(c0.w);
    int   u_hdr : packoffset(c1.x);
    int   u_frame : packoffset(c1.y);
}

//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------
struct VERTEX
{
    float2 vTexcoord : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
Texture2D        HDR              :register(t0);
Texture2D        BlueNoise              :register(t1);
SamplerState     samLinearWrap    :register(s0);

float3 Tonemap(float3 color, float exposure, int tonemapper)
{
    color *= exposure;

    switch (tonemapper)
    {
    case 0: return AMDTonemapper(color);
    case 1: return DX11DSK(color);
    case 2: return Reinhard(color);
    case 3: return Uncharted2Tonemap(color);
    case 4: return ACESFilm(color);
    case 5: return color;
    default: return float3(1, 1, 1);
    }
}

//--------------------------------------------------------------------------------------
// Main function
//--------------------------------------------------------------------------------------

float4 mainPS(VERTEX Input) : SV_Target
{
    if (u_exposure < 0)
    {
        return HDR.Sample(samLinearWrap, Input.vTexcoord);
    }

    float4 texColor = HDR.Sample(samLinearWrap, Input.vTexcoord);

    float3 color = Tonemap(texColor.rgb, u_exposure, u_toneMapper);
    if (u_hdr == 1)
        FsrTepdC10F(color, saturate(BlueNoise.Load(int3(int(Input.vTexcoord.x * u_width - 0.3) % 128, int(Input.vTexcoord.y * u_height - 0.3) % 128 + u_frame * 128, 0)).w));
    return float4(color, 1);
}

