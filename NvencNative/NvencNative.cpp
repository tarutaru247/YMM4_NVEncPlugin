#include "NvencNative.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <algorithm>

#include "nvEncodeAPI.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
    struct FileWriter
    {
        HANDLE handle = INVALID_HANDLE_VALUE;
        uint64_t position = 0;

        bool Open(const std::wstring& path)
        {
            handle = CreateFileW(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            position = 0;
            return true;
        }

        bool IsOpen() const
        {
            return handle != INVALID_HANDLE_VALUE;
        }

        bool Write(const void* data, size_t size)
        {
            if (!IsOpen())
            {
                return false;
            }
            DWORD written = 0;
            if (!WriteFile(handle, data, static_cast<DWORD>(size), &written, nullptr))
            {
                return false;
            }
            if (written != size)
            {
                return false;
            }
            position += size;
            return true;
        }

        bool Seek(uint64_t pos)
        {
            if (!IsOpen())
            {
                return false;
            }
            LARGE_INTEGER li{};
            li.QuadPart = static_cast<LONGLONG>(pos);
            if (!SetFilePointerEx(handle, li, nullptr, FILE_BEGIN))
            {
                return false;
            }
            position = pos;
            return true;
        }

        uint64_t Tell() const
        {
            return position;
        }

        void Close()
        {
            if (IsOpen())
            {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
            }
        }
    };

    struct Mp4Buffer
    {
        std::vector<uint8_t> data;

        void WriteU8(uint8_t value) { data.push_back(value); }

        void WriteU16(uint16_t value)
        {
            data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            data.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        void WriteU32(uint32_t value)
        {
            data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
            data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            data.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        void WriteU64(uint64_t value)
        {
            for (int i = 7; i >= 0; --i)
            {
                data.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
            }
        }

        void WriteString4(const char* value)
        {
            data.push_back(static_cast<uint8_t>(value[0]));
            data.push_back(static_cast<uint8_t>(value[1]));
            data.push_back(static_cast<uint8_t>(value[2]));
            data.push_back(static_cast<uint8_t>(value[3]));
        }

        void WriteBytes(const std::vector<uint8_t>& bytes)
        {
            data.insert(data.end(), bytes.begin(), bytes.end());
        }

        size_t BeginBox(const char* type)
        {
            size_t start = data.size();
            WriteU32(0);
            WriteString4(type);
            return start;
        }

        void EndBox(size_t start)
        {
            uint32_t size = static_cast<uint32_t>(data.size() - start);
            data[start + 0] = static_cast<uint8_t>((size >> 24) & 0xFF);
            data[start + 1] = static_cast<uint8_t>((size >> 16) & 0xFF);
            data[start + 2] = static_cast<uint8_t>((size >> 8) & 0xFF);
            data[start + 3] = static_cast<uint8_t>(size & 0xFF);
        }
    };

    struct EncoderState
    {
        HMODULE nvencModule = nullptr;
        NVENCSTATUS(NVENCAPI* createInstance)(NV_ENCODE_API_FUNCTION_LIST*) = nullptr;
        NV_ENCODE_API_FUNCTION_LIST funcs{};
        void* session = nullptr;
        NV_ENC_INITIALIZE_PARAMS initParams{};
        NV_ENC_CONFIG config{};
        NV_ENC_OUTPUT_PTR bitstream = nullptr;
        NV_ENC_BUFFER_FORMAT bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
        int width = 0;
        int height = 0;
        int fps = 30;
        uint64_t frameIndex = 0;
        bool writerInitialized = false;
        bool mp4Finalized = false;
        bool isHevc = false;
        FileWriter file;
        uint64_t mdatHeaderOffset = 0;
        uint64_t mdatLargeSizeOffset = 0;
        uint64_t mdatDataOffset = 0;
        std::vector<uint32_t> sampleSizes;
        std::vector<uint64_t> sampleOffsets;
        std::vector<uint32_t> syncSamples;
        std::vector<uint8_t> codecPrivate;
        std::wstring outputPath;
        std::wstring lastError;
    };

    void SetError(EncoderState* state, const std::wstring& message)
    {
        if (state)
        {
            state->lastError = message;
        }
    }

    bool CheckStatus(EncoderState* state, NVENCSTATUS status, const wchar_t* message)
    {
        if (status == NV_ENC_SUCCESS)
        {
            return true;
        }

        std::wstring error = message;
        error += L" (";
        error += std::to_wstring(static_cast<int>(status));
        error += L")";
        SetError(state, error);
        return false;
    }

    bool WriteU32BE(FileWriter& file, uint32_t value)
    {
        uint8_t bytes[4] = {
            static_cast<uint8_t>((value >> 24) & 0xFF),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>(value & 0xFF)
        };
        return file.Write(bytes, sizeof(bytes));
    }

    bool WriteU64BE(FileWriter& file, uint64_t value)
    {
        uint8_t bytes[8];
        for (int i = 7; i >= 0; --i)
        {
            bytes[7 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
        }
        return file.Write(bytes, sizeof(bytes));
    }

    bool WriteString4(FileWriter& file, const char* value)
    {
        return file.Write(value, 4);
    }

    bool WriteFtyp(FileWriter& file, bool hevc)
    {
        const char* brand = hevc ? "hvc1" : "avc1";
        const uint32_t boxSize = 32;
        return WriteU32BE(file, boxSize)
            && WriteString4(file, "ftyp")
            && WriteString4(file, "isom")
            && WriteU32BE(file, 0x00000200)
            && WriteString4(file, "isom")
            && WriteString4(file, "iso2")
            && WriteString4(file, brand)
            && WriteString4(file, "mp41");
    }

    bool InitializeMp4Writer(EncoderState* state, bool hevc, const std::vector<uint8_t>& codecPrivate)
    {
        if (state->writerInitialized)
        {
            return true;
        }

        if (!state->file.Open(state->outputPath))
        {
            SetError(state, L"Failed to open output file.");
            return false;
        }

        state->isHevc = hevc;
        state->codecPrivate = codecPrivate;

        if (!WriteFtyp(state->file, hevc))
        {
            SetError(state, L"Failed to write ftyp.");
            return false;
        }

        state->mdatHeaderOffset = state->file.Tell();
        if (!WriteU32BE(state->file, 1) || !WriteString4(state->file, "mdat"))
        {
            SetError(state, L"Failed to write mdat header.");
            return false;
        }

        state->mdatLargeSizeOffset = state->file.Tell();
        if (!WriteU64BE(state->file, 0))
        {
            SetError(state, L"Failed to write mdat size.");
            return false;
        }

        state->mdatDataOffset = state->file.Tell();
        state->writerInitialized = true;
        return true;
    }

    void WriteMatrix(Mp4Buffer& buffer)
    {
        buffer.WriteU32(0x00010000);
        buffer.WriteU32(0);
        buffer.WriteU32(0);
        buffer.WriteU32(0);
        buffer.WriteU32(0x00010000);
        buffer.WriteU32(0);
        buffer.WriteU32(0);
        buffer.WriteU32(0);
        buffer.WriteU32(0x40000000);
    }

    std::vector<uint8_t> BuildMoov(const EncoderState* state)
    {
        Mp4Buffer moov;

        const uint32_t timescale = 90000;
        const uint32_t fps = state->fps > 0 ? static_cast<uint32_t>(state->fps) : 30;
        const uint32_t frameDuration = timescale / fps;
        const uint32_t sampleCount = static_cast<uint32_t>(state->sampleSizes.size());
        const uint64_t duration = static_cast<uint64_t>(frameDuration) * sampleCount;

        size_t moovStart = moov.BeginBox("moov");

        size_t mvhdStart = moov.BeginBox("mvhd");
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(timescale);
        moov.WriteU32(static_cast<uint32_t>(duration));
        moov.WriteU32(0x00010000);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.WriteU32(0);
        moov.WriteU32(0);
        WriteMatrix(moov);
        for (int i = 0; i < 6; ++i)
        {
            moov.WriteU32(0);
        }
        moov.WriteU32(2);
        moov.EndBox(mvhdStart);

        size_t trakStart = moov.BeginBox("trak");

        size_t tkhdStart = moov.BeginBox("tkhd");
        moov.WriteU32(0x00000007);
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(1);
        moov.WriteU32(0);
        moov.WriteU32(static_cast<uint32_t>(duration));
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.WriteU16(0);
        WriteMatrix(moov);
        moov.WriteU32(static_cast<uint32_t>(state->width) << 16);
        moov.WriteU32(static_cast<uint32_t>(state->height) << 16);
        moov.EndBox(tkhdStart);

        size_t mdiaStart = moov.BeginBox("mdia");

        size_t mdhdStart = moov.BeginBox("mdhd");
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(timescale);
        moov.WriteU32(static_cast<uint32_t>(duration));
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.EndBox(mdhdStart);

        size_t hdlrStart = moov.BeginBox("hdlr");
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteString4("vide");
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(0);
        const char handlerName[] = "VideoHandler";
        moov.data.insert(moov.data.end(), handlerName, handlerName + sizeof(handlerName));
        moov.EndBox(hdlrStart);

        size_t minfStart = moov.BeginBox("minf");

        size_t vmhdStart = moov.BeginBox("vmhd");
        moov.WriteU32(0x00000001);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.EndBox(vmhdStart);

        size_t dinfStart = moov.BeginBox("dinf");
        size_t drefStart = moov.BeginBox("dref");
        moov.WriteU32(0);
        moov.WriteU32(1);
        size_t urlStart = moov.BeginBox("url ");
        moov.WriteU32(0x00000001);
        moov.EndBox(urlStart);
        moov.EndBox(drefStart);
        moov.EndBox(dinfStart);

        size_t stblStart = moov.BeginBox("stbl");

        size_t stsdStart = moov.BeginBox("stsd");
        moov.WriteU32(0);
        moov.WriteU32(1);
        const char* sampleType = state->isHevc ? "hvc1" : "avc1";
        size_t sampleEntryStart = moov.BeginBox(sampleType);
        for (int i = 0; i < 6; ++i) moov.WriteU8(0);
        moov.WriteU16(1);
        moov.WriteU16(0);
        moov.WriteU16(0);
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU16(static_cast<uint16_t>(state->width));
        moov.WriteU16(static_cast<uint16_t>(state->height));
        moov.WriteU32(0x00480000);
        moov.WriteU32(0x00480000);
        moov.WriteU32(0);
        moov.WriteU16(1);
        moov.WriteU8(0);
        for (int i = 0; i < 31; ++i) moov.WriteU8(0);
        moov.WriteU16(0x0018);
        moov.WriteU16(0xFFFF);

        size_t codecBoxStart = moov.BeginBox(state->isHevc ? "hvcC" : "avcC");
        moov.WriteBytes(state->codecPrivate);
        moov.EndBox(codecBoxStart);

        moov.EndBox(sampleEntryStart);
        moov.EndBox(stsdStart);

        size_t sttsStart = moov.BeginBox("stts");
        moov.WriteU32(0);
        moov.WriteU32(1);
        moov.WriteU32(sampleCount);
        moov.WriteU32(frameDuration);
        moov.EndBox(sttsStart);

        size_t stscStart = moov.BeginBox("stsc");
        moov.WriteU32(0);
        moov.WriteU32(1);
        moov.WriteU32(1);
        moov.WriteU32(1);
        moov.WriteU32(1);
        moov.EndBox(stscStart);

        size_t stszStart = moov.BeginBox("stsz");
        moov.WriteU32(0);
        moov.WriteU32(0);
        moov.WriteU32(sampleCount);
        for (uint32_t size : state->sampleSizes)
        {
            moov.WriteU32(size);
        }
        moov.EndBox(stszStart);

        bool useCo64 = false;
        for (uint64_t offset : state->sampleOffsets)
        {
            if (offset > 0xFFFFFFFFu)
            {
                useCo64 = true;
                break;
            }
        }

        size_t stcoStart = moov.BeginBox(useCo64 ? "co64" : "stco");
        moov.WriteU32(0);
        moov.WriteU32(sampleCount);
        if (useCo64)
        {
            for (uint64_t offset : state->sampleOffsets)
            {
                moov.WriteU64(offset);
            }
        }
        else
        {
            for (uint64_t offset : state->sampleOffsets)
            {
                moov.WriteU32(static_cast<uint32_t>(offset));
            }
        }
        moov.EndBox(stcoStart);

        if (!state->syncSamples.empty())
        {
            size_t stssStart = moov.BeginBox("stss");
            moov.WriteU32(0);
            moov.WriteU32(static_cast<uint32_t>(state->syncSamples.size()));
            for (uint32_t sampleIndex : state->syncSamples)
            {
                moov.WriteU32(sampleIndex);
            }
            moov.EndBox(stssStart);
        }

        moov.EndBox(stblStart);
        moov.EndBox(minfStart);
        moov.EndBox(mdiaStart);
        moov.EndBox(trakStart);
        moov.EndBox(moovStart);

        return moov.data;
    }

    bool FinalizeMp4(EncoderState* state)
    {
        if (!state->writerInitialized || state->mp4Finalized)
        {
            return true;
        }

        auto moov = BuildMoov(state);
        if (!state->file.Write(moov.data(), moov.size()))
        {
            SetError(state, L"Failed to write moov.");
            return false;
        }

        uint64_t fileSize = state->file.Tell();
        uint64_t mdatSize = fileSize - state->mdatHeaderOffset;
        if (!state->file.Seek(state->mdatLargeSizeOffset) || !WriteU64BE(state->file, mdatSize))
        {
            SetError(state, L"Failed to update mdat size.");
            return false;
        }

        state->file.Seek(fileSize);
        state->file.Close();
        state->mp4Finalized = true;
        return true;
    }

    struct NalUnit
    {
        const uint8_t* data = nullptr;
        size_t size = 0;
        uint8_t type = 0;
    };

    std::vector<NalUnit> ParseAnnexB(const uint8_t* data, size_t size, bool hevc)
    {
        std::vector<NalUnit> units;
        size_t i = 0;
        auto findStart = [&](size_t from) -> size_t
        {
            for (size_t j = from; j + 3 < size; ++j)
            {
                if (data[j] == 0 && data[j + 1] == 0)
                {
                    if (data[j + 2] == 1)
                    {
                        return j;
                    }
                    if (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1)
                    {
                        return j;
                    }
                }
            }
            return size;
        };

        while (i < size)
        {
            size_t start = findStart(i);
            if (start >= size)
            {
                break;
            }
            size_t scSize = (data[start + 2] == 1) ? 3 : 4;
            size_t nalStart = start + scSize;
            size_t next = findStart(nalStart);
            size_t nalEnd = (next < size) ? next : size;
            if (nalEnd > nalStart)
            {
                uint8_t type = 0;
                if (hevc)
                {
                    type = (data[nalStart] >> 1) & 0x3F;
                }
                else
                {
                    type = data[nalStart] & 0x1F;
                }
                units.push_back({ data + nalStart, nalEnd - nalStart, type });
            }
            i = nalEnd;
        }
        return units;
    }

    std::vector<uint8_t> BuildAvcC(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps)
    {
        if (sps.size() < 4)
        {
            return {};
        }
        std::vector<uint8_t> avcc;
        avcc.push_back(1);
        avcc.push_back(sps[1]);
        avcc.push_back(sps[2]);
        avcc.push_back(sps[3]);
        avcc.push_back(0xFF); // lengthSizeMinusOne=3
        avcc.push_back(0xE1); // numOfSPS=1
        avcc.push_back(static_cast<uint8_t>((sps.size() >> 8) & 0xFF));
        avcc.push_back(static_cast<uint8_t>(sps.size() & 0xFF));
        avcc.insert(avcc.end(), sps.begin(), sps.end());
        avcc.push_back(1); // numOfPPS=1
        avcc.push_back(static_cast<uint8_t>((pps.size() >> 8) & 0xFF));
        avcc.push_back(static_cast<uint8_t>(pps.size() & 0xFF));
        avcc.insert(avcc.end(), pps.begin(), pps.end());
        return avcc;
    }

    std::vector<uint8_t> BuildHvcC(const std::vector<uint8_t>& vps, const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps)
    {
        // Minimal hvcC. Many fields are set to defaults; VPS/SPS/PPS are included.
        std::vector<uint8_t> hvcc;
        hvcc.reserve(64 + vps.size() + sps.size() + pps.size());

        hvcc.push_back(1); // configurationVersion
        hvcc.push_back(1); // general_profile_space(0), tier(0), profile_idc(1=Main)
        hvcc.insert(hvcc.end(), 4, 0); // general_profile_compatibility_flags
        hvcc.insert(hvcc.end(), 6, 0); // general_constraint_indicator_flags
        hvcc.push_back(120); // general_level_idc (4.0)
        hvcc.push_back(0xF0); // min_spatial_segmentation_idc (upper 4 bits set)
        hvcc.push_back(0);
        hvcc.push_back(0xFC); // parallelismType (reserved)
        hvcc.push_back(0xFC); // chromaFormat (reserved)
        hvcc.push_back(0xF8); // bitDepthLumaMinus8 (reserved)
        hvcc.push_back(0xF8); // bitDepthChromaMinus8 (reserved)
        hvcc.push_back(0); // avgFrameRate
        hvcc.push_back(0);
        hvcc.push_back(0x03); // constantFrameRate=0, numTemporalLayers=0, temporalIdNested=0, lengthSizeMinusOne=3

        uint8_t numArrays = 0;
        if (!vps.empty()) numArrays++;
        if (!sps.empty()) numArrays++;
        if (!pps.empty()) numArrays++;
        hvcc.push_back(numArrays);

        auto appendArray = [&](uint8_t nalType, const std::vector<uint8_t>& data)
        {
            hvcc.push_back(0x80 | nalType); // array_completeness=1
            hvcc.push_back(0); // numNalus (hi)
            hvcc.push_back(1); // numNalus (lo)
            hvcc.push_back(static_cast<uint8_t>((data.size() >> 8) & 0xFF));
            hvcc.push_back(static_cast<uint8_t>(data.size() & 0xFF));
            hvcc.insert(hvcc.end(), data.begin(), data.end());
        };

        if (!vps.empty()) appendArray(32, vps);
        if (!sps.empty()) appendArray(33, sps);
        if (!pps.empty()) appendArray(34, pps);

        return hvcc;
    }

    std::vector<uint8_t> ConvertToLengthPrefixed(const std::vector<NalUnit>& units, bool keepParameterSets)
    {
        std::vector<uint8_t> output;
        for (const auto& unit : units)
        {
            if (!keepParameterSets)
            {
                if (unit.type == 7 || unit.type == 8 || unit.type == 32 || unit.type == 33 || unit.type == 34)
                {
                    continue;
                }
            }
            uint32_t len = static_cast<uint32_t>(unit.size);
            output.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
            output.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
            output.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            output.push_back(static_cast<uint8_t>(len & 0xFF));
            output.insert(output.end(), unit.data, unit.data + unit.size);
        }
        return output;
    }


    bool InitializeEncoder(EncoderState* state, ID3D11Device* device, int width, int height, int fps, int bitrateKbps, int codec, NV_ENC_BUFFER_FORMAT bufferFormat)
    {
        state->width = width;
        state->height = height;
        state->fps = fps;
        state->bufferFormat = bufferFormat;

        state->nvencModule = LoadLibraryW(L"nvEncodeAPI64.dll");
        if (!state->nvencModule)
        {
            SetError(state, L"nvEncodeAPI64.dll not found. Check NVIDIA driver.");
            return false;
        }

        state->createInstance = reinterpret_cast<decltype(state->createInstance)>(
            GetProcAddress(state->nvencModule, "NvEncodeAPICreateInstance"));
        if (!state->createInstance)
        {
            SetError(state, L"Failed to get NvEncodeAPICreateInstance.");
            return false;
        }

        state->funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        auto status = state->createInstance(&state->funcs);
        if (!CheckStatus(state, status, L"NvEncodeAPICreateInstance failed"))
        {
            return false;
        }

        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS openParams{};
        openParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        openParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        openParams.device = device;
        openParams.apiVersion = NVENCAPI_VERSION;

        status = state->funcs.nvEncOpenEncodeSessionEx(&openParams, &state->session);
        if (!CheckStatus(state, status, L"nvEncOpenEncodeSessionEx failed"))
        {
            return false;
        }

        state->initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
        state->config.version = NV_ENC_CONFIG_VER;

        const GUID encodeGuid = (codec == 1) ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
        const GUID presetGuid = NV_ENC_PRESET_P3_GUID;
        const NV_ENC_TUNING_INFO tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;

        NV_ENC_PRESET_CONFIG presetConfig{};
        presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
        presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
        status = state->funcs.nvEncGetEncodePresetConfigEx(state->session, encodeGuid, presetGuid, tuningInfo, &presetConfig);
        if (!CheckStatus(state, status, L"nvEncGetEncodePresetConfigEx failed"))
        {
            return false;
        }

        state->config = presetConfig.presetCfg;

        state->initParams.encodeGUID = encodeGuid;
        state->initParams.presetGUID = presetGuid;
        state->initParams.tuningInfo = tuningInfo;
        state->initParams.encodeWidth = width;
        state->initParams.encodeHeight = height;
        state->initParams.maxEncodeWidth = width;
        state->initParams.maxEncodeHeight = height;
        state->initParams.darWidth = width;
        state->initParams.darHeight = height;
        state->initParams.frameRateNum = fps;
        state->initParams.frameRateDen = 1;
        state->initParams.enablePTD = 1;
        state->initParams.reportSliceOffsets = 0;
        state->initParams.enableSubFrameWrite = 0;
        state->initParams.encodeConfig = &state->config;

        state->config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        state->config.rcParams.averageBitRate = static_cast<uint32_t>(bitrateKbps) * 1000;
        state->config.rcParams.maxBitRate = state->config.rcParams.averageBitRate;
        state->config.gopLength = NVENC_INFINITE_GOPLENGTH;
        state->config.frameIntervalP = 1;

        if (codec == 1)
        {
            state->config.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
        }
        else
        {
            state->config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
        }

        status = state->funcs.nvEncInitializeEncoder(state->session, &state->initParams);
        if (!CheckStatus(state, status, L"nvEncInitializeEncoder failed"))
        {
            return false;
        }

        NV_ENC_CREATE_BITSTREAM_BUFFER createBitstream{};
        createBitstream.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        status = state->funcs.nvEncCreateBitstreamBuffer(state->session, &createBitstream);
        if (!CheckStatus(state, status, L"nvEncCreateBitstreamBuffer failed"))
        {
            return false;
        }
        state->bitstream = createBitstream.bitstreamBuffer;

        return true;
    }

    bool EncodeTexture(EncoderState* state, ID3D11Texture2D* texture)
    {
        NV_ENC_REGISTER_RESOURCE registerRes{};
        registerRes.version = NV_ENC_REGISTER_RESOURCE_VER;
        registerRes.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        registerRes.resourceToRegister = texture;
        registerRes.width = state->width;
        registerRes.height = state->height;
        registerRes.bufferFormat = state->bufferFormat;
        registerRes.bufferUsage = NV_ENC_INPUT_IMAGE;

        auto status = state->funcs.nvEncRegisterResource(state->session, &registerRes);
        if (!CheckStatus(state, status, L"nvEncRegisterResource failed"))
        {
            return false;
        }

        NV_ENC_MAP_INPUT_RESOURCE map{};
        map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = registerRes.registeredResource;
        status = state->funcs.nvEncMapInputResource(state->session, &map);
        if (!CheckStatus(state, status, L"nvEncMapInputResource failed"))
        {
            state->funcs.nvEncUnregisterResource(state->session, registerRes.registeredResource);
            return false;
        }

        NV_ENC_PIC_PARAMS pic{};
        pic.version = NV_ENC_PIC_PARAMS_VER;
        pic.inputBuffer = map.mappedResource;
        pic.bufferFmt = registerRes.bufferFormat;
        pic.inputWidth = state->width;
        pic.inputHeight = state->height;
        pic.outputBitstream = state->bitstream;
        pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        pic.inputTimeStamp = state->frameIndex++;
        pic.inputDuration = 1;

        status = state->funcs.nvEncEncodePicture(state->session, &pic);
        state->funcs.nvEncUnmapInputResource(state->session, map.mappedResource);
        state->funcs.nvEncUnregisterResource(state->session, registerRes.registeredResource);
        if (!CheckStatus(state, status, L"nvEncEncodePicture failed"))
        {
            return false;
        }

        NV_ENC_LOCK_BITSTREAM lockBitstream{};
        lockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
        lockBitstream.outputBitstream = state->bitstream;
        status = state->funcs.nvEncLockBitstream(state->session, &lockBitstream);
        if (!CheckStatus(state, status, L"nvEncLockBitstream failed"))
        {
            return false;
        }

        const auto* ptr = static_cast<uint8_t*>(lockBitstream.bitstreamBufferPtr);
        const size_t size = lockBitstream.bitstreamSizeInBytes;
        std::vector<uint8_t> buffer(ptr, ptr + size);
        bool hevc = (state->initParams.encodeGUID == NV_ENC_CODEC_HEVC_GUID);
        auto units = ParseAnnexB(buffer.data(), buffer.size(), hevc);

        std::vector<uint8_t> sps;
        std::vector<uint8_t> pps;
        std::vector<uint8_t> vps;
        bool isKeyframe = false;
        for (const auto& unit : units)
        {
            if (!hevc)
            {
                if (unit.type == 7 && sps.empty())
                {
                    sps.assign(unit.data, unit.data + unit.size);
                }
                else if (unit.type == 8 && pps.empty())
                {
                    pps.assign(unit.data, unit.data + unit.size);
                }
                else if (unit.type == 5)
                {
                    isKeyframe = true;
                }
            }
            else
            {
                if (unit.type == 32 && vps.empty())
                {
                    vps.assign(unit.data, unit.data + unit.size);
                }
                else if (unit.type == 33 && sps.empty())
                {
                    sps.assign(unit.data, unit.data + unit.size);
                }
                else if (unit.type == 34 && pps.empty())
                {
                    pps.assign(unit.data, unit.data + unit.size);
                }
                else if (unit.type == 19 || unit.type == 20)
                {
                    isKeyframe = true;
                }
            }
        }

        if (!state->writerInitialized)
        {
            std::vector<uint8_t> codecPrivate = hevc ? BuildHvcC(vps, sps, pps) : BuildAvcC(sps, pps);
            if (codecPrivate.empty())
            {
                state->funcs.nvEncUnlockBitstream(state->session, state->bitstream);
                return true;
            }
            if (!InitializeMp4Writer(state, hevc, codecPrivate))
            {
                state->funcs.nvEncUnlockBitstream(state->session, state->bitstream);
                return false;
            }
        }

        if (buffer.empty())
        {
            state->funcs.nvEncUnlockBitstream(state->session, state->bitstream);
            return true;
        }

        auto sampleData = ConvertToLengthPrefixed(units, false);
        if (sampleData.empty())
        {
            state->funcs.nvEncUnlockBitstream(state->session, state->bitstream);
            return true;
        }

        uint64_t offset = state->file.Tell();
        if (!state->file.Write(sampleData.data(), sampleData.size()))
        {
            SetError(state, L"Failed to write sample data.");
            state->funcs.nvEncUnlockBitstream(state->session, state->bitstream);
            return false;
        }

        state->sampleOffsets.push_back(offset);
        state->sampleSizes.push_back(static_cast<uint32_t>(sampleData.size()));
        if (isKeyframe)
        {
            state->syncSamples.push_back(static_cast<uint32_t>(state->sampleSizes.size()));
        }

        status = state->funcs.nvEncUnlockBitstream(state->session, state->bitstream);
        return CheckStatus(state, status, L"nvEncUnlockBitstream failed");
    }
}

void* NvencCreate(ID3D11Device* device, int width, int height, int fps, int bitrateKbps, int codec, int bufferFormat, const wchar_t* outputPath)
{
    if (!device || !outputPath)
    {
        return nullptr;
    }

    auto* state = new EncoderState();
    state->outputPath = outputPath;

    if (!InitializeEncoder(state, device, width, height, fps, bitrateKbps, codec, static_cast<NV_ENC_BUFFER_FORMAT>(bufferFormat)))
    {
        return state;
    }

    return state;
}

int NvencEncode(void* handle, ID3D11Texture2D* texture)
{
    auto* state = reinterpret_cast<EncoderState*>(handle);
    if (!state || !texture)
    {
        return 0;
    }

    if (!EncodeTexture(state, texture))
    {
        return 0;
    }

    return 1;
}

int NvencFinalize(void* handle)
{
    auto* state = reinterpret_cast<EncoderState*>(handle);
    if (!state)
    {
        return 0;
    }

    NV_ENC_PIC_PARAMS pic{};
    pic.version = NV_ENC_PIC_PARAMS_VER;
    pic.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    auto status = state->funcs.nvEncEncodePicture(state->session, &pic);
    if (status != NV_ENC_SUCCESS)
    {
        SetError(state, L"nvEncEncodePicture (EOS) failed");
        return 0;
    }

    if (!FinalizeMp4(state))
    {
        return 0;
    }

    return 1;
}

void NvencDestroy(void* handle)
{
    auto* state = reinterpret_cast<EncoderState*>(handle);
    if (!state)
    {
        return;
    }

    if (state->session)
    {
        if (state->bitstream)
        {
            state->funcs.nvEncDestroyBitstreamBuffer(state->session, state->bitstream);
            state->bitstream = nullptr;
        }
        state->funcs.nvEncDestroyEncoder(state->session);
        state->session = nullptr;
    }

    if (!state->mp4Finalized)
    {
        FinalizeMp4(state);
    }

    if (state->nvencModule)
    {
        FreeLibrary(state->nvencModule);
        state->nvencModule = nullptr;
    }

    delete state;
}

const wchar_t* NvencGetLastError(void* handle)
{
    auto* state = reinterpret_cast<EncoderState*>(handle);
    if (!state)
    {
        return L"";
    }
    return state->lastError.c_str();
}
