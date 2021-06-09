#include <optional>
#include <set>
#include "boost/algorithm/string/classification.hpp"
#include "boost/algorithm/string/split.hpp"
#include "record.h"
#include "surface.h"

namespace Tegra {

using Maxwell = Tegra::Engines::Maxwell3D;
using REG_LIST = std::array<Record::Method, 400>;
#define REG(field_name) (offsetof(Maxwell::Regs, field_name) / sizeof(u32))

std::optional<std::tuple<REG_LIST::const_iterator, size_t, size_t>> FindMethod(
    GPU::RecordEntry& entry);

void Record::BuildResults(Tegra::GPU* gpu, size_t frame) {
    u32 lastDraw = -1;
    std::unordered_set<u32> encountered_methods;
    for (auto& entry : gpu->METHODS_CALLED) {
        GPU::DrawResult result;
        if (entry.draw != lastDraw) {
            result.draw = entry.draw;
            lastDraw = entry.draw;
        }
        const auto method = FindMethod(entry);
        if (!method) {
            continue;
        }
        result.engineName = GetEngineName(entry.engine);
        encountered_methods.insert(entry.method);
        const auto& [foundMethod, struct_idx, element_idx] = *method;
        result.method = entry.method;
        const auto methodNames = GetMethodNames(entry, foundMethod, struct_idx, element_idx, false);
        const auto time = std::chrono::duration_cast<std::chrono::microseconds>(
            entry.timestamp - gpu->RECORD_TIME_ORIGIN);
        result.time = time;

        size_t i = 0;
        for (const auto& name : methodNames) {
            const auto arg = GetArgumentInfo(entry, foundMethod, i);
            result.args.emplace_back(std::make_pair(name, arg));
            ++i;
        }
        gpu->RECORD_RESULTS_CHANGED.emplace_back(std::move(result));
    }

    std::vector<std::string> unchanged_state;
    for (auto& [_, entry] : gpu->RECORD_OLD_REGS) {
        if (encountered_methods.contains(entry.method)) {
            continue;
        }
        auto found = FindMethod(entry);
        if (!found) {
            continue;
        }
        const auto& [foundMethod, struct_idx, element_idx] = *found;
        auto methodNames = GetMethodNames(entry, foundMethod, struct_idx, element_idx, true);
        if (methodNames.size() == 0) {
            continue;
        }

        GPU::DrawResult result;
        result.method = entry.method;
        result.engineName = GetEngineName(entry.engine);
        result.draw = -1;
        result.time = std::chrono::microseconds(0);
        size_t i = 0;
        for (const auto& name : methodNames) {
            const auto arg = GetArgumentInfo(entry, foundMethod, i);
            result.args.emplace_back(std::make_pair(name, arg));
            ++i;
        }
        gpu->RECORD_RESULTS_UNCHANGED.emplace_back(std::move(result));
    }

    std::sort(gpu->RECORD_RESULTS_UNCHANGED.begin(), gpu->RECORD_RESULTS_UNCHANGED.end(),
              [](GPU::DrawResult& a, GPU::DrawResult& b) { return a.method < b.method; });
}

[[nodiscard]] std::string Record::GetArgumentInfo(GPU::RecordEntry& entry,
                                                  REG_LIST::const_iterator foundMethod, size_t i) {
    switch (entry.engine) {
    case EngineID::FERMI_TWOD_A:
        return GetFermiArg(entry, foundMethod, i);
    case EngineID::MAXWELL_B:
        return GetMaxwellArg(entry, foundMethod, i);
    case EngineID::KEPLER_COMPUTE_B:
        return GetKeplerComputeArg(entry, foundMethod, i);
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        return GetKeplerMemoryArg(entry, foundMethod, i);
    case EngineID::MAXWELL_DMA_COPY_A:
        return GetMaxwellDMAArg(entry, foundMethod, i);
    }
    UNREACHABLE();
    return "";
}

std::string Record::GetFermiArg(GPU::RecordEntry& entry, REG_LIST::const_iterator method,
                                size_t i) {
    return "";
}

std::string Record::GetMaxwellArg(GPU::RecordEntry& entry, REG_LIST::const_iterator method,
                                  size_t i) {
    using Regs = Maxwell::Regs;

    // static bool printed = false;
    // if (!printed) {
    //    // LOG_INFO(HW_Memory, "REG(scissor_test) {} REG(scissor_test[0].enable) {}",
    //    //         REG(scissor_test), REG(scissor_test[0].enable));
    //    printed = true;
    //}

    switch (method->offset) {

    case REG(wait_for_idle):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(shadow_ram_control): {
        const auto arg = *(Regs::ShadowRamControl*)(&entry.arg);
        switch (arg) {
        case Regs::ShadowRamControl::Track:
            return "Track";
        case Regs::ShadowRamControl::TrackWithFilter:
            return "TrackWithFilter";
        case Regs::ShadowRamControl::Passthrough:
            return "Passthrough";
        case Regs::ShadowRamControl::Replay:
            return "Replay";
        }
        break;
    }

    case REG(upload.dest.block_width): {
        const auto arg = *(Tegra::Engines::Upload::Registers*)(&entry.arg);
        switch (i) {
        case 0:
            return fmt::format("{}", arg.dest.block_width);
        case 1:
            return fmt::format("{}", arg.dest.block_height);
        case 2:
            return fmt::format("{}", arg.dest.block_depth);
        }
        break;
    }
    case REG(upload.dest.width):
    case REG(upload.dest.height):
    case REG(upload.dest.depth):
    case REG(upload.dest.z):
    case REG(upload.dest.x):
    case REG(upload.dest.y):
        return fmt::format("{}", entry.arg);

    case REG(exec_upload.linear):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(force_early_fragment_tests):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(sync_info): {
        switch (i) {
        case 0:
            return fmt::format("0x{:X}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 16) & 1));
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 20) & 1));
        }
        break;
    }

