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

#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

#include "tonemappers.glsl"

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#include "ffx_fsr1.h"

layout (location = 0) in vec2 inTexCoord;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform perBatch 
{
    float u_exposure; 
    int u_toneMapper; 
    int u_width;
    int u_height;
    int u_hdr;
    int u_frame;
} myPerScene;

layout(set=0, binding=1) uniform sampler2D sSampler[2]; //0 - HDR color input; 1 - temporal blue noise

vec3 Tonemap(vec3 color, float exposure, int tonemapper)
{
    color *= exposure;

    switch (tonemapper)
    {
        case 0: return AMDTonemapper(color);
        case 1: return DX11DSK(color);
        case 2: return Reinhard(color);
        case 3: return Uncharted2Tonemap(color);
        case 4: return tonemapACES( color );
        case 5: return color;
        default: return vec3(1, 1, 1);
    } 
}

void main() 
{
    if (myPerScene.u_exposure<0)
    {
        outColor = texture(sSampler[0], inTexCoord.st);
        return;
    }

    vec4 texColor = texture(sSampler[0], inTexCoord.st);

    vec3 color = Tonemap(texColor.rgb, myPerScene.u_exposure, myPerScene.u_toneMapper);
    if (myPerScene.u_hdr == 1) //HDR
        FsrTepdC10F(color, clamp(texelFetch(sSampler[1], ASU2(int(inTexCoord.x * myPerScene.u_width - 0.3) % 128, int(inTexCoord.y * myPerScene.u_height - 0.3) % 128 + myPerScene.u_frame * 128), 0).x, 0.0, 1.0));
    outColor = vec4(color,1.0);
}
