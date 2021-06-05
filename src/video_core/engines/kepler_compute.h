// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <vector>
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/engines/const_buffer_engine_interface.h"
#include "video_core/engines/engine_interface.h"
#include "video_core/engines/engine_upload.h"
#include "video_core/engines/shader_type.h"
#include "video_core/gpu.h"
#include "video_core/textures/texture.h"

namespace Core {
class System;
}

namespace Tegra {
class MemoryManager;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra::Engines {

/**
 * This Engine is known as GK104_Compute. Documentation can be found in:
 * https://github.com/envytools/envytools/blob/master/rnndb/graph/gk104_compute.xml
 * https://cgit.freedesktop.org/mesa/mesa/tree/src/gallium/drivers/nouveau/nvc0/nve4_compute.xml.h
 */

#define KEPLER_COMPUTE_REG_INDEX(field_name)                                                       \
    (offsetof(Tegra::Engines::KeplerCompute::Regs, field_name) / sizeof(u32))

class KeplerCompute final : public ConstBufferEngineInterface, public EngineInterface {
public:
    explicit KeplerCompute(Core::System& system, MemoryManager& memory_manager);
    ~KeplerCompute();

    /// Binds a rasterizer to this engine.
    void BindRasterizer(VideoCore::RasterizerInterface* rasterizer);

    static constexpr std::size_t NumConstBuffers = 8;

    struct Regs {
        static constexpr std::size_t NUM_REGS = 0xCF8;

        union {
            struct {
                /* 0x0000 */ INSERT_PADDING_BYTES_NOINIT(0x180);
                /* 0x0180 */ Upload::Registers upload;
                /* 0x01B0 */ struct {
                    union {
                        BitField<0, 1, u32> linear;
                    };
                } exec_upload;
                /* 0x01B4 */ u32 data_upload;
                /* 0x01B8 */ INSERT_PADDING_BYTES_NOINIT(0xFC);
                /* 0x02B4 */ struct {
                    u32 address;
                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address) << 8));
                    }
                } launch_desc_loc;
                /* 0x02B8 */ INSERT_PADDING_BYTES_NOINIT(0x4);
                /* 0x02BC */ u32 launch;
                /* 0x02C0 */ INSERT_PADDING_BYTES_NOINIT(0x129C);
                /* 0x155C */ struct {
                    u32 address_high;
                    u32 address_low;
                    u32 limit;
                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } tsc;
                /* 0x1568 */ INSERT_PADDING_BYTES_NOINIT(0xC);
                /* 0x1574 */ struct {
                    u32 address_high;
                    u32 address_low;
                    u32 limit;
                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } tic;
                /* 0x1580 */ INSERT_PADDING_BYTES_NOINIT(0x88);
                /* 0x1608 */ struct {
                    u32 address_high;
                    u32 address_low;
                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } code_loc;
                /* 0x1610 */ INSERT_PADDING_BYTES_NOINIT(0xFF8);
                /* 0x2608 */ u32 tex_cb_index;
                /* 0x260C */ INSERT_PADDING_BYTES_NOINIT(0xDD4);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    struct LaunchParams {
        static constexpr std::size_t NUM_LAUNCH_PARAMETERS = 0x40;

        /* 0x00 */ INSERT_PADDING_BYTES(0x20);
        /* 0x20 */ u32 program_start;
        /* 0x24 */ INSERT_PADDING_BYTES(0x8);
        /* 0x2C */ BitField<30, 1, u32> linked_tsc;
        /* 0x30 */ BitField<0, 31, u32> grid_dim_x;
        /* 0x34 */ union {
            BitField<0, 16, u32> grid_dim_y;
            BitField<16, 16, u32> grid_dim_z;
        };
        /* 0x38 */ INSERT_PADDING_BYTES(0xC);
        /* 0x44 */ BitField<0, 18, u32> shared_alloc;
        /* 0x48 */ BitField<16, 16, u32> block_dim_x;
        /* 0x4C */ union {
            BitField<0, 16, u32> block_dim_y;
            BitField<16, 16, u32> block_dim_z;
        };
        /* 0x50 */ union {
            BitField<0, 8, u32> const_buffer_enable_mask;
            BitField<29, 2, u32> cache_layout;
        };
        /* 0x54 */ INSERT_PADDING_BYTES(0x20);
        struct ConstBufferConfig {
            u32 address_low;
            union {
                BitField<0, 8, u32> address_high;
                BitField<15, 17, u32> size;
            };
            GPUVAddr Address() const {
                return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high.Value()) << 32) |
                                             address_low);
            }
        };
        /* 0x74 */ std::array<ConstBufferConfig, NumConstBuffers> const_buffer_config;
        /* 0xB4 */ union {
            BitField<0, 20, u32> local_pos_alloc;
            BitField<27, 5, u32> barrier_alloc;
        };
        /* 0xB8 */ union {
            BitField<0, 20, u32> local_neg_alloc;
            BitField<24, 5, u32> gpr_alloc;
        };
        /* 0xBC */ union {
            BitField<0, 20, u32> local_crs_alloc;
            BitField<24, 5, u32> sass_version;
        };
        /* 0xC0 */ INSERT_PADDING_BYTES(0x40);
    } launch_description{};

    struct {
        u32 write_offset = 0;
        u32 copy_size = 0;
        std::vector<u8> inner_buffer;
    } state{};

    static_assert(sizeof(Regs) == Regs::NUM_REGS * sizeof(u32),
                  "KeplerCompute Regs has wrong size");

    static_assert(sizeof(LaunchParams) == LaunchParams::NUM_LAUNCH_PARAMETERS * sizeof(u32),
                  "KeplerCompute LaunchParams has wrong size");

    /// Write the value to the register identified by method.
    void CallMethod(u32 method, u32 method_argument, bool is_last_call) override;

    /// Write multiple values to the register identified by method.
    void CallMultiMethod(u32 method, const u32* base_start, u32 amount,
                         u32 methods_pending) override;

    u32 AccessConstBuffer32(ShaderType stage, u64 const_buffer, u64 offset) const override;

    SamplerDescriptor AccessBoundSampler(ShaderType stage, u64 offset) const override;

    SamplerDescriptor AccessBindlessSampler(ShaderType stage, u64 const_buffer,
                                            u64 offset) const override;

    SamplerDescriptor AccessSampler(u32 handle) const override;

    u32 GetBoundBuffer() const override {
        return regs.tex_cb_index;
    }

    VideoCore::GuestDriverProfile& AccessGuestDriverProfile() override;

    const VideoCore::GuestDriverProfile& AccessGuestDriverProfile() const override;

