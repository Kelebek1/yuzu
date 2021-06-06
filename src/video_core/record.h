
#pragma once
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include "common/common_types.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/gpu.h"

namespace Tegra {

class Record {
public:
    /// Debug-only. Enable an engine to record its method calls for each frame.
    static constexpr std::array<bool, 5> RECORD_ENGINE{
        false, // FERMI_TWOD_A
        true,  // MAXWELL_B
        false, // KEPLER_COMPUTE_B
        false, // KEPLER_INLINE_TO_MEMORY_B
        false  // MAXWELL_DMA_COPY_A
    };
    static constexpr bool DO_RECORD =
        std::any_of(RECORD_ENGINE.begin(), RECORD_ENGINE.end(), [](bool active) { return active; });

    [[nodiscard]] static constexpr s32 GetEngineIndex(const Tegra::EngineID id) {
        switch (id) {
        case EngineID::FERMI_TWOD_A:
            return 0;
        case EngineID::MAXWELL_B:
            return 1;
        case EngineID::KEPLER_COMPUTE_B:
            return 2;
        case EngineID::KEPLER_INLINE_TO_MEMORY_B:
            return 3;
        case EngineID::MAXWELL_DMA_COPY_A:
            return 4;
        }
        return 1;
    }

    [[nodiscard]] static std::string_view GetEngineName(Tegra::EngineID id) {
        switch (id) {
        case EngineID::FERMI_TWOD_A:
            return "FERMI2D";
        case EngineID::KEPLER_COMPUTE_B:
            return "KEPLERC";
        case EngineID::KEPLER_INLINE_TO_MEMORY_B:
            return "KEPLERI";
        case EngineID::MAXWELL_B:
            return "MAXWELL";
        case EngineID::MAXWELL_DMA_COPY_A:
            return "MAXWDMA";
        }
        UNREACHABLE();
        return "Unknown";
    }

    enum class Types {
        BOOL = 0,
        U8,
        U16,
        U32,
        S8,
        S16,
        S32,
        F32,
        BITFIELD,
        ARRAY,
    };

    struct Method {
        u32 offset = 0;
        u32 elem_count = 0;
        u32 elem_size = 0;
        u32 struct_base = 0;
        u32 struct_count = 0;
        u32 struct_size = 0;
        std::string_view name;

        constexpr Method() = default;
        constexpr Method(u32 offset_, u32 elem_count_, u32 elem_size_, u32 struct_base_,
                         u32 struct_count_, u32 struct_size_, std::string_view name_)
            : offset{offset_}, elem_count{elem_count_}, elem_size{elem_size_},
              struct_base{struct_base_}, struct_count{struct_count_},
              struct_size{struct_size_}, name{name_} {};
    };

    static void Print(Tegra::GPU& gpu, size_t frame);

    [[nodiscard]] static std::vector<std::string> GetMethodNames(GPU::RecordEntry& entry);
    [[nodiscard]] static std::string GetArgumentInfo(GPU::RecordEntry& entry);
    [[nodiscard]] static std::string GetFermiArg(u32 method, u32 arg);
    [[nodiscard]] static std::string GetMaxwellArg(u32 method, u32 arg);
    [[nodiscard]] static std::string GetKeplerComputeArg(u32 method, u32 arg);
    [[nodiscard]] static std::string GetKeplerMemoryArg(u32 method, u32 arg);
    [[nodiscard]] static std::string GetMaxwellDMAArg(u32 method, u32 arg);
};
} // namespace Tegra