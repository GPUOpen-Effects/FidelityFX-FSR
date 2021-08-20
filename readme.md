# FidelityFX Super Resolution 1.0 (FSR) 

Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Super Resolution (FSR)

![Screenshot](screenshot.png)

AMD FidelityFX Super Resolution (FSR) is an open source, high-quality solution for producing high resolution frames from lower resolution inputs.

It uses a collection of cutting-edge algorithms with a particular emphasis on creating high-quality edges, giving large performance improvements compared to rendering at native resolution directly. FSR enables “practical performance” for costly render operations, such as hardware ray tracing.

- ffx-fsr contains the [FSR shader code](https://github.com/GPUOpen-Effects/FidelityFX-FSR/tree/master/ffx-fsr)
- sample contains the [FSR sample](https://github.com/GPUOpen-Effects/FidelityFX-FSR/tree/master/sample)

You can find the binaries for FidelityFX FSR in the release section on GitHub. 

# Build Instructions

### Prerequisites

To build the FSR sample, please follow the following instructions:

1) Install the following tools:

- [CMake 3.16](https://cmake.org/download/)
- Install the "Desktop Development with C++" workload
- [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
- [Windows 10 SDK 10.0.18362.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)
- [Git 2.32.0](https://git-scm.com/downloads)

2) Generate the solutions:
    ```
    > cd <installation path>\build
    > GenerateSolutions.bat
    ```

3) Open the solutions in the DX12 or Vulkan directory (depending on your preference), compile and run.

