
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

namespace Vulkan {
class VKScheduler;
namespace vk {
class Instance;
}
}

namespace Tegra {

class Record {
public:
    /// Debug-only. Enable an engine to record its method calls for each frame.
    static constexpr std::array<bool, 5> RECORD_ENGINE{
        true, // FERMI_TWOD_A
        true,  // MAXWELL_B
        true, // KEPLER_COMPUTE_B
        true, // KEPLER_INLINE_TO_MEMORY_B
        true  // MAXWELL_DMA_COPY_A
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

    [[nodiscard]] static constexpr EngineID GetEngineIDFromIndex(const u32 index) {
        switch (index) {
        case 0:
            return EngineID::FERMI_TWOD_A;
        case 1:
            return EngineID::MAXWELL_B;
        case 2:
            return EngineID::KEPLER_COMPUTE_B;
        case 3:
            return EngineID::KEPLER_INLINE_TO_MEMORY_B;
        case 4:
            return EngineID::MAXWELL_DMA_COPY_A;
        }
        return EngineID::MAXWELL_B;
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
            return "MAXDMA";
        }
        UNREACHABLE();
        return "Unknown";
    }

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

    static void BuildResults(Tegra::GPU* gpu, size_t frame);
    static void OutputMarker(Tegra::GPU* gpu, Vulkan::VKScheduler* scheduler = nullptr);
    static void CaptureFrames(u32 num = 1);

    [[nodiscard]] static std::vector<std::string> GetMethodNames(
        GPU::RecordEntry& entry, std::array<Method, 400>::const_iterator start_it,
        size_t struct_idx, size_t element_idx, bool is_prev_state);
    [[nodiscard]] static std::string GetArgumentInfo(
        GPU::RecordEntry& entry, std::array<Method, 400>::const_iterator foundMethod, size_t i);
    [[nodiscard]] static std::string GetFermiArg(GPU::RecordEntry& entry,
                                                 std::array<Method, 400>::const_iterator method,
                                                 size_t i);
    [[nodiscard]] static std::string GetMaxwellArg(GPU::RecordEntry& entry,
                                                   std::array<Method, 400>::const_iterator method,
                                                   size_t i);
    [[nodiscard]] static std::string GetKeplerComputeArg(
        GPU::RecordEntry& entry, std::array<Method, 400>::const_iterator method, size_t i);
    [[nodiscard]] static std::string GetKeplerMemoryArg(
        GPU::RecordEntry& entry, std::array<Method, 400>::const_iterator method, size_t i);
    [[nodiscard]] static std::string GetMaxwellDMAArg(
        GPU::RecordEntry& entry, std::array<Method, 400>::const_iterator method, size_t i);
};
} // namespace Tegra