    case REG(tess_mode): {
        switch (i) {
        case 0: {
            const auto arg = *(Regs::TessellationPrimitive*)(&entry.arg);
            switch (arg) {
            case Regs::TessellationPrimitive::Isolines:
                return "Isolines";
            case Regs::TessellationPrimitive::Triangles:
                return "Triangles";
            case Regs::TessellationPrimitive::Quads:
                return "Quads";
            }
        } break;
        case 1: {
            const auto arg = *(Regs::TessellationSpacing*)(&entry.arg);
            switch (arg) {
            case Regs::TessellationSpacing::Equal:
                return "Equal";
            case Regs::TessellationSpacing::FractionalOdd:
                return "FractionalOdd";
            case Regs::TessellationSpacing::FractionalEven:
                return "FractionalEven";
            }
        } break;
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 8) & 1));
        case 3:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 9) & 1));
        }
        break;
    }
    case REG(tess_level_outer):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));
    case REG(tess_level_inner):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(rasterize_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(tfb_bindings) + REG(tfb_bindings[0].buffer_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(tfb_enabled):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(rt) + REG(rt[0].width):
        return fmt::format("{}", entry.arg);
    case REG(rt) + REG(rt[0].height):
        return fmt::format("{}", entry.arg);
    case REG(rt) + REG(rt[0].format): {
        const auto arg = *(Tegra::RenderTargetFormat*)(&entry.arg);
        if (arg == Tegra::RenderTargetFormat::NONE) {
            return "None";
        }
        const auto format = VideoCore::Surface::PixelFormatFromRenderTargetFormat(arg);
        return fmt::format("{}", VideoCore::Surface::GetPixelFormatName(format));
    }
    case REG(rt) + REG(rt[0].tile_mode): {
        switch (i) {
        case 0:
        case 1:
        case 2:
            return fmt::format("{}", entry.arg);
        case 3:
        case 4:
            return fmt::format("{}", static_cast<bool>(entry.arg));
        }
        break;
    }
    case REG(rt) + REG(rt[0].depth): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 1);
        }
        break;
    }
    case REG(rt) + REG(rt[0].base_layer):
        return fmt::format("{}", entry.arg);

    case REG(viewport_transform) + REG(viewport_transform[0].scale_x):
    case REG(viewport_transform) + REG(viewport_transform[0].scale_y):
    case REG(viewport_transform) + REG(viewport_transform[0].scale_z):
    case REG(viewport_transform) + REG(viewport_transform[0].translate_x):
    case REG(viewport_transform) + REG(viewport_transform[0].translate_y):
    case REG(viewport_transform) + REG(viewport_transform[0].translate_z):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));
    case REG(viewport_transform) + REG(viewport_transform[0].swizzle): {
        switch (i) {
        case 0:
            return fmt::format("{:X}", entry.arg);
        case 1:
        case 2:
        case 3:
        case 4: {
            u32 temp;
            if (i == 1) {
                temp = entry.arg & 0x7;
            } else if (i == 2) {
                temp = (entry.arg >> 4) & 0x7;
            } else if (i == 3) {
                temp = (entry.arg >> 8) & 0x7;
            } else if (i == 4) {
                temp = (entry.arg >> 12) & 0x7;
            }
            const auto arg = *(Regs::ViewportSwizzle*)(&temp);
            switch (arg) {
            case Regs::ViewportSwizzle::PositiveX:
                return "PositiveX";
            case Regs::ViewportSwizzle::NegativeX:
                return "NegativeX";
            case Regs::ViewportSwizzle::PositiveY:
                return "PositiveY";
            case Regs::ViewportSwizzle::NegativeY:
                return "NegativeY";
            case Regs::ViewportSwizzle::PositiveZ:
                return "PositiveZ";
            case Regs::ViewportSwizzle::NegativeZ:
                return "NegativeZ";
            case Regs::ViewportSwizzle::PositiveW:
                return "PositiveW";
            case Regs::ViewportSwizzle::NegativeW:
                return "NegativeW";
            }
            break;
        } break;
        }
        break;
    }

    case REG(viewports) + REG(viewports[0].x): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 0xFFFF);
        }
        break;
    }
    case REG(viewports) + REG(viewports[0].y): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 0xFFFF);
        }
        break;
    }
    case REG(viewports) + REG(viewports[0].depth_range_near):
    case REG(viewports) + REG(viewports[0].depth_range_far):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(depth_mode): {
        const auto arg = *(Regs::DepthMode*)(&entry.arg);
        switch (arg) {
        case Regs::DepthMode::MinusOneToOne:
            return "MinusOneToOne";
        case Regs::DepthMode::ZeroToOne:
            return "ZeroToOne";
        }
        break;
    }

    case REG(clear_color):
    case REG(clear_depth):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(clear_stencil):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(polygon_mode_front):
    case REG(polygon_mode_back): {
        const auto arg = *(Regs::PolygonMode*)(&entry.arg);
        switch (arg) {
        case Regs::PolygonMode::Point:
            return "Point";
        case Regs::PolygonMode::Line:
            return "Line";
        case Regs::PolygonMode::Fill:
            return "Fill";
        }
        break;
    }

    case REG(polygon_offset_point_enable):
    case REG(polygon_offset_line_enable):
    case REG(polygon_offset_fill_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(scissor_test) + REG(scissor_test[0].enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));
    case REG(scissor_test) + REG(scissor_test[0].min_x): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 0xFFFF);
        }
        break;
    }
    case REG(scissor_test) + REG(scissor_test[0].min_y): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 0xFFFF);
        }
        break;
    }

    case REG(invalidate_texture_data_cache):
    case REG(invalidate_sampler_cache_no_wfi):
    case REG(invalidate_texture_header_cache_no_wfi): {
        switch (i) {
        case 0: {
            switch (entry.arg) {
            case 0:
                return "All";
            case 1:
                return "One";
            }
            break;
        }
        case 1:
            return fmt::format("{}", (entry.arg >> 4) & 0x3FFFFF);
        }
        break;
    }

    case REG(color_mask_common):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(depth_bounds):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(rt_separate_frag_data):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(multisample_raster_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));
    case REG(multisample_raster_samples):
        return fmt::format("{}", entry.arg);

    case REG(zeta.format): {
        const auto arg = *(Tegra::DepthFormat*)(&entry.arg);
        switch (arg) {
        case Tegra::DepthFormat::D32_FLOAT:
            return "D32_FLOAT";
        case Tegra::DepthFormat::D16_UNORM:
            return "D16_UNORM";
        case Tegra::DepthFormat::S8_UINT_Z24_UNORM:
            return "S8_UINT_Z24_UNORM";
        case Tegra::DepthFormat::D24X8_UNORM:
            return "D24X8_UNORM";
        case Tegra::DepthFormat::D24S8_UNORM:
            return "D24S8_UNORM";
        case Tegra::DepthFormat::D24C8_UNORM:
            return "D24C8_UNORM";
        case Tegra::DepthFormat::D32_FLOAT_S8X24_UINT:
            return "D32_FLOAT_S8X24_UINT";
        }
        break;
    }

    case REG(zeta.tile_mode): {
        switch (i) {
        case 0:
        case 1:
        case 2:
            return fmt::format("{}", entry.arg);
        case 3:
        case 4:
            return fmt::format("{}", static_cast<bool>(entry.arg));
        }
        break;
    }

    case REG(render_area.x): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 0xFFFF);
        }
        break;
    }
    case REG(render_area.y): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 0xFFFF);
        }
        break;
    }

    case REG(clear_flags): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 0xF));
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 0xF));
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 8) & 0xF));
        case 3:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 12) & 0xF));
        }
        break;
    }

    case REG(fill_rectangle):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(vertex_attrib_format): {
        const auto arg = *(Regs::VertexAttribute*)(&entry.arg);
        switch (i) {
        case 0:
            return fmt::format("{}", arg.buffer);
        case 1:
            return fmt::format("{}", arg.constant);
        case 2:
            return fmt::format("{}", arg.offset);
        case 3:
            switch (arg.size) {
            case Regs::VertexAttribute::Size::Invalid:
                return "Invalid";
            case Regs::VertexAttribute::Size::Size_32_32_32_32:
                return "32_32_32_32";
            case Regs::VertexAttribute::Size::Size_32_32_32:
                return "32_32_32";
            case Regs::VertexAttribute::Size::Size_16_16_16_16:
                return "16_16_16_16";
            case Regs::VertexAttribute::Size::Size_32_32:
                return "32_32";
            case Regs::VertexAttribute::Size::Size_16_16_16:
                return "16_16_16";
            case Regs::VertexAttribute::Size::Size_8_8_8_8:
                return "8_8_8_8";
            case Regs::VertexAttribute::Size::Size_16_16:
                return "16_16";
            case Regs::VertexAttribute::Size::Size_32:
                return "32";
            case Regs::VertexAttribute::Size::Size_8_8_8:
                return "8_8_8";
            case Regs::VertexAttribute::Size::Size_8_8:
                return "8_8";
            case Regs::VertexAttribute::Size::Size_16:
                return "16";
            case Regs::VertexAttribute::Size::Size_8:
                return "8";
            case Regs::VertexAttribute::Size::Size_10_10_10_2:
                return "10_10_10_2";
            case Regs::VertexAttribute::Size::Size_11_11_10:
                return "11_11_10";
            }
            break;
        case 4:
            switch (arg.type) {
            case Regs::VertexAttribute::Type::SignedNorm:
                return "SignedNorm";
            case Regs::VertexAttribute::Type::UnsignedNorm:
                return "UnsignedNorm";
            case Regs::VertexAttribute::Type::SignedInt:
                return "SignedInt";
            case Regs::VertexAttribute::Type::UnsignedInt:
                return "UnsignedInt";
            case Regs::VertexAttribute::Type::UnsignedScaled:
                return "UnsignedScaled";
            case Regs::VertexAttribute::Type::SignedScaled:
                return "SignedScaled";
            case Regs::VertexAttribute::Type::Float:
                return "Float";
            }
            break;
        case 5:
            return fmt::format("{}", arg.bgra);
        case 6:
            return fmt::format("0x{:X}", arg.hex);
        }
        break;
    }

    case REG(multisample_sample_locations): {
        const auto arg = *(Regs::MsaaSampleLocation*)(&entry.arg);
        switch (i) {
        case 0:
            return fmt::format("{}", arg.x0);
        case 1:
            return fmt::format("{}", arg.y0);
        case 2:
            return fmt::format("{}", arg.x1);
        case 3:
            return fmt::format("{}", arg.y1);
        case 4:
            return fmt::format("{}", arg.x2);
        case 5:
            return fmt::format("{}", arg.y2);
        case 6:
            return fmt::format("{}", arg.x3);
        case 7:
            return fmt::format("{}", arg.y3);
        }
        break;
    }

    case REG(multisample_coverage_to_color): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 1:
            return fmt::format("{}", (entry.arg >> 4) & 0x7);
        }
        break;
    }

    case REG(rt_control): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xF);
        case 1:
            return fmt::format("{}", (entry.arg >> 4) & 0x7);
        case 2:
            return fmt::format("{}", (entry.arg >> 7) & 0x7);
        case 3:
            return fmt::format("{}", (entry.arg >> 10) & 0x7);
        case 4:
            return fmt::format("{}", (entry.arg >> 13) & 0x7);
        case 5:
            return fmt::format("{}", (entry.arg >> 16) & 0x7);
        case 6:
            return fmt::format("{}", (entry.arg >> 19) & 0x7);
        case 7:
            return fmt::format("{}", (entry.arg >> 22) & 0x7);
        case 8:
            return fmt::format("{}", (entry.arg >> 25) & 0x7);
        }
        break;
    }

    case REG(zeta_width):
    case REG(zeta_height):
        return fmt::format("{}", entry.arg);
        return fmt::format("{}", entry.arg);

    case REG(zeta_depth): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFFF);
        case 1:
            return fmt::format("{}", (entry.arg >> 16) & 1);
        }
        break;
    }

    case REG(sampler_index): {
        switch (entry.arg) {
        case 0:
            return "Independently";
        case 1:
            return "ViaHeaderIndex";
        }
        break;
    }

    case REG(depth_test_enable):
    case REG(independent_blend_enable):
    case REG(depth_write_enabled):
    case REG(alpha_test_enabled):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(d3d_cull_mode): {
        switch (entry.arg) {
        case 1:
            return "None";
        case 2:
            return "Front";
        case 3:
            return "Back";
        }
        break;
    }

    case REG(depth_test_func):
    case REG(alpha_test_func):
    case REG(stencil_front_func_func):
    case REG(stencil_back_func_func): {
        const auto arg = *(Regs::ComparisonOp*)(&entry.arg);
        switch (arg) {
        case Regs::ComparisonOp::Never:
        case Regs::ComparisonOp::NeverOld:
            return "Never";
        case Regs::ComparisonOp::Less:
        case Regs::ComparisonOp::LessOld:
            return "Less";
        case Regs::ComparisonOp::Equal:
        case Regs::ComparisonOp::EqualOld:
            return "Equal";
        case Regs::ComparisonOp::LessEqual:
        case Regs::ComparisonOp::LessEqualOld:
            return "LessEqual";
        case Regs::ComparisonOp::Greater:
        case Regs::ComparisonOp::GreaterOld:
            return "Greater";
        case Regs::ComparisonOp::NotEqual:
        case Regs::ComparisonOp::NotEqualOld:
            return "NotEqual";
        case Regs::ComparisonOp::GreaterEqual:
        case Regs::ComparisonOp::GreaterEqualOld:
            return "GreaterEqual";
        case Regs::ComparisonOp::Always:
        case Regs::ComparisonOp::AlwaysOld:
            return "Always";
        }
        break;
    }

    case REG(alpha_test_ref):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(blend_color.r):
    case REG(blend_color.b):
    case REG(blend_color.g):
    case REG(blend_color.a):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(blend.separate_alpha):
    case REG(independent_blend[0].separate_alpha):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(blend.equation_rgb):
    case REG(blend.equation_a):
    case REG(independent_blend[0].equation_rgb):
    case REG(independent_blend[0].equation_a): {
        const auto arg = *(Regs::Blend::Equation*)(&entry.arg);
        switch (arg) {
        case Regs::Blend::Equation::Add:
        case Regs::Blend::Equation::AddGL:
            return "Add";
        case Regs::Blend::Equation::Subtract:
        case Regs::Blend::Equation::SubtractGL:
            return "Subtract";
        case Regs::Blend::Equation::ReverseSubtract:
        case Regs::Blend::Equation::ReverseSubtractGL:
            return "ReverseSubtract";
        case Regs::Blend::Equation::Min:
        case Regs::Blend::Equation::MinGL:
            return "Min";
        case Regs::Blend::Equation::Max:
        case Regs::Blend::Equation::MaxGL:
            return "Max";
        }
        break;
    }

    case REG(blend.factor_source_rgb):
    case REG(blend.factor_dest_rgb):
    case REG(blend.factor_source_a):
    case REG(blend.factor_dest_a):
    case REG(independent_blend[0].factor_source_rgb):
    case REG(independent_blend[0].factor_dest_rgb):
    case REG(independent_blend[0].factor_source_a):
    case REG(independent_blend[0].factor_dest_a): {
        const auto arg = *(Regs::Blend::Factor*)(&entry.arg);
        switch (arg) {
        case Regs::Blend::Factor::Zero:
        case Regs::Blend::Factor::ZeroGL:
            return "Zero";
        case Regs::Blend::Factor::One:
        case Regs::Blend::Factor::OneGL:
            return "One";
        case Regs::Blend::Factor::SourceColor:
        case Regs::Blend::Factor::SourceColorGL:
            return "SourceColor";
        case Regs::Blend::Factor::OneMinusSourceColor:
        case Regs::Blend::Factor::OneMinusSourceColorGL:
            return "OneMinusSourceColor";
        case Regs::Blend::Factor::SourceAlpha:
        case Regs::Blend::Factor::SourceAlphaGL:
            return "SourceAlpha";
        case Regs::Blend::Factor::OneMinusSourceAlpha:
        case Regs::Blend::Factor::OneMinusSourceAlphaGL:
            return "OneMinusSourceAlpha";
        case Regs::Blend::Factor::DestAlpha:
        case Regs::Blend::Factor::DestAlphaGL:
            return "DestAlpha";
        case Regs::Blend::Factor::OneMinusDestAlpha:
        case Regs::Blend::Factor::OneMinusDestAlphaGL:
            return "OneMinusDestAlpha";
        case Regs::Blend::Factor::DestColor:
        case Regs::Blend::Factor::DestColorGL:
            return "DestColor";
        case Regs::Blend::Factor::OneMinusDestColor:
        case Regs::Blend::Factor::OneMinusDestColorGL:
            return "OneMinusDestColor";
        case Regs::Blend::Factor::SourceAlphaSaturate:
        case Regs::Blend::Factor::SourceAlphaSaturateGL:
            return "SourceAlphaSaturate";
        case Regs::Blend::Factor::Source1Color:
        case Regs::Blend::Factor::ConstantColorGL:
            return "Source1Color";
        case Regs::Blend::Factor::OneMinusSource1Color:
        case Regs::Blend::Factor::OneMinusConstantColorGL:
            return "OneMinusSource1Color";
        case Regs::Blend::Factor::Source1Alpha:
        case Regs::Blend::Factor::ConstantAlphaGL:
            return "Source1Alpha";
        case Regs::Blend::Factor::OneMinusSource1Alpha:
        case Regs::Blend::Factor::OneMinusConstantAlphaGL:
            return "OneMinusSource1Alpha";
        case Regs::Blend::Factor::ConstantColor:
        case Regs::Blend::Factor::Source1ColorGL:
            return "ConstantColor";
        case Regs::Blend::Factor::OneMinusConstantColor:
        case Regs::Blend::Factor::OneMinusSource1ColorGL:
            return "OneMinusConstantColor";
        case Regs::Blend::Factor::ConstantAlpha:
        case Regs::Blend::Factor::Source1AlphaGL:
            return "ConstantAlpha";
        case Regs::Blend::Factor::OneMinusConstantAlpha:
        case Regs::Blend::Factor::OneMinusSource1AlphaGL:
            return "OneMinusConstantAlpha";
        }
        break;
    }

    case REG(blend.enable_common):
    case REG(blend.enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(stencil_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(stencil_front_op_fail):
    case REG(stencil_front_op_zfail):
    case REG(stencil_front_op_zpass):

    case REG(stencil_back_op_fail):
    case REG(stencil_back_op_zfail):
    case REG(stencil_back_op_zpass): {
        const auto arg = *(Regs::StencilOp*)(&entry.arg);
        switch (arg) {
        case Regs::StencilOp::Keep:
        case Regs::StencilOp::KeepOGL:
            return "Keep";
        case Regs::StencilOp::Zero:
        case Regs::StencilOp::ZeroOGL:
            return "Zero";
        case Regs::StencilOp::Replace:
        case Regs::StencilOp::ReplaceOGL:
            return "Replace";
        case Regs::StencilOp::Incr:
        case Regs::StencilOp::IncrOGL:
            return "Incr";
        case Regs::StencilOp::Decr:
        case Regs::StencilOp::DecrOGL:
            return "Decr";
        case Regs::StencilOp::Invert:
        case Regs::StencilOp::InvertOGL:
            return "Invert";
        case Regs::StencilOp::IncrWrap:
        case Regs::StencilOp::IncrWrapOGL:
            return "IncrWrap";
        case Regs::StencilOp::DecrWrap:
        case Regs::StencilOp::DecrWrapOGL:
            return "DecrWrap";
        }
        break;
    }

    case REG(frag_color_clamp):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(screen_y_control): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 1));
        }
        break;
    }

    case REG(line_width_smooth):
    case REG(line_width_aliased):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(clip_distance_enabled):
    case REG(samplecnt_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(point_size):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));
    case REG(point_sprite_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(counter_reset): {
        const auto arg = *(Regs::CounterReset*)(&entry.arg);
        switch (arg) {
        case Regs::CounterReset::SampleCnt:
            return "SampleCnt";
        case Regs::CounterReset::Unk02:
            return "Unk02";
        case Regs::CounterReset::Unk03:
            return "Unk03";
        case Regs::CounterReset::Unk04:
            return "Unk04";
        case Regs::CounterReset::EmittedPrimitives:
            return "EmittedPrimitives";
        case Regs::CounterReset::Unk11:
            return "Unk11";
        case Regs::CounterReset::Unk12:
            return "Unk12";
        case Regs::CounterReset::Unk13:
            return "Unk13";
        case Regs::CounterReset::Unk15:
            return "Unk15";
        case Regs::CounterReset::Unk16:
            return "Unk16";
        case Regs::CounterReset::Unk17:
            return "Unk17";
        case Regs::CounterReset::Unk18:
            return "Unk18";
        case Regs::CounterReset::Unk1A:
            return "Unk1A";
        case Regs::CounterReset::Unk1B:
            return "Unk1B";
        case Regs::CounterReset::Unk1C:
            return "Unk1C";
        case Regs::CounterReset::Unk1D:
            return "Unk1D";
        case Regs::CounterReset::Unk1E:
            return "Unk1E";
        case Regs::CounterReset::GeneratedPrimitives:
            return "GeneratedPrimitives";
        }
        break;
    }

    case REG(multisample_enable):
    case REG(zeta_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(multisample_control): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 1));
        }
        break;
    }

    case REG(condition.mode): {
        const auto arg = *(Regs::ConditionMode*)(&entry.arg);
        switch (arg) {
        case Regs::ConditionMode::Never:
            return "Never";
        case Regs::ConditionMode::Always:
            return "Always";
        case Regs::ConditionMode::ResNonZero:
            return "ResNonZero";
        case Regs::ConditionMode::Equal:
            return "Equal";
        case Regs::ConditionMode::NotEqual:
            return "NotEqual";
        }
        break;
    }

    case REG(polygon_offset_factor):
    case REG(polygon_offset_units):
    case REG(polygon_offset_clamp):
        return fmt::format("{:.02f}f", *(f32*)(&entry.arg));

    case REG(line_smooth_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(stencil_two_side_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(framebuffer_srgb):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(multisample_mode): {
        const auto arg = *(Tegra::Texture::MsaaMode*)(&entry.arg);
        switch (arg) {
        case Tegra::Texture::MsaaMode::Msaa1x1:
            return "Msaa1x1";
        case Tegra::Texture::MsaaMode::Msaa2x1:
            return "Msaa2x1";
        case Tegra::Texture::MsaaMode::Msaa2x2:
            return "Msaa2x2";
        case Tegra::Texture::MsaaMode::Msaa4x2:
            return "Msaa4x2";
        case Tegra::Texture::MsaaMode::Msaa4x2_D3D:
            return "Msaa4x2_D3D";
        case Tegra::Texture::MsaaMode::Msaa2x1_D3D:
            return "Msaa2x1_D3D";
        case Tegra::Texture::MsaaMode::Msaa4x4:
            return "Msaa4x4";
        case Tegra::Texture::MsaaMode::Msaa2x2_VC4:
            return "Msaa2x2_VC4";
        case Tegra::Texture::MsaaMode::Msaa2x2_VC12:
            return "Msaa2x2_VC12";
        case Tegra::Texture::MsaaMode::Msaa4x2_VC8:
            return "Msaa4x2_VC8";
        case Tegra::Texture::MsaaMode::Msaa4x2_VC24:
            return "Msaa4x2_VC24";
        }
        break;
    }

    case REG(point_coord_replace): {
        switch (i) {
        case 0:
            return (entry.arg >> 2) & 1 ? "UpperLeft" : "LowerLeft";
        case 1:
            return fmt::format("0x{:X}", (entry.arg >> 3) & 0x3FF);
        }
        break;
    }

    case REG(draw.vertex_begin_gl): {
        switch (i) {
        case 0:
            return fmt::format("0x{:X}", entry.arg);
        case 1: {
            const auto arg = *(Regs::PrimitiveTopology*)(&entry.arg);
            switch (arg) {
            case Regs::PrimitiveTopology::Points:
                return "Points";
            case Regs::PrimitiveTopology::Lines:
                return "Lines";
            case Regs::PrimitiveTopology::LineLoop:
                return "LineLoop";
            case Regs::PrimitiveTopology::LineStrip:
                return "LineStrip";
            case Regs::PrimitiveTopology::Triangles:
                return "Triangles";
            case Regs::PrimitiveTopology::TriangleStrip:
                return "TriangleStrip";
            case Regs::PrimitiveTopology::TriangleFan:
                return "TriangleFan";
            case Regs::PrimitiveTopology::Quads:
                return "Quads";
            case Regs::PrimitiveTopology::QuadStrip:
                return "QuadStrip";
            case Regs::PrimitiveTopology::Polygon:
                return "Polygon";
            case Regs::PrimitiveTopology::LinesAdjacency:
                return "LinesAdjacency";
            case Regs::PrimitiveTopology::LineStripAdjacency:
                return "LineStripAdjacency";
            case Regs::PrimitiveTopology::TrianglesAdjacency:
                return "TrianglesAdjacency";
            case Regs::PrimitiveTopology::TriangleStripAdjacency:
                return "TriangleStripAdjacency";
            case Regs::PrimitiveTopology::Patches:
                return "Patches";
            }
            break;
        }
        case 2:
            return fmt::format("{}", (entry.arg >> 26) & 1);
        case 3:
            return fmt::format("{}", (entry.arg >> 27) & 1);
        }
        break;
    }

    case REG(primitive_restart.enabled):
        return fmt::format("{}", static_cast<bool>(entry.arg));
    case REG(primitive_restart.index):
        return fmt::format("{}", entry.arg);

    case REG(index_array.format): {
        const auto arg = *(Regs::IndexFormat*)(&entry.arg);
        switch (arg) {
        case Regs::IndexFormat::UnsignedByte:
            return "UnsignedByte";
        case Regs::IndexFormat::UnsignedShort:
            return "UnsignedShort";
        case Regs::IndexFormat::UnsignedInt:
            return "UnsignedInt";
        }
        break;
    }

    case REG(index_array.first):
    case REG(index_array.count):
        return fmt::format("{}", entry.arg);

    case REG(instanced_arrays.is_instanced):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(vp_point_size): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 0xFF));
        }
        break;
    }

    case REG(cull_test_enabled):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(front_face): {
        const auto arg = *(Regs::FrontFace*)(&entry.arg);
        switch (arg) {
        case Regs::FrontFace::ClockWise:
            return "ClockWise";
        case Regs::FrontFace::CounterClockWise:
            return "CounterClockWise";
        }
        break;
    }
    case REG(cull_face): {
        const auto arg = *(Regs::CullFace*)(&entry.arg);
        switch (arg) {
        case Regs::CullFace::Front:
            return "Front";
        case Regs::CullFace::Back:
            return "Back";
        case Regs::CullFace::FrontAndBack:
            return "FrontAndBack";
        }
        break;
    }

    case REG(pixel_center_integer):
        return fmt::format("{}", entry.arg);

    case REG(viewport_transform_enabled):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(view_volume_clip_control): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 3) & 1));
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 1));
        case 3:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 11) & 1));
        }
        break;
    }

    case REG(depth_bounds_enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));

    case REG(logic_op.enable):
        return fmt::format("{}", static_cast<bool>(entry.arg));
    case REG(logic_op.operation): {
        const auto arg = *(Regs::LogicOperation*)(&entry.arg);
        switch (arg) {
        case Regs::LogicOperation::Clear:
            return "Clear";
        case Regs::LogicOperation::And:
            return "And";
        case Regs::LogicOperation::AndReverse:
            return "AndReverse";
        case Regs::LogicOperation::Copy:
            return "Copy";
        case Regs::LogicOperation::AndInverted:
            return "AndInverted";
        case Regs::LogicOperation::NoOp:
            return "NoOp";
        case Regs::LogicOperation::Xor:
            return "Xor";
        case Regs::LogicOperation::Or:
            return "Or";
        case Regs::LogicOperation::Nor:
            return "Nor";
        case Regs::LogicOperation::Equiv:
            return "Equiv";
        case Regs::LogicOperation::Invert:
            return "Invert";
        case Regs::LogicOperation::OrReverse:
            return "OrReverse";
        case Regs::LogicOperation::CopyInverted:
            return "CopyInverted";
        case Regs::LogicOperation::OrInverted:
            return "OrInverted";
        case Regs::LogicOperation::Nand:
            return "Nand";
        case Regs::LogicOperation::Set:
            return "Set";
        }
        break;
    }
    case REG(clear_buffers): {
        switch (i) {
        case 0:
            return fmt::format("0x{:X}", entry.arg);
        case 1:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 1) & 1));
        case 3:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 2) & 1));
        case 4:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 3) & 1));
        case 5:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 1));
        case 6:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 5) & 1));
        case 7:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 6) & 0xF));
        case 8:
            return fmt::format("0x{:X}", (entry.arg >> 10) & 0x7FF);
        }
        break;
    }

    case REG(color_mask): {
        switch (i) {
        case 0:
            return fmt::format("0x{:X}", entry.arg);
        case 1:
            return fmt::format("{}", static_cast<bool>(entry.arg & 0xF));
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 0xF));
        case 3:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 8) & 0xF));
        case 4:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 12) & 0xF));
        }
        break;
    }

    case REG(query.query_get): {
        switch (i) {
        case 0:
            return fmt::format("0x{:X}", entry.arg);
        case 1: {
            u32 temp = entry.arg & 0x3;
            const auto arg = *(Regs::QueryOperation*)(&temp);
            switch (arg) {
            case Regs::QueryOperation::Release:
                return "Release";
            case Regs::QueryOperation::Acquire:
                return "Acquire";
            case Regs::QueryOperation::Counter:
                return "Counter";
            case Regs::QueryOperation::Trap:
                return "Trap";
            }
            break;
        }
        case 2:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 4) & 1));
        case 3: {
            u32 temp = (entry.arg >> 12) & 0xF;
            const auto arg = *(Regs::QueryUnit*)(&temp);
            switch (arg) {
            case Regs::QueryUnit::VFetch:
                return "VFetch";
            case Regs::QueryUnit::VP:
                return "VP";
            case Regs::QueryUnit::Rast:
                return "Rast";
            case Regs::QueryUnit::StrmOut:
                return "StrmOut";
            case Regs::QueryUnit::GP:
                return "GP";
            case Regs::QueryUnit::ZCull:
                return "ZCull";
            case Regs::QueryUnit::Prop:
                return "Prop";
            case Regs::QueryUnit::Crop:
                return "Crop";
            }
            break;
        }
        case 4: {
            u32 temp = (entry.arg >> 16) & 1;
            const auto arg = *(Regs::QuerySyncCondition*)(&temp);
            switch (arg) {
            case Regs::QuerySyncCondition::NotEqual:
                return "NotEqual";
            case Regs::QuerySyncCondition::GreaterThan:
                return "GreaterThan";
            }
            break;
        }
        case 5: {
            u32 temp = (entry.arg >> 23) & 0x1F;
            const auto arg = *(Regs::QuerySelect*)(&temp);
            switch (arg) {
            case Regs::QuerySelect::Zero:
                return "Zero";
            case Regs::QuerySelect::TimeElapsed:
                return "TimeElapsed";
            case Regs::QuerySelect::TransformFeedbackPrimitivesGenerated:
                return "TransformFeedbackPrimitivesGenerated";
            case Regs::QuerySelect::PrimitivesGenerated:
                return "PrimitivesGenerated";
            case Regs::QuerySelect::SamplesPassed:
                return "SamplesPassed";
            case Regs::QuerySelect::TransformFeedbackUnknown:
                return "TransformFeedbackUnknown";
            }
            break;
        }
        case 6:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 28) & 1));
        }
        break;
    }

    case REG(vertex_array[0].stride): {
        switch (i) {
        case 0:
            return fmt::format("{}", entry.arg & 0xFFF);
        case 1:
            return fmt::format("{}", static_cast<bool>((entry.arg >> 12) & 1));
        }
        break;
    }
    case REG(vertex_array[0].divisor):
        return fmt::format("{}", entry.arg);

    case REG(shader_config[0].enable): {
        switch (i) {
        case 0:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 1: {
            u32 temp = (entry.arg >> 4) & 0xF;
            const auto arg = *(Regs::ShaderProgram*)(&temp);
            switch (arg) {
            case Regs::ShaderProgram::VertexA:
                return "VertexA";
            case Regs::ShaderProgram::VertexB:
                return "VertexB";
            case Regs::ShaderProgram::TesselationControl:
                return "TesselationControl";
            case Regs::ShaderProgram::TesselationEval:
                return "TesselationEval";
            case Regs::ShaderProgram::Geometry:
                return "Geometry";
            case Regs::ShaderProgram::Fragment:
                return "Fragment";
            }
            break;
        }
        }
        break;
    }

    case REG(cb_bind[0].raw_config): {
        switch (i) {
        case 0:
            return fmt::format("0x{:X}", entry.arg);
        case 1:
            return fmt::format("{}", static_cast<bool>(entry.arg & 1));
        case 2:
            return fmt::format("{}", (entry.arg >> 4) & 0x1F);
        }
        break;
    }

    case REG(tex_cb_index):
        return fmt::format("{}", entry.arg);
    }

    return fmt::format("0x{:X}", entry.arg);
}

