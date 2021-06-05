#include "record.h"

namespace Tegra {
/// Return an index for a given EngineID to use with arrays
[[nodiscard]] constexpr s32 Record::GetEngineIndex(const EngineID id) {
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

/// Return the name of an engine
[[nodiscard]] constexpr std::string_view Record::GetEngineName(EngineID id) {
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

[[nodiscard]] constexpr std::string_view Record::GetMethodName(Tegra::EngineID id, u32 offset) {
    for (auto& method : RECORD_METHODS[GetEngineIndex(id)]) {
        if (method.offset <= offset && offset <= method.offset + method.size) {
            return method.name;
        }
    }
    return "Invalid";
}

void Record::Print(Tegra::GPU& gpu, size_t frame) {
    LOG_INFO(Render_OpenGL, "Methods called for frame {}:", frame);
    std::string out;
    out.reserve(0x1000);
    for (auto& [engine, method, arg] : METHODS_CALLED) {
        out += fmt::format("\tEngine {} -- {}({:X})\n", GetEngineName(engine),
                           GetMethodName(engine, method), arg);
    }
    LOG_INFO(Render_OpenGL, "{}", out);
    METHODS_CALLED.clear();
    CURRENTLY_RECORDING = false;
}

[[nodiscard]] constexpr Record::REG_LIST Record::BuildFermiMethods() {
    return {};
}

[[nodiscard]] constexpr Record::REG_LIST Record::BuildMaxwellMethods() {
    return {{
        {EngineID::MAXWELL_B, 0x0, 0x44, "unk_0000", "u32"},
    }};
}

[[nodiscard]] constexpr Record::REG_LIST Record::BuildKeplerComputeMethods() {
    return {};
}

[[nodiscard]] constexpr Record::REG_LIST Record::BuildKeplerMemoryMethods() {
    return {};
}

[[nodiscard]] constexpr Record::REG_LIST Record::BuildMaxwellDMAMethods() {
    return {};
}
} // namespace Tegra