private:
    void ProcessLaunch();

    /// Retrieves information about a specific TIC entry from the TIC buffer.
    Texture::TICEntry GetTICEntry(u32 tic_index) const;

    /// Retrieves information about a specific TSC entry from the TSC buffer.
    Texture::TSCEntry GetTSCEntry(u32 tsc_index) const;

    Core::System& system;
    MemoryManager& memory_manager;
    VideoCore::RasterizerInterface* rasterizer = nullptr;
    Upload::State upload_state;
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(KeplerCompute::Regs, field_name) == position,                           \
                  "Field " #field_name " has invalid position")

#define ASSERT_LAUNCH_PARAM_POSITION(field_name, position)                                         \
    static_assert(offsetof(KeplerCompute::LaunchParams, field_name) == position,                   \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(upload, 0x180);
ASSERT_REG_POSITION(exec_upload, 0x1B0);
ASSERT_REG_POSITION(data_upload, 0x1B4);
ASSERT_REG_POSITION(launch, 0x2BC);
ASSERT_REG_POSITION(tsc, 0x155C);
ASSERT_REG_POSITION(tic, 0x1574);
ASSERT_REG_POSITION(code_loc, 0x1608);
ASSERT_REG_POSITION(tex_cb_index, 0x2608);
ASSERT_LAUNCH_PARAM_POSITION(program_start, 0x20);
ASSERT_LAUNCH_PARAM_POSITION(grid_dim_x, 0x30);
ASSERT_LAUNCH_PARAM_POSITION(shared_alloc, 0x44);
ASSERT_LAUNCH_PARAM_POSITION(block_dim_x, 0x48);
ASSERT_LAUNCH_PARAM_POSITION(const_buffer_enable_mask, 0x50);
ASSERT_LAUNCH_PARAM_POSITION(const_buffer_config, 0x74);

#undef ASSERT_REG_POSITION

} // namespace Tegra::Engines
