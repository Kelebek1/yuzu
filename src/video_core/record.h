
#pragma once
#include <array>
#include <string_view>
#include <tuple>
#include "common/common_types.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/gpu.h"

namespace Tegra {
static bool CURRENTLY_RECORDING = false;
static std::vector<std::tuple<Tegra::EngineID, u32, u32>> METHODS_CALLED;

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
        std::any_of(RECORD_ENGINE.begin(), RECORD_ENGINE.end(),
                                                  [](bool active) { return active; });

    [[nodiscard]] static constexpr std::string_view GetEngineName(Tegra::EngineID id);
    [[nodiscard]] static constexpr std::string_view GetMethodName(Tegra::EngineID id, u32 method);
    [[nodiscard]] static constexpr s32 GetEngineIndex(const Tegra::EngineID id);
    static void Print(Tegra::GPU& gpu, size_t frame);

    struct Method {
        EngineID engine;
        u32 offset;
        u32 size;
        std::string_view name;
        std::string_view type;

        constexpr Method(EngineID engine_, u32 offset_, u32 size_, std::string_view name_,
                         std::string_view type_)
            : engine{engine_}, offset{offset_}, size{size_}, name{name_}, type{type_} {};
        constexpr Method(){};
    };
    using Maxwell = Tegra::Engines::Maxwell3D;
    using REG_LIST = std::array<Method, Maxwell::Regs::NUM_REGS>;

    [[nodiscard]] static constexpr REG_LIST BuildFermiMethods();
    [[nodiscard]] static constexpr REG_LIST BuildMaxwellMethods();
    [[nodiscard]] static constexpr REG_LIST BuildKeplerComputeMethods();
    [[nodiscard]] static constexpr REG_LIST BuildKeplerMemoryMethods();
    [[nodiscard]] static constexpr REG_LIST BuildMaxwellDMAMethods();
    static constexpr std::array<REG_LIST, 5> RECORD_METHODS{{
        BuildFermiMethods(),
        BuildMaxwellMethods(),
        BuildKeplerComputeMethods(),
        BuildKeplerMemoryMethods(),
        BuildMaxwellDMAMethods(),
    }};
};
} // namespace Tegra