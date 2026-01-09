#pragma once

#include <stdint.h>

struct ID3D11Device;
struct ID3D11Texture2D;

extern "C" {
    __declspec(dllexport) void* NvencCreate(
        ID3D11Device* device,
        int width,
        int height,
        int fps,
        int bitrateKbps,
        int codec,
        int bufferFormat,
        const wchar_t* outputPath);

    __declspec(dllexport) int NvencEncode(void* handle, ID3D11Texture2D* texture);

    __declspec(dllexport) int NvencWriteAudio(void* handle, const float* samples, int sampleCount, int sampleRate, int channels);

    __declspec(dllexport) int NvencFinalize(void* handle);

    __declspec(dllexport) void NvencDestroy(void* handle);

    __declspec(dllexport) const wchar_t* NvencGetLastError(void* handle);
}