std::string Record::GetKeplerComputeArg(GPU::RecordEntry& entry, REG_LIST::const_iterator method,
                                        size_t i) {
    return "";
}

std::string Record::GetKeplerMemoryArg(GPU::RecordEntry& entry, REG_LIST::const_iterator method,
                                       size_t i) {
    return "";
}

std::string Record::GetMaxwellDMAArg(GPU::RecordEntry& entry, REG_LIST::const_iterator method,
                                     size_t i) {
    return "";
}

[[nodiscard]] static constexpr REG_LIST BuildFermiMethods() {
    return {};
}

[[nodiscard]] static constexpr REG_LIST BuildMaxwellMethods() {
    return {{
        {0x0000, 0x44, 0x01, 0x0000, 0x01, 0x01, "unk_0000(OFFSET)"},
        {0x0044, 0x01, 0x01, 0x0044, 0x01, 0x01, "wait_for_idle"},
        {0x0045, 0x01, 0x01, 0x0045, 0x01, 0x04, "macros.upload_address"},
        {0x0046, 0x01, 0x01, 0x0045, 0x01, 0x04, "macros.data"},
        {0x0047, 0x01, 0x01, 0x0045, 0x01, 0x04, "macros.entry"},
        {0x0048, 0x01, 0x01, 0x0045, 0x01, 0x04, "macros.bind"},
        {0x0049, 0x01, 0x01, 0x0049, 0x01, 0x01, "shadow_ram_control"},
        {0x004A, 0x16, 0x01, 0x004A, 0x01, 0x01, "unk_004A(OFFSET)"},
        {0x0060, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.line_length_in"},
        {0x0061, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.line_count"},
        {0x0062, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.address_high"},
        {0x0063, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.address_low"},
        {0x0064, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.pitch"},
        {0x0065, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.block_width"},
        {0x0065, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.block_height"},
        {0x0065, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.block_depth"},
        {0x0066, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.width"},
        {0x0067, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.height"},
        {0x0068, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.depth"},
        {0x0069, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.z"},
        {0x006A, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.x"},
        {0x006B, 0x01, 0x01, 0x0060, 0x01, 0x0C, "upload.dest.y"},
        {0x006C, 0x01, 0x01, 0x006C, 0x01, 0x01, "exec_upload.linear"},
        {0x006D, 0x01, 0x01, 0x006D, 0x01, 0x01, "data_upload"},
        {0x006E, 0x16, 0x01, 0x006E, 0x01, 0x01, "unk_006E(OFFSET)"},
        {0x0084, 0x01, 0x01, 0x0084, 0x01, 0x01, "force_early_fragment_tests"},
        {0x0085, 0x2D, 0x01, 0x0085, 0x01, 0x01, "unk_0085(OFFSET)"},
        {0x00B2, 0x01, 0x01, 0x00B2, 0x01, 0x01, "sync_info.sync_point"},
        {0x00B2, 0x01, 0x01, 0x00B2, 0x01, 0x01, "sync_info.unknown"},
        {0x00B2, 0x01, 0x01, 0x00B2, 0x01, 0x01, "sync_info.increment"},
        {0x00B3, 0x15, 0x01, 0x00B3, 0x01, 0x01, "unk_00B3(OFFSET)"},
        {0x00C8, 0x01, 0x01, 0x00C8, 0x01, 0x01, "tess_mode.prim"},
        {0x00C8, 0x01, 0x01, 0x00C8, 0x01, 0x01, "tess_mode.spacing"},
        {0x00C8, 0x01, 0x01, 0x00C8, 0x01, 0x01, "tess_mode.cw"},
        {0x00C8, 0x01, 0x01, 0x00C8, 0x01, 0x01, "tess_mode.connected"},
        {0x00C9, 0x04, 0x01, 0x00C9, 0x01, 0x01, "tess_level_outer(OFFSET)"},
        {0x00CD, 0x02, 0x01, 0x00CD, 0x01, 0x01, "tess_level_inner(OFFSET)"},
        {0x00CF, 0x10, 0x01, 0x00CF, 0x01, 0x01, "unk_00CF(OFFSET)"},
        {0x00DF, 0x01, 0x01, 0x00DF, 0x01, 0x01, "rasterize_enable"},
        {0x00E0, 0x01, 0x01, 0x00E0, 0x04, 0x08, "tfb_bindings(OFFSET).buffer_enable"},
        {0x00E1, 0x01, 0x01, 0x00E0, 0x04, 0x08, "tfb_bindings(OFFSET).address_high"},
        {0x00E2, 0x01, 0x01, 0x00E0, 0x04, 0x08, "tfb_bindings(OFFSET).address_low"},
        {0x00E3, 0x01, 0x01, 0x00E0, 0x04, 0x08, "tfb_bindings(OFFSET).buffer_size"},
        {0x00E4, 0x01, 0x01, 0x00E0, 0x04, 0x08, "tfb_bindings(OFFSET).buffer_offset"},
        {0x00E5, 0x03, 0x01, 0x00E0, 0x04, 0x08, "tfb_bindings(OFFSET).unk_00E5(OFFSET)"},
        {0x0100, 0xC0, 0x01, 0x0100, 0x01, 0x01, "unk_0100(OFFSET)"},
        {0x01C0, 0x01, 0x01, 0x01C0, 0x04, 0x04, "tfb_layouts(OFFSET).stream"},
        {0x01C1, 0x01, 0x01, 0x01C0, 0x04, 0x04, "tfb_layouts(OFFSET).varying_count"},
        {0x01C2, 0x01, 0x01, 0x01C0, 0x04, 0x04, "tfb_layouts(OFFSET).stride"},
        {0x01C3, 0x01, 0x01, 0x01C0, 0x04, 0x04, "tfb_layouts(OFFSET).unk_01C3"},
        {0x01D0, 0x01, 0x01, 0x01D0, 0x01, 0x01, "unk_01D0(OFFSET)"},
        {0x01D1, 0x01, 0x01, 0x01D1, 0x01, 0x01, "tfb_enabled"},
        {0x01D2, 0x2E, 0x01, 0x01D2, 0x01, 0x01, "unk_01D2(OFFSET)"},
        {0x0200, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).address_high"},
        {0x0201, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).address_low"},
        {0x0202, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).width"},
        {0x0203, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).height"},
        {0x0204, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).format"},
        {0x0205, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).tile_mode.block_width"},
        {0x0205, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).tile_mode.block_height"},
        {0x0205, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).tile_mode.block_depth"},
        {0x0205, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).tile_mode.is_pitch_linear"},
        {0x0205, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).tile_mode.is_3d"},
        {0x0206, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).depth"},
        {0x0206, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).volume"},
        {0x0207, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).layer_stride"},
        {0x0208, 0x01, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).base_layer"},
        {0x0209, 0x07, 0x01, 0x0200, 0x08, 0x10, "rt(OFFSET).unk_0209(OFFSET)"},
        {0x0280, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).scale_x"},
        {0x0281, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).scale_y"},
        {0x0282, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).scale_z"},
        {0x0283, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).translate_x"},
        {0x0284, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).translate_y"},
        {0x0285, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).translate_z"},
        {0x0286, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).swizzle.raw"},
        {0x0286, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).swizzle.x"},
        {0x0286, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).swizzle.y"},
        {0x0286, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).swizzle.z"},
        {0x0286, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).swizzle.w"},
        {0x0287, 0x01, 0x01, 0x0280, 0x10, 0x08, "viewport_transform(OFFSET).unk_0287"},
        {0x0300, 0x01, 0x01, 0x0300, 0x10, 0x04, "viewports(OFFSET).x"},
        {0x0300, 0x01, 0x01, 0x0300, 0x10, 0x04, "viewports(OFFSET).width"},
        {0x0301, 0x01, 0x01, 0x0300, 0x10, 0x04, "viewports(OFFSET).y"},
        {0x0301, 0x01, 0x01, 0x0300, 0x10, 0x04, "viewports(OFFSET).height"},
        {0x0302, 0x01, 0x01, 0x0300, 0x10, 0x04, "viewports(OFFSET).depth_range_near"},
        {0x0303, 0x01, 0x01, 0x0300, 0x10, 0x04, "viewports(OFFSET).depth_range_far"},
        {0x0340, 0x1D, 0x01, 0x0340, 0x01, 0x01, "unk_0340(OFFSET)"},
        {0x035D, 0x01, 0x01, 0x035D, 0x01, 0x02, "vertex_buffer.first"},
        {0x035E, 0x01, 0x01, 0x035D, 0x01, 0x02, "vertex_buffer.count"},
        {0x035F, 0x01, 0x01, 0x035F, 0x01, 0x01, "depth_mode"},
        {0x0360, 0x04, 0x01, 0x0360, 0x01, 0x01, "clear_color(OFFSET)"},
        {0x0364, 0x01, 0x01, 0x0364, 0x01, 0x01, "clear_depth"},
        {0x0365, 0x03, 0x01, 0x0365, 0x01, 0x01, "unk_0365(OFFSET)"},
        {0x0368, 0x01, 0x01, 0x0368, 0x01, 0x01, "clear_stencil"},
        {0x0369, 0x02, 0x01, 0x0369, 0x01, 0x01, "unk_0369(OFFSET)"},
        {0x036B, 0x01, 0x01, 0x036B, 0x01, 0x01, "polygon_mode_front"},
        {0x036C, 0x01, 0x01, 0x036C, 0x01, 0x01, "polygon_mode_back"},
        {0x036D, 0x03, 0x01, 0x036D, 0x01, 0x01, "unk_036D(OFFSET)"},
        {0x0370, 0x01, 0x01, 0x0370, 0x01, 0x01, "polygon_offset_point_enable"},
        {0x0371, 0x01, 0x01, 0x0371, 0x01, 0x01, "polygon_offset_line_enable"},
        {0x0372, 0x01, 0x01, 0x0372, 0x01, 0x01, "polygon_offset_fill_enable"},
        {0x0373, 0x01, 0x01, 0x0373, 0x01, 0x01, "patch_vertices"},
        {0x0374, 0x04, 0x01, 0x0374, 0x01, 0x01, "unk_0374(OFFSET)"},
        {0x0378, 0x01, 0x01, 0x0378, 0x01, 0x01, "fragment_barrier"},
        {0x0379, 0x07, 0x01, 0x0379, 0x01, 0x01, "unk_0379(OFFSET)"},
        {0x0380, 0x01, 0x01, 0x0380, 0x10, 0x04, "scissor_test(OFFSET).enable"},
        {0x0381, 0x01, 0x01, 0x0380, 0x10, 0x04, "scissor_test(OFFSET).min_x"},
        {0x0381, 0x01, 0x01, 0x0380, 0x10, 0x04, "scissor_test(OFFSET).max_x"},
        {0x0382, 0x01, 0x01, 0x0380, 0x10, 0x04, "scissor_test(OFFSET).min_y"},
        {0x0382, 0x01, 0x01, 0x0380, 0x10, 0x04, "scissor_test(OFFSET).max_y"},
        {0x0383, 0x01, 0x01, 0x0380, 0x10, 0x04, "scissor_test(OFFSET).fill"},
        {0x03C0, 0x15, 0x01, 0x03C0, 0x01, 0x01, "unk_03C0(OFFSET)"},
        {0x03D5, 0x01, 0x01, 0x03D5, 0x01, 0x01, "stencil_back_func_ref"},
        {0x03D6, 0x01, 0x01, 0x03D6, 0x01, 0x01, "stencil_back_mask"},
        {0x03D7, 0x01, 0x01, 0x03D7, 0x01, 0x01, "stencil_back_func_mask"},
        {0x03D8, 0x05, 0x01, 0x03D8, 0x01, 0x01, "unk_03D8(OFFSET)"},
        {0x03DD, 0x01, 0x01, 0x03DD, 0x01, 0x01, "invalidate_texture_data_cache.lines"},
        {0x03DD, 0x01, 0x01, 0x03DD, 0x01, 0x01, "invalidate_texture_data_cache.tag"},
        {0x03DE, 0x01, 0x01, 0x03DE, 0x01, 0x01, "unk_03DE(OFFSET)"},
        {0x03DF, 0x01, 0x01, 0x03DF, 0x01, 0x01, "tiled_cache_barrier"},
        {0x03E0, 0x04, 0x01, 0x03E0, 0x01, 0x01, "unk_03E0(OFFSET)"},
        {0x03E4, 0x01, 0x01, 0x03E4, 0x01, 0x01, "color_mask_common"},
        {0x03E5, 0x02, 0x01, 0x03E5, 0x01, 0x01, "unk_03E5(OFFSET)"},
        {0x03E7, 0x02, 0x01, 0x03E7, 0x01, 0x01, "depth_bounds(OFFSET)"},
        {0x03E9, 0x02, 0x01, 0x03E9, 0x01, 0x01, "unk_03E9(OFFSET)"},
        {0x03EB, 0x01, 0x01, 0x03EB, 0x01, 0x01, "rt_separate_frag_data"},
        {0x03EC, 0x01, 0x01, 0x03EC, 0x01, 0x01, "unk_03EC(OFFSET)"},
        {0x03ED, 0x01, 0x01, 0x03ED, 0x01, 0x01, "multisample_raster_enable"},
        {0x03EE, 0x01, 0x01, 0x03EE, 0x01, 0x01, "multisample_raster_samples"},
        {0x03EF, 0x04, 0x01, 0x03EF, 0x01, 0x01, "multisample_sample_mask(OFFSET)"},
        {0x03F3, 0x05, 0x01, 0x03F3, 0x01, 0x01, "unk_03F3(OFFSET)"},
        {0x03F8, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.address_high"},
        {0x03F9, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.address_low"},
        {0x03FA, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.format"},
        {0x03FB, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.tile_mode.block_width"},
        {0x03FB, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.tile_mode.block_height"},
        {0x03FB, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.tile_mode.block_depth"},
        {0x03FB, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.tile_mode.is_pitch_linear"},
        {0x03FB, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.tile_mode.is_3d"},
        {0x03FC, 0x01, 0x01, 0x03F8, 0x01, 0x05, "zeta.layer_stride"},
        {0x03FD, 0x01, 0x01, 0x03FD, 0x01, 0x02, "render_area.x"},
        {0x03FD, 0x01, 0x01, 0x03FD, 0x01, 0x02, "render_area.width"},
        {0x03FE, 0x01, 0x01, 0x03FD, 0x01, 0x02, "render_area.y"},
        {0x03FE, 0x01, 0x01, 0x03FD, 0x01, 0x02, "render_area.height"},
        {0x03FF, 0x3F, 0x01, 0x03FF, 0x01, 0x01, "unk_03FF(OFFSET)"},
        {0x043E, 0x01, 0x01, 0x043E, 0x01, 0x01, "clear_flags.stencil"},
        {0x043E, 0x01, 0x01, 0x043E, 0x01, 0x01, "clear_flags.unknown"},
        {0x043E, 0x01, 0x01, 0x043E, 0x01, 0x01, "clear_flags.scissor"},
        {0x043E, 0x01, 0x01, 0x043E, 0x01, 0x01, "clear_flags.viewport"},
        {0x043F, 0x10, 0x01, 0x043F, 0x01, 0x01, "unk_043F(OFFSET)"},
        {0x044F, 0x01, 0x01, 0x044F, 0x01, 0x01, "fill_rectangle"},
        {0x0450, 0x08, 0x01, 0x0450, 0x01, 0x01, "unk_0450(OFFSET)"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).buffer"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).constant"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).offset"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).size"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).type"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).bgra"},
        {0x0458, 0x01, 0x01, 0x0458, 0x20, 0x01, "vertex_attrib_format(OFFSET).hex"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).x0"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).y0"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).x1"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).y1"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).x2"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).y2"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).x3"},
        {0x0478, 0x01, 0x01, 0x0478, 0x04, 0x01, "multisample_sample_locations(OFFSET).y3"},
        {0x047C, 0x02, 0x01, 0x047C, 0x01, 0x01, "unk_047C(OFFSET)"},
        {0x047E, 0x01, 0x01, 0x047E, 0x01, 0x01, "multisample_coverage_to_color.enable"},
        {0x047E, 0x01, 0x01, 0x047E, 0x01, 0x01, "multisample_coverage_to_color.target"},
        {0x047F, 0x08, 0x01, 0x047F, 0x01, 0x01, "unk_047F(OFFSET)"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.count"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_0"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_1"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_2"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_3"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_4"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_5"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_6"},
        {0x0487, 0x01, 0x01, 0x0487, 0x01, 0x01, "rt_control.map_7"},
        {0x0488, 0x02, 0x01, 0x0488, 0x01, 0x01, "unk_0488(OFFSET)"},
        {0x048A, 0x01, 0x01, 0x048A, 0x01, 0x01, "zeta_width"},
        {0x048B, 0x01, 0x01, 0x048B, 0x01, 0x01, "zeta_height"},
        {0x048C, 0x01, 0x01, 0x048C, 0x01, 0x01, "zeta_depth"},
        {0x048C, 0x01, 0x01, 0x048C, 0x01, 0x01, "zeta_volume"},
        {0x048D, 0x01, 0x01, 0x048D, 0x01, 0x01, "sampler_index"},
        {0x048E, 0x25, 0x01, 0x048E, 0x01, 0x01, "unk_048E(OFFSET)"},
        {0x04B3, 0x01, 0x01, 0x04B3, 0x01, 0x01, "depth_test_enable"},
        {0x04B4, 0x05, 0x01, 0x04B4, 0x01, 0x01, "unk_04B4(OFFSET)"},
        {0x04B9, 0x01, 0x01, 0x04B9, 0x01, 0x01, "independent_blend_enable"},
        {0x04BA, 0x01, 0x01, 0x04BA, 0x01, 0x01, "depth_write_enabled"},
        {0x04BB, 0x01, 0x01, 0x04BB, 0x01, 0x01, "alpha_test_enabled"},
        {0x04BC, 0x06, 0x01, 0x04BC, 0x01, 0x01, "unk_04BC(OFFSET)"},
        {0x04C2, 0x01, 0x01, 0x04C2, 0x01, 0x01, "d3d_cull_mode"},
        {0x04C3, 0x01, 0x01, 0x04C3, 0x01, 0x01, "depth_test_func"},
        {0x04C4, 0x01, 0x01, 0x04C4, 0x01, 0x01, "alpha_test_ref"},
        {0x04C5, 0x01, 0x01, 0x04C5, 0x01, 0x01, "alpha_test_func"},
        {0x04C6, 0x01, 0x01, 0x04C6, 0x01, 0x01, "draw_tfb_stride"},
        {0x04C7, 0x01, 0x01, 0x04C7, 0x01, 0x04, "blend_color.r"},
        {0x04C8, 0x01, 0x01, 0x04C7, 0x01, 0x04, "blend_color.g"},
        {0x04C9, 0x01, 0x01, 0x04C7, 0x01, 0x04, "blend_color.b"},
        {0x04CA, 0x01, 0x01, 0x04C7, 0x01, 0x04, "blend_color.a"},
        {0x04CB, 0x04, 0x01, 0x04CB, 0x01, 0x01, "unk_04CB(OFFSET)"},
        {0x04CF, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.separate_alpha"},
        {0x04D0, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.equation_rgb"},
        {0x04D1, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.factor_source_rgb"},
        {0x04D2, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.factor_dest_rgb"},
        {0x04D3, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.equation_a"},
        {0x04D4, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.factor_source_a"},
        {0x04D5, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.unk_04D5"},
        {0x04D6, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.factor_dest_a"},
        {0x04D7, 0x01, 0x01, 0x04CF, 0x01, 0x11, "blend.enable_common"},
        {0x04D8, 0x08, 0x01, 0x04CF, 0x01, 0x11, "blend.enable(OFFSET)"},
        {0x04E0, 0x01, 0x01, 0x04E0, 0x01, 0x01, "stencil_enable"},
        {0x04E1, 0x01, 0x01, 0x04E1, 0x01, 0x01, "stencil_front_op_fail"},
        {0x04E2, 0x01, 0x01, 0x04E2, 0x01, 0x01, "stencil_front_op_zfail"},
        {0x04E3, 0x01, 0x01, 0x04E3, 0x01, 0x01, "stencil_front_op_zpass"},
        {0x04E4, 0x01, 0x01, 0x04E4, 0x01, 0x01, "stencil_front_func_func"},
        {0x04E5, 0x01, 0x01, 0x04E5, 0x01, 0x01, "stencil_front_func_ref"},
        {0x04E6, 0x01, 0x01, 0x04E6, 0x01, 0x01, "stencil_front_func_mask"},
        {0x04E7, 0x01, 0x01, 0x04E7, 0x01, 0x01, "stencil_front_mask"},
        {0x04E8, 0x02, 0x01, 0x04E8, 0x01, 0x01, "unk_04E8(OFFSET)"},
        {0x04EA, 0x01, 0x01, 0x04EA, 0x01, 0x01, "frag_color_clamp"},
        {0x04EB, 0x01, 0x01, 0x04EB, 0x01, 0x01, "screen_y_control.y_negate"},
        {0x04EB, 0x01, 0x01, 0x04EB, 0x01, 0x01, "screen_y_control.triangle_rast_flip"},
        {0x04EC, 0x01, 0x01, 0x04EC, 0x01, 0x01, "line_width_smooth"},
        {0x04ED, 0x01, 0x01, 0x04ED, 0x01, 0x01, "line_width_aliased"},
        {0x04EE, 0x1B, 0x01, 0x04EE, 0x01, 0x01, "unk_04EE(OFFSET)"},
        {0x0509, 0x01, 0x01, 0x0509, 0x01, 0x01, "invalidate_sampler_cache_no_wfi.lines"},
        {0x0509, 0x01, 0x01, 0x0509, 0x01, 0x01, "invalidate_sampler_cache_no_wfi.tag"},
        {0x050A, 0x01, 0x01, 0x050A, 0x01, 0x01, "invalidate_texture_header_cache_no_wfi.lines"},
        {0x050A, 0x01, 0x01, 0x050A, 0x01, 0x01, "invalidate_texture_header_cache_no_wfi.tag"},
        {0x050B, 0x02, 0x01, 0x050B, 0x01, 0x01, "unk_050B(OFFSET)"},
        {0x050D, 0x01, 0x01, 0x050D, 0x01, 0x01, "vb_element_base"},
        {0x050E, 0x01, 0x01, 0x050E, 0x01, 0x01, "vb_base_instance"},
        {0x050F, 0x35, 0x01, 0x050F, 0x01, 0x01, "unk_050F(OFFSET)"},
        {0x0544, 0x01, 0x01, 0x0544, 0x01, 0x01, "clip_distance_enabled"},
        {0x0545, 0x01, 0x01, 0x0545, 0x01, 0x01, "samplecnt_enable"},
        {0x0546, 0x01, 0x01, 0x0546, 0x01, 0x01, "point_size"},
        {0x0547, 0x01, 0x01, 0x0547, 0x01, 0x01, "unk_0547(OFFSET)"},
        {0x0548, 0x01, 0x01, 0x0548, 0x01, 0x01, "point_sprite_enable"},
        {0x0549, 0x03, 0x01, 0x0549, 0x01, 0x01, "unk_0549(OFFSET)"},
        {0x054C, 0x01, 0x01, 0x054C, 0x01, 0x01, "counter_reset"},
        {0x054D, 0x01, 0x01, 0x054D, 0x01, 0x01, "multisample_enable"},
        {0x054E, 0x01, 0x01, 0x054E, 0x01, 0x01, "zeta_enable"},
        {0x054F, 0x01, 0x01, 0x054F, 0x01, 0x01, "multisample_control.alpha_to_coverage"},
        {0x054F, 0x01, 0x01, 0x054F, 0x01, 0x01, "multisample_control.alpha_to_one"},
        {0x0550, 0x04, 0x01, 0x0550, 0x01, 0x01, "unk_0550(OFFSET)"},
        {0x0554, 0x01, 0x01, 0x0554, 0x01, 0x03, "condition.address_high"},
        {0x0555, 0x01, 0x01, 0x0554, 0x01, 0x03, "condition.address_low"},
        {0x0556, 0x01, 0x01, 0x0554, 0x01, 0x03, "condition.mode"},
        {0x0557, 0x01, 0x01, 0x0557, 0x01, 0x03, "tsc.address_high"},
        {0x0558, 0x01, 0x01, 0x0557, 0x01, 0x03, "tsc.address_low"},
        {0x0559, 0x01, 0x01, 0x0557, 0x01, 0x03, "tsc.limit"},
        {0x055A, 0x01, 0x01, 0x055A, 0x01, 0x01, "unk_055A(OFFSET)"},
        {0x055B, 0x01, 0x01, 0x055B, 0x01, 0x01, "polygon_offset_factor"},
        {0x055C, 0x01, 0x01, 0x055C, 0x01, 0x01, "line_smooth_enable"},
        {0x055D, 0x01, 0x01, 0x055D, 0x01, 0x03, "tic.address_high"},
        {0x055E, 0x01, 0x01, 0x055D, 0x01, 0x03, "tic.address_low"},
        {0x055F, 0x01, 0x01, 0x055D, 0x01, 0x03, "tic.limit"},
        {0x0560, 0x05, 0x01, 0x0560, 0x01, 0x01, "unk_0560(OFFSET)"},
        {0x0565, 0x01, 0x01, 0x0565, 0x01, 0x01, "stencil_two_side_enable"},
        {0x0566, 0x01, 0x01, 0x0566, 0x01, 0x01, "stencil_back_op_fail"},
        {0x0567, 0x01, 0x01, 0x0567, 0x01, 0x01, "stencil_back_op_zfail"},
        {0x0568, 0x01, 0x01, 0x0568, 0x01, 0x01, "stencil_back_op_zpass"},
        {0x0569, 0x01, 0x01, 0x0569, 0x01, 0x01, "stencil_back_func_func"},
        {0x056A, 0x04, 0x01, 0x056A, 0x01, 0x01, "unk_056A(OFFSET)"},
        {0x056E, 0x01, 0x01, 0x056E, 0x01, 0x01, "framebuffer_srgb"},
        {0x056F, 0x01, 0x01, 0x056F, 0x01, 0x01, "polygon_offset_units"},
        {0x0570, 0x04, 0x01, 0x0570, 0x01, 0x01, "unk_0570(OFFSET)"},
        {0x0574, 0x01, 0x01, 0x0574, 0x01, 0x01, "multisample_mode"},
        {0x0575, 0x0C, 0x01, 0x0575, 0x01, 0x01, "unk_0575(OFFSET)"},
        {0x0581, 0x01, 0x01, 0x0581, 0x01, 0x01, "point_coord_replace.coord_origin"},
        {0x0581, 0x01, 0x01, 0x0581, 0x01, 0x01, "point_coord_replace.enable"},
        {0x0582, 0x01, 0x01, 0x0582, 0x01, 0x02, "code_address.code_address_high"},
        {0x0583, 0x01, 0x01, 0x0582, 0x01, 0x02, "code_address.code_address_low"},
        {0x0584, 0x01, 0x01, 0x0584, 0x01, 0x01, "unk_0584(OFFSET)"},
        {0x0585, 0x01, 0x01, 0x0585, 0x01, 0x02, "draw.vertex_end_gl"},
        {0x0586, 0x01, 0x01, 0x0585, 0x01, 0x02, "draw.vertex_begin_gl"},
        {0x0586, 0x01, 0x01, 0x0585, 0x01, 0x02, "draw.topology"},
        {0x0586, 0x01, 0x01, 0x0585, 0x01, 0x02, "draw.instance_next"},
        {0x0586, 0x01, 0x01, 0x0585, 0x01, 0x02, "draw.instance_cont"},
        {0x0587, 0x0A, 0x01, 0x0587, 0x01, 0x01, "unk_0587(OFFSET)"},
        {0x0591, 0x01, 0x01, 0x0591, 0x01, 0x02, "primitive_restart.enabled"},
        {0x0592, 0x01, 0x01, 0x0591, 0x01, 0x02, "primitive_restart.index"},
        {0x0593, 0x5F, 0x01, 0x0593, 0x01, 0x01, "unk_0593(OFFSET)"},
        {0x05F2, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.start_addr_high"},
        {0x05F3, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.start_addr_low"},
        {0x05F4, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.end_addr_high"},
        {0x05F5, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.end_addr_low"},
        {0x05F6, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.format"},
        {0x05F7, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.first"},
        {0x05F8, 0x01, 0x01, 0x05F2, 0x01, 0x07, "index_array.count"},
        {0x05F9, 0x26, 0x01, 0x05F9, 0x01, 0x01, "unk_05F9(OFFSET)"},
        {0x061F, 0x01, 0x01, 0x061F, 0x01, 0x01, "polygon_offset_clamp"},
        {0x0620, 0x20, 0x01, 0x0620, 0x01, 0x20, "instanced_arrays.is_instanced(OFFSET)"},
        {0x0640, 0x04, 0x01, 0x0640, 0x01, 0x01, "unk_0640(OFFSET)"},
        {0x0644, 0x01, 0x01, 0x0644, 0x01, 0x01, "vp_point_size.enable"},
        {0x0644, 0x01, 0x01, 0x0644, 0x01, 0x01, "vp_point_size.unk4"},
        {0x0645, 0x01, 0x01, 0x0645, 0x01, 0x01, "unk_0645(OFFSET)"},
        {0x0646, 0x01, 0x01, 0x0646, 0x01, 0x01, "cull_test_enabled"},
        {0x0647, 0x01, 0x01, 0x0647, 0x01, 0x01, "front_face"},
        {0x0648, 0x01, 0x01, 0x0648, 0x01, 0x01, "cull_face"},
        {0x0649, 0x01, 0x01, 0x0649, 0x01, 0x01, "pixel_center_integer"},
        {0x064A, 0x01, 0x01, 0x064A, 0x01, 0x01, "unk_064A(OFFSET)"},
        {0x064B, 0x01, 0x01, 0x064B, 0x01, 0x01, "viewport_transform_enabled"},
        {0x064C, 0x03, 0x01, 0x064C, 0x01, 0x01, "unk_064C(OFFSET)"},
        {0x064F, 0x01, 0x01, 0x064F, 0x01, 0x01, "view_volume_clip_control.depth_range_0_1"},
        {0x064F, 0x01, 0x01, 0x064F, 0x01, 0x01, "view_volume_clip_control.depth_clamp_near"},
        {0x064F, 0x01, 0x01, 0x064F, 0x01, 0x01, "view_volume_clip_control.depth_clamp_far"},
        {0x064F, 0x01, 0x01, 0x064F, 0x01, 0x01, "view_volume_clip_control.depth_clamp_disabled"},
        {0x0650, 0x1F, 0x01, 0x0650, 0x01, 0x01, "unk_0650(OFFSET)"},
        {0x066F, 0x01, 0x01, 0x066F, 0x01, 0x01, "depth_bounds_enable"},
        {0x0670, 0x01, 0x01, 0x0670, 0x01, 0x01, "unk_0670(OFFSET)"},
        {0x0671, 0x01, 0x01, 0x0671, 0x01, 0x02, "logic_op.enable"},
        {0x0672, 0x01, 0x01, 0x0671, 0x01, 0x02, "logic_op.operation"},
        {0x0673, 0x01, 0x01, 0x0673, 0x01, 0x01, "unk_0673(OFFSET)"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.raw"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.Z"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.S"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.R"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.G"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.B"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.A"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.RT"},
        {0x0674, 0x01, 0x01, 0x0674, 0x01, 0x01, "clear_buffers.layer"},
        {0x0675, 0x0B, 0x01, 0x0675, 0x01, 0x01, "unk_0675(OFFSET)"},
        {0x0680, 0x01, 0x01, 0x0680, 0x08, 0x01, "color_mask(OFFSET).raw"},
        {0x0680, 0x01, 0x01, 0x0680, 0x08, 0x01, "color_mask(OFFSET).R"},
        {0x0680, 0x01, 0x01, 0x0680, 0x08, 0x01, "color_mask(OFFSET).G"},
        {0x0680, 0x01, 0x01, 0x0680, 0x08, 0x01, "color_mask(OFFSET).B"},
        {0x0680, 0x01, 0x01, 0x0680, 0x08, 0x01, "color_mask(OFFSET).A"},
        {0x0688, 0x38, 0x01, 0x0688, 0x01, 0x01, "unk_0688(OFFSET)"},
        {0x06C0, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_address_high"},
        {0x06C1, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_address_low"},
        {0x06C2, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_sequence"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.raw"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.operation"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.fence"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.unit"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.sync_cond"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.select"},
        {0x06C3, 0x01, 0x01, 0x06C0, 0x01, 0x04, "query.query_get.short_query"},
        {0x06C4, 0x3C, 0x01, 0x06C4, 0x01, 0x01, "unk_06C4(OFFSET)"},
        {0x0700, 0x01, 0x01, 0x0700, 0x20, 0x04, "vertex_array(OFFSET).stride"},
        {0x0700, 0x01, 0x01, 0x0700, 0x20, 0x04, "vertex_array(OFFSET).enable"},
        {0x0701, 0x01, 0x01, 0x0700, 0x20, 0x04, "vertex_array(OFFSET).start_high"},
        {0x0702, 0x01, 0x01, 0x0700, 0x20, 0x04, "vertex_array(OFFSET).start_low"},
        {0x0703, 0x01, 0x01, 0x0700, 0x20, 0x04, "vertex_array(OFFSET).divisor"},
        {0x0780, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).separate_alpha"},
        {0x0781, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).equation_rgb"},
        {0x0782, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).factor_source_rgb"},
        {0x0783, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).factor_dest_rgb"},
        {0x0784, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).equation_a"},
        {0x0785, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).factor_source_a"},
        {0x0786, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).factor_dest_a"},
        {0x0787, 0x01, 0x01, 0x0780, 0x08, 0x08, "independent_blend(OFFSET).unk_0787"},
        {0x07C0, 0x01, 0x01, 0x07C0, 0x20, 0x02, "vertex_array_limit(OFFSET).limit_high"},
        {0x07C1, 0x01, 0x01, 0x07C0, 0x20, 0x02, "vertex_array_limit(OFFSET).limit_low"},
        {0x0800, 0x01, 0x01, 0x0800, 0x06, 0x10, "shader_config(OFFSET).enable"},
        {0x0800, 0x01, 0x01, 0x0800, 0x06, 0x10, "shader_config(OFFSET).program"},
        {0x0801, 0x01, 0x01, 0x0800, 0x06, 0x10, "shader_config(OFFSET).offset"},
        {0x0802, 0x0E, 0x01, 0x0800, 0x06, 0x10, "shader_config(OFFSET).unk_0802(OFFSET)"},
        {0x0860, 0x60, 0x01, 0x0860, 0x01, 0x01, "unk_0860(OFFSET)"},
        {0x08C0, 0x20, 0x01, 0x08C0, 0x01, 0x01, "firmware(OFFSET)"},
        {0x08E0, 0x01, 0x01, 0x08E0, 0x01, 0x14, "const_buffer.cb_size"},
        {0x08E1, 0x01, 0x01, 0x08E0, 0x01, 0x14, "const_buffer.cb_address_high"},
        {0x08E2, 0x01, 0x01, 0x08E0, 0x01, 0x14, "const_buffer.cb_address_low"},
        {0x08E3, 0x01, 0x01, 0x08E0, 0x01, 0x14, "const_buffer.cb_pos"},
        {0x08E4, 0x10, 0x01, 0x08E0, 0x01, 0x14, "const_buffer.cb_data(OFFSET)"},
        {0x08F4, 0x10, 0x01, 0x08F4, 0x01, 0x01, "unk_08F4(OFFSET)"},
        {0x0904, 0x01, 0x01, 0x0904, 0x05, 0x08, "cb_bind(OFFSET).raw_config"},
        {0x0904, 0x01, 0x01, 0x0904, 0x05, 0x08, "cb_bind(OFFSET).valid"},
        {0x0904, 0x01, 0x01, 0x0904, 0x05, 0x08, "cb_bind(OFFSET).index"},
        {0x0905, 0x07, 0x01, 0x0904, 0x05, 0x08, "cb_bind(OFFSET).unk_0905(OFFSET)"},
        {0x092C, 0x56, 0x01, 0x092C, 0x01, 0x01, "unk_092C(OFFSET)"},
        {0x0982, 0x01, 0x01, 0x0982, 0x01, 0x01, "tex_cb_index"},
        {0x0983, 0x7D, 0x01, 0x0983, 0x01, 0x01, "unk_0983(OFFSET)"},
        {0x0A00, 0x04, 0x20, 0x0A00, 0x01, 0x01, "tfb_varying_locs(OFFSET)"},
        {0x0A80, 0x298, 0x01, 0x0A80, 0x01, 0x01, "unk_0A80(OFFSET)"},
        {0x0D18, 0x01, 0x01, 0x0D18, 0x01, 0x01, "ssbo_info.buffer_address"},
        {0x0D19, 0x11, 0x01, 0x0D19, 0x01, 0x01, "unk_0D19(OFFSET)"},
        {0x0D2A, 0x05, 0x01, 0x0D2A, 0x01, 0x0A, "tex_info_buffers.address(OFFSET)"},
        {0x0D2F, 0x05, 0x01, 0x0D2A, 0x01, 0x0A, "tex_info_buffers.size(OFFSET)"},
        {0x0D34, 0xCC, 0x01, 0x0D34, 0x01, 0x01, "unk_0D34(OFFSET)"},
        {0x0E00, 0x1000, 0x01, 0x0E00, 0x01, 0x01, "Macro(OFFSET)"},
    }};
}

[[nodiscard]] static constexpr REG_LIST BuildKeplerComputeMethods() {
    return {};
}

[[nodiscard]] static constexpr REG_LIST BuildKeplerMemoryMethods() {
    return {};
}

[[nodiscard]] static constexpr REG_LIST BuildMaxwellDMAMethods() {
    return {};
}

static constexpr REG_LIST METHODS_FERMI{BuildFermiMethods()};
static constexpr REG_LIST METHODS_MAXWELL{BuildMaxwellMethods()};
static constexpr REG_LIST METHODS_KEPLERCOMPUTE{BuildKeplerComputeMethods()};
static constexpr REG_LIST METHODS_KEPLERMEMORY{BuildKeplerMemoryMethods()};
static constexpr REG_LIST METHODS_MAXWELLDMA{BuildMaxwellDMAMethods()};

std::optional<std::tuple<REG_LIST::const_iterator, size_t, size_t>> FindMethod(
    GPU::RecordEntry& entry) {
    const REG_LIST* methods = nullptr;

    switch (entry.engine) {
    case EngineID::FERMI_TWOD_A:
        methods = &METHODS_FERMI;
        break;
    case EngineID::MAXWELL_B:
        methods = &METHODS_MAXWELL;
        break;
    case EngineID::KEPLER_COMPUTE_B:
        methods = &METHODS_KEPLERCOMPUTE;
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        methods = &METHODS_KEPLERMEMORY;
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        methods = &METHODS_MAXWELLDMA;
        break;
    default:
        UNREACHABLE();
        break;
    }

    if (!methods) {
        return std::nullopt;
    }

    for (const auto& method : *methods) {
        size_t this_struct_size =
            method.struct_count > 1
                ? method.struct_base + (method.struct_count * method.struct_size)
                : method.offset + (method.elem_count * method.elem_size);
        if (method.offset <= entry.method && entry.method < this_struct_size) {
            size_t base_offset =
                method.struct_base + ((entry.method - method.struct_base) % method.struct_size);
            auto start_it =
                std::find_if(methods->cbegin(), methods->cend(),
                             [&method](const auto& a) { return a.offset == method.struct_base; });
            while (start_it->offset < base_offset) {
                start_it++;
            }

            if (start_it->offset > base_offset) {
                start_it--;
            }

            size_t struct_idx = (entry.method - method.struct_base) / method.struct_size;
            size_t element_idx = (base_offset - start_it->offset) / method.elem_size;
            if (method.struct_count == 0x1 && base_offset == start_it->offset) {
                element_idx = struct_idx;
            }
            return std::make_optional<std::tuple<REG_LIST::const_iterator, size_t, size_t>>(
                std::make_tuple(start_it, struct_idx, element_idx));
        }
    }
    return std::nullopt;
}

std::vector<std::string> Record::GetMethodNames(GPU::RecordEntry& entry,
                                                REG_LIST::const_iterator start_it,
                                                size_t struct_idx, size_t element_idx,
                                                bool is_prev_state) {
    std::vector<std::string> methods_found;

    if (is_prev_state && start_it->name.starts_with("unk")) {
        return methods_found;
    }

    if (entry.method >= 0xE00) {
        methods_found.emplace_back(
            fmt::format("Macro[{}]", (entry.method - start_it->struct_base) / 2));
        return methods_found;
    }

    const u32 found_offset = start_it->offset;
    while (start_it->offset == found_offset) {
        std::string name{start_it->name};

        if (start_it->struct_count > 1) {
            size_t it = name.find("(OFFSET)");
            if (it != std::string::npos) {
                const std::string pref = name.substr(0, it);
                const std::string suff = name.substr(it + 8);
                name = pref + fmt::format("[{}]", struct_idx) + suff;
            }
        }

        if (start_it->elem_count > 1) {
            size_t it = name.find("(OFFSET)");
            if (it != std::string::npos) {
                const std::string pref = name.substr(0, it);
                const std::string suff = name.substr(it + 8);
                name = pref + fmt::format("[{}]", element_idx) + suff;
            }
        }

        methods_found.emplace_back(name);
        ++start_it;
    }
    return methods_found;
}

} // namespace Tegra
