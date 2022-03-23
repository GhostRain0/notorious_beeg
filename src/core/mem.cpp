// Copyright 2022 TotalJustice.
// SPDX-License-Identifier: GPL-3.0-only

#include "mem.hpp"
#include "apu/apu.hpp"
#include "arm7tdmi/arm7tdmi.hpp"
#include "backup/backup.hpp"
#include "bit.hpp"
#include "gba.hpp"
#include "ppu.hpp"
#include "scheduler.hpp"
#include "timer.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <span>

namespace gba::mem {

#define MEM gba.mem

// General Internal Memory
constexpr auto BIOS_MASK        = 0x00003FFF; // not checked if this is correct!!!
constexpr auto EWRAM_MASK       = 0x0003FFFF;
constexpr auto IWRAM_MASK       = 0x00007FFF;
constexpr auto IO_MASK          = 0x3FF;
// Internal Display Memory
constexpr auto PALETTE_RAM_MASK = 0x000003FF;
constexpr auto VRAM_MASK        = 0x0001FFFF;
constexpr auto OAM_MASK         = 0x000003FF;
// External Memory (Game Pak)
constexpr auto ROM_MASK         = 0x01FFFFFF;
constexpr auto SRAM_MASK        = (1024*32)-1;

auto setup_tables(Gba& gba) -> void
{
    MEM.rmap_8.fill({});
    MEM.rmap_16.fill({});
    MEM.rmap_32.fill({});
    MEM.wmap_8.fill({});
    MEM.wmap_16.fill({});
    MEM.wmap_32.fill({});

    MEM.rmap_16[0x0] = {gba.mem.bios, BIOS_MASK};
    MEM.rmap_16[0x2] = {gba.mem.ewram, EWRAM_MASK};
    MEM.rmap_16[0x3] = {gba.mem.iwram, IWRAM_MASK};
    MEM.rmap_16[0x5] = {gba.mem.palette_ram, PALETTE_RAM_MASK};
    // MEM.rmap_16[0x6] = {gba.mem.vram, VRAM_MASK};
    MEM.rmap_16[0x7] = {gba.mem.oam, OAM_MASK};
    MEM.rmap_16[0x8] = {gba.rom, ROM_MASK};
    MEM.rmap_16[0x9] = {gba.rom, ROM_MASK};
    MEM.rmap_16[0xA] = {gba.rom, ROM_MASK};
    MEM.rmap_16[0xB] = {gba.rom, ROM_MASK};
    MEM.rmap_16[0xC] = {gba.rom, ROM_MASK};
    MEM.rmap_16[0xD] = {gba.rom, ROM_MASK};
    MEM.rmap_16[0xE] = {gba.rom, ROM_MASK};

    MEM.wmap_16[0x2] = {gba.mem.ewram, EWRAM_MASK};
    MEM.wmap_16[0x3] = {gba.mem.iwram, IWRAM_MASK};
    MEM.wmap_16[0x5] = {gba.mem.palette_ram, PALETTE_RAM_MASK};
    // MEM.wmap_16[0x6] = {gba.mem.vram, VRAM_MASK};
    MEM.wmap_16[0x7] = {gba.mem.oam, OAM_MASK};

    // this will be handled by the function handlers
    switch (gba.backup.type)
    {
        case backup::Type::NONE:
            break;

        case backup::Type::EEPROM:
            MEM.rmap_16[0xD] = {};
            MEM.wmap_16[0xD] = {};
            break;

        case backup::Type::SRAM:
            MEM.rmap_16[0xE] = {gba.backup.sram.data, SRAM_MASK};
            MEM.wmap_16[0xE] = {gba.backup.sram.data, SRAM_MASK};
            break;

        case backup::Type::FLASH:
        case backup::Type::FLASH512:
        case backup::Type::FLASH1M:
            MEM.rmap_16[0xE] = {};
            MEM.wmap_16[0xE] = {};
            break;
    }

    // can't use ranges::copy on std::array...
    std::copy(MEM.rmap_16.begin(), MEM.rmap_16.end(), MEM.rmap_8.begin());
    std::copy(MEM.wmap_16.begin(), MEM.wmap_16.end(), MEM.wmap_8.begin());
    std::copy(MEM.rmap_16.begin(), MEM.rmap_16.end(), MEM.rmap_32.begin());
    std::copy(MEM.wmap_16.begin(), MEM.wmap_16.end(), MEM.wmap_32.begin());

    MEM.rmap_8[0x5] = {}; // ignore palette ram byte reads
    MEM.rmap_8[0x6] = {}; // ignore vram byte reads
    MEM.rmap_8[0x7] = {}; // ignore oam byte reads
    MEM.wmap_8[0x5] = {}; // ignore palette ram byte stores
    MEM.wmap_8[0x6] = {}; // ignore vram byte stores
    MEM.wmap_8[0x7] = {}; // ignore oam byte stores
}

auto reset(Gba& gba) -> void
{
    std::memset(MEM.ewram, 0, std::size(MEM.ewram));
    std::memset(MEM.iwram, 0, std::size(MEM.iwram));
    std::memset(MEM.palette_ram, 0, std::size(MEM.palette_ram));
    std::memset(MEM.vram, 0, std::size(MEM.vram));
    std::memset(MEM.oam, 0, std::size(MEM.oam));
    REG_KEY = 0xFFFF;

    setup_tables(gba);
}

// helpers for read / write arrays.
template <typename T, bool Vram = false> [[nodiscard]]
constexpr auto read_array(std::span<const uint8_t> array, auto mask, uint32_t addr) -> T {
    if constexpr(Vram)
    {
        if ((addr & VRAM_MASK) > 0x17FFF) [[unlikely]]
        {
            addr -= 0x8000;
        }
    }
    constexpr auto shift = sizeof(T) >> 1;
    return reinterpret_cast<const T*>(array.data())[(addr>>shift) & (mask>>shift)];
}

template <typename T, bool Vram = false>
constexpr auto write_array(std::span<uint8_t> array, auto mask, uint32_t addr, T v) -> void {
    if constexpr(Vram)
    {
        if ((addr & VRAM_MASK) > 0x17FFF) [[unlikely]]
        {
            addr -= 0x8000;
        }
    }
    constexpr auto shift = sizeof(T) >> 1;
    reinterpret_cast<T*>(array.data())[(addr>>shift) & (mask>>shift)] = v;
}

// byte access, just makes it easier
[[nodiscard]]
auto read_io(Gba& gba, std::uint32_t addr) -> std::uint8_t
{
    switch (addr)
    {
        // todo: fix this for scheduler timers
        #if ENABLE_SCHEDULER
        case IO_TM0D + 0: return timer::read_timer(gba, 0) & 0xFF;
        case IO_TM0D + 1: return timer::read_timer(gba, 0) >> 8;
        case IO_TM1D + 0: return timer::read_timer(gba, 1) & 0xFF;
        case IO_TM1D + 1: return timer::read_timer(gba, 1) >> 8;
        case IO_TM2D + 0: return timer::read_timer(gba, 2) & 0xFF;
        case IO_TM2D + 1: return timer::read_timer(gba, 2) >> 8;
        case IO_TM3D + 0: return timer::read_timer(gba, 3) & 0xFF;
        case IO_TM3D + 1: return timer::read_timer(gba, 3) >> 8;
        #else
        case IO_TM0D + 0: return REG_TM0D & 0xFF;
        case IO_TM0D + 1: return REG_TM0D >> 8;
        case IO_TM1D + 0: return REG_TM1D & 0xFF;
        case IO_TM1D + 1: return REG_TM1D >> 8;
        case IO_TM2D + 0: return REG_TM2D & 0xFF;
        case IO_TM2D + 1: return REG_TM2D >> 8;
        case IO_TM3D + 0: return REG_TM3D & 0xFF;
        case IO_TM3D + 1: return REG_TM3D >> 8;
        #endif

        default:
            //printf("unhandled io read addr: 0x%08X\n", addr);
            // assert(!"todo");
            // CPU.breakpoint = true;
            break;
    }

    return MEM.io[addr & 0x3FF];
}

auto write_io16(Gba& gba, std::uint32_t addr, std::uint16_t value) -> void
{
    switch (addr)
    {
        // read only regs
        case IO_KEY: return;
        case IO_VCOUNT: return;

        case IO_TM0D: gba.timer[0].reload = value; return;
        case IO_TM1D: gba.timer[1].reload = value; return;
        case IO_TM2D: gba.timer[2].reload = value; return;
        case IO_TM3D: gba.timer[3].reload = value; return;

        case IO_IF:
            REG_IF &= ~value;
            return;

        case IO_DISPSTAT:
            REG_DISPSTAT = (REG_DISPSTAT & 0x7) | (value & ~0x7);
            return;
    }

    write_array<u16>(MEM.io, IO_MASK, addr, value);

    switch (addr)
    {
        // todo: read only when apu is off
        case mem::IO_SOUND1CNT_L:
        case mem::IO_SOUND1CNT_H:
        case mem::IO_SOUND1CNT_X:
        case mem::IO_SOUND2CNT_L:
        case mem::IO_SOUND2CNT_H:
        case mem::IO_SOUND3CNT_L:
        case mem::IO_SOUND3CNT_H:
        case mem::IO_SOUND3CNT_X:
        case mem::IO_SOUND4CNT_L:
        case mem::IO_SOUND4CNT_H:
        case mem::IO_SOUNDCNT_L:
        case mem::IO_SOUNDCNT_X:
        case mem::IO_WAVE_RAM0_L:
        case mem::IO_WAVE_RAM0_H:
        case mem::IO_WAVE_RAM1_L:
        case mem::IO_WAVE_RAM1_H:
        case mem::IO_WAVE_RAM2_L:
        case mem::IO_WAVE_RAM2_H:
        case mem::IO_WAVE_RAM3_L:
        case mem::IO_WAVE_RAM3_H:
            apu::write_legacy(gba, addr, value);
            break;

        case IO_TM0CNT: timer::on_cnt0_write(gba, REG_TM0CNT); break;
        case IO_TM1CNT: timer::on_cnt1_write(gba, REG_TM1CNT); break;
        case IO_TM2CNT: timer::on_cnt2_write(gba, REG_TM2CNT); break;
        case IO_TM3CNT: timer::on_cnt3_write(gba, REG_TM3CNT); break;

        case IO_DMA0CNT: gba_log("REG_DMA0CNT: 0x%08X\n", REG_DMA0CNT); break;
        case IO_DMA0CNT + 2:
            gba_log("REG_DMA0CNT: 0x%08X\n", REG_DMA0CNT);
            dma::on_cnt_write(gba, 0);
            break;

        case IO_DMA1CNT: gba_log("REG_DMA1CNT: 0x%08X\n", REG_DMA1CNT); break;
        case IO_DMA1CNT + 2:
            gba_log("REG_DMA1CNT: 0x%08X\n", REG_DMA1CNT);
            dma::on_cnt_write(gba, 1);
            break;

        case IO_DMA2CNT: gba_log("REG_DMA2CNT: 0x%08X\n", REG_DMA2CNT); break;
        case IO_DMA2CNT + 2:
            gba_log("REG_DMA2CNT: 0x%08X\n", REG_DMA2CNT);
            dma::on_cnt_write(gba, 2);
            break;

        case IO_DMA3CNT: gba_log("REG_DMA3CNT: 0x%08X\n", REG_DMA3CNT); break;
        case IO_DMA3CNT + 2:
            gba_log("REG_DMA3CNT + 3: 0x%08X\n", REG_DMA3CNT);
            dma::on_cnt_write(gba, 3);
            break;

        case IO_IME:
            arm7tdmi::schedule_interrupt(gba);
            break;

        case IO_HALTCNT_L:
        case IO_HALTCNT_H:
            arm7tdmi::on_halt_trigger(gba, arm7tdmi::HaltType::write);
            break;

        case IO_IE:
            arm7tdmi::schedule_interrupt(gba);
            break;

        case IO_FIFO_A_L:
        case IO_FIFO_A_H:
            apu::on_fifo_write16(gba, value, 0);
            break;

        case IO_FIFO_B_L:
        case IO_FIFO_B_H:
            apu::on_fifo_write16(gba, value, 1);
            break;

        case IO_SOUNDCNT_H:
            apu::on_soundcnt_write(gba);
            break;

        default:
            //printf("unhandled io write addr: 0x%08X value: 0x%02X pc: 0x%08X\n", addr, value, arm7tdmi::get_pc(gba));
            // assert(!"todo");
            break;
    }
}

auto write_io32(Gba& gba, std::uint32_t addr, std::uint32_t value) -> void
{
    // games typically do 32-bit writes to 32-bit registers

    switch (addr)
    {
        case IO_DISPCNT:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            return;

        case IO_IME:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            arm7tdmi::schedule_interrupt(gba);
            return;

        case IO_DMA0SAD:
        case IO_DMA1SAD:
        case IO_DMA2SAD:
        case IO_DMA3SAD:
        case IO_DMA0DAD:
        case IO_DMA1DAD:
        case IO_DMA2DAD:
        case IO_DMA3DAD:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            return;

        case IO_DMA0CNT:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            dma::on_cnt_write(gba, 0);
            return;

        case IO_DMA1CNT:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            dma::on_cnt_write(gba, 1);
            return;

        case IO_DMA2CNT:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            dma::on_cnt_write(gba, 2);
            return;

        case IO_DMA3CNT:
            write_array<u32>(MEM.io, IO_MASK, addr, value);
            dma::on_cnt_write(gba, 3);
            return;

        case IO_FIFO_A_L:
        case IO_FIFO_A_H:
            apu::on_fifo_write32(gba, value, 0);
            return;

        case IO_FIFO_B_L:
        case IO_FIFO_B_H:
            apu::on_fifo_write32(gba, value, 1);
            return;
    }

    // std::printf("[IO] 32bit write to 0x%08X\n", addr);
    write_io16(gba, addr + 0, value >> 0x00);
    write_io16(gba, addr + 2, value >> 0x10);
}

auto write_io8(Gba& gba, std::uint32_t addr, std::uint8_t value) -> void
{
    // printf("bit io write to 0x%08X\n", addr);
    switch (addr)
    {
        case IO_SOUND1CNT_L + 0:
        case IO_SOUND1CNT_H + 0:
        case IO_SOUND1CNT_H + 1:
        case IO_SOUND1CNT_X + 0:
        case IO_SOUND1CNT_X + 1:
        case IO_SOUND2CNT_L + 0:
        case IO_SOUND2CNT_L + 1:
        case IO_SOUND2CNT_H + 0:
        case IO_SOUND2CNT_H + 1:
        case IO_SOUND3CNT_L + 0:
        case IO_SOUND3CNT_H + 0:
        case IO_SOUND3CNT_H + 1:
        case IO_SOUND3CNT_X + 0:
        case IO_SOUND3CNT_X + 1:
        case IO_SOUND4CNT_L + 0:
        case IO_SOUND4CNT_L + 1:
        case IO_SOUND4CNT_H + 0:
        case IO_SOUND4CNT_H + 1:
        case IO_WAVE_RAM0_L + 0:
        case IO_WAVE_RAM0_L + 1:
        case IO_WAVE_RAM0_H + 0:
        case IO_WAVE_RAM0_H + 1:
        case IO_WAVE_RAM1_L + 0:
        case IO_WAVE_RAM1_L + 1:
        case IO_WAVE_RAM1_H + 0:
        case IO_WAVE_RAM1_H + 1:
        case IO_WAVE_RAM2_L + 0:
        case IO_WAVE_RAM2_L + 1:
        case IO_WAVE_RAM2_H + 0:
        case IO_WAVE_RAM2_H + 1:
        case IO_WAVE_RAM3_L + 0:
        case IO_WAVE_RAM3_L + 1:
        case IO_WAVE_RAM3_H + 0:
        case IO_WAVE_RAM3_H + 1:
            // printf("8bit apu write: 0x%08X\n", addr);
            write_array<u8>(MEM.io, IO_MASK, addr, value);
            apu::write_legacy8(gba, addr, value);
            return;

        case IO_IF + 0:
            REG_IF &= ~value;
            return;

        case IO_IF + 1:
            REG_IF &= ~(value << 8);
            return;

        case IO_FIFO_A_L:
        case IO_FIFO_A_L+1:
        case IO_FIFO_A_H:
        case IO_FIFO_A_H+1:
            apu::on_fifo_write8(gba, value, 0);
            return;

        case IO_FIFO_B_L:
        case IO_FIFO_B_L+1:
        case IO_FIFO_B_H:
        case IO_FIFO_B_H+1:
            apu::on_fifo_write8(gba, value, 1);
            return;

        case IO_IME:
            write_array<u8>(MEM.io, IO_MASK, addr, value);
            arm7tdmi::schedule_interrupt(gba);
            return;

        case IO_HALTCNT_H:
            arm7tdmi::on_halt_trigger(gba, arm7tdmi::HaltType::write);
            return;
    }

    u16 actual_value = value;
    if (addr & 1)
    {
        actual_value <<= 8;
        actual_value |= MEM.io[addr & 0x3FF];
    }
    else
    {
        actual_value |= (u16)MEM.io[addr & 0x3FF] << 8;
    }

    write_io16(gba, addr & ~0x1, actual_value);
}

enum class FunctionType
{
    bios,
    ewram,
    iwram,
    io,
    pram,
    vram,
    oam,
    rom,
    eeprom,
    sram,
};

template<typename T> [[nodiscard]]
constexpr T read_io2(Gba& gba, u32 addr)
{
    if constexpr(sizeof(T) == 4)
    {
        const auto hi_word_hi = read_io(gba, addr + 0);
        const auto hi_word_lo = read_io(gba, addr + 1);
        const auto lo_word_hi = read_io(gba, addr + 2);
        const auto lo_word_lo = read_io(gba, addr + 3);

        return (lo_word_lo << 24) | (lo_word_hi << 16) | (hi_word_lo << 8) | hi_word_hi;
    }
    else if constexpr(sizeof(T) == 2)
    {
        const auto hi = read_io(gba, addr + 0);
        const auto lo = read_io(gba, addr + 1);

        return (lo << 8) | hi;
    }
    else if constexpr(sizeof(T) == 1)
    {
        return read_io(gba, addr);
    }
}

template<typename T>
constexpr void write_io2(Gba& gba, u32 addr, T value)
{
    if constexpr(sizeof(T) == 4)
    {
        write_io32(gba, addr, value);
    }
    else if constexpr(sizeof(T) == 2)
    {
        write_io16(gba, addr, value);
    }
    else if constexpr(sizeof(T) == 1)
    {
        write_io8(gba, addr, value);
    }
}

template<typename T, FunctionType type> [[nodiscard]]
constexpr auto read_function(Gba& gba, u32 addr) -> T
{
    if constexpr(type == FunctionType::io)
    {
        return read_io2<T>(gba, addr);
    }
    if constexpr(type == FunctionType::vram)
    {
        if ((addr & VRAM_MASK) > 0x17FFF) [[unlikely]]
        {
            addr -= 0x8000;
        }
        return read_array<T, true>(MEM.vram, VRAM_MASK, addr);
    }
    if constexpr(type == FunctionType::rom)
    {
        return read_array<T>(gba.rom, ROM_MASK, addr);
    }
    if constexpr(type == FunctionType::eeprom)
    {
        if (gba.backup.type == backup::Type::EEPROM)
        {
            T value = 0;
            // todo: check rom size for region access
            if constexpr(sizeof(T) == 1)
            {
                value |= gba.backup.eeprom.read(gba, addr);
            }
            if constexpr(sizeof(T) == 2)
            {
                value |= gba.backup.eeprom.read(gba, addr);
            }
            if constexpr(sizeof(T) == 4)
            {
                assert(!"32bit read from eeprom");
                value |= gba.backup.eeprom.read(gba, addr+0) << 0;
                value |= gba.backup.eeprom.read(gba, addr+1) << 16;
            }
            return value;
        }
        else
        {
            return read_array<T>(gba.rom, ROM_MASK, addr);
        }
    }
    if constexpr(type == FunctionType::sram)
    {
        const auto backup_type = gba.backup.type;

        if (backup_type == backup::Type::SRAM)
        {
            return read_array<T>(gba.backup.sram.data, SRAM_MASK, addr);
        }
        else if (backup_type == backup::Type::FLASH || backup_type == backup::Type::FLASH1M || backup_type == backup::Type::FLASH512)
        {
            return gba.backup.flash.read(gba, addr);
        }
        else
        {
            return read_array<T>(gba.rom, ROM_MASK, addr);
        }
    }
}

template<typename T, FunctionType type>
constexpr void write_function(Gba& gba, u32 addr, T value)
{
    if constexpr(type == FunctionType::pram)
    {
        if constexpr(sizeof(T) == 1)
        {
            const u32 end_region = 0x50003FF;

            // if we are in this region, then we do a 16bit write
            // where the 8bit value is written as the upper / lower half
            if (addr <= end_region)
            {
                // align to 16bits
                addr &= ~0x1;
                const u16 new_value = (value << 8) | value;

                write_array<u16, true>(MEM.palette_ram, PALETTE_RAM_MASK, addr, new_value);
            }
        }
    }
    if constexpr(type == FunctionType::vram)
    {
        if constexpr(sizeof(T) == 1)
        {
            const bool bitmap = ppu::is_bitmap_mode(gba);
            const u32 end_region = bitmap ? 0x6013FFF : 0x600FFFF;

            // if we are in this region, then we do a 16bit write
            // where the 8bit value is written as the upper / lower half
            if (addr <= end_region)
            {
                // align to 16bits
                addr &= ~0x1;
                const u16 new_value = (value << 8) | value;

                if ((addr & VRAM_MASK) > 0x17FFF) [[unlikely]]
                {
                    addr -= 0x8000;
                }
                write_array<u16, true>(MEM.vram, VRAM_MASK, addr, new_value);
            }
        }
        else
        {
            if ((addr & VRAM_MASK) > 0x17FFF) [[unlikely]]
            {
                addr -= 0x8000;
            }
            write_array<T, true>(MEM.vram, VRAM_MASK, addr, value);
        }
    }
    if constexpr(type == FunctionType::eeprom)
    {
        if (gba.backup.type == backup::Type::EEPROM)
        {
            // todo: check rom size for region access
            if constexpr(sizeof(T) == 1)
            {
                gba.backup.eeprom.write(gba, addr, value);
            }
            if constexpr(sizeof(T) == 2)
            {
                gba.backup.eeprom.write(gba, addr, value);
            }
            if constexpr(sizeof(T) == 4)
            {
                assert(!"32bit write to eeprom");
                printf("32bit write\n");
                arm7tdmi::print_bits<32>(value);
                gba.backup.eeprom.write(gba, addr+0, value>>0);
                gba.backup.eeprom.write(gba, addr+1, value>>16);
            }
        }
    }
    if constexpr(type == FunctionType::sram)
    {
        const auto backup_type = gba.backup.type;

        if (backup_type == backup::Type::SRAM)
        {
            write_array<T>(gba.backup.sram.data, SRAM_MASK, addr, value);
        }
        else if (backup_type == backup::Type::FLASH || backup_type == backup::Type::FLASH1M || backup_type == backup::Type::FLASH512)
        {
            gba.backup.flash.write(gba, addr, value);
        }
    }
}

template<typename T>
constexpr T empty_read(Gba& gba, u32 addr)
{
    return 0xFF;
}

template<typename T>
constexpr void empty_write(Gba& gba, u32 addr, T value)
{
}

template<typename T>
using ReadFunction = T(*)(Gba& gba, u32 addr);
template<typename T>
using WriteFunction = void(*)(Gba& gba, u32 addr, T value);

template<typename T>
constexpr ReadFunction<T> READ_FUNCTION[0x10] =
{
    /*[0x0] =*/ empty_read, // handled
    /*[0x1] =*/ empty_read,
    /*[0x2] =*/ empty_read, // handled
    /*[0x3] =*/ empty_read, // handled
    /*[0x4] =*/ read_function<T, FunctionType::io>,
    /*[0x5] =*/ empty_read, // handled
    /*[0x6] =*/ read_function<T, FunctionType::vram>,
    /*[0x7] =*/ empty_read, // handled
    /*[0x8] =*/ read_function<T, FunctionType::rom>,
    /*[0x9] =*/ read_function<T, FunctionType::rom>,
    /*[0xA] =*/ read_function<T, FunctionType::rom>,
    /*[0xB] =*/ read_function<T, FunctionType::rom>,
    /*[0xC] =*/ read_function<T, FunctionType::rom>,
    /*[0xD] =*/ read_function<T, FunctionType::eeprom>,
    /*[0xE] =*/ read_function<T, FunctionType::sram>, // todo: backup ram support
    /*[0xF] =*/ empty_read,
};

template<typename T>
constexpr WriteFunction<T> WRITE_FUNCTION[0x10] =
{
    /*[0x0] =*/ empty_write,
    /*[0x1] =*/ empty_write,
    /*[0x2] =*/ empty_write, // handled
    /*[0x3] =*/ empty_write, // handled
    /*[0x4] =*/ write_io2<T>,
    /*[0x5] =*/ write_function<T, FunctionType::pram>,
    /*[0x6] =*/ write_function<T, FunctionType::vram>,
    /*[0x7] =*/ empty_write, // handled
    /*[0x8] =*/ empty_write,
    /*[0x9] =*/ empty_write,
    /*[0xA] =*/ empty_write,
    /*[0xB] =*/ empty_write,
    /*[0xC] =*/ empty_write,
    /*[0xD] =*/ write_function<T, FunctionType::eeprom>,
    /*[0xE] =*/ write_function<T, FunctionType::sram>,
    /*[0xF] =*/ empty_write,
};

// all these functions are inlined
auto read8(Gba& gba, std::uint32_t addr) -> std::uint8_t
{
    gba.cycles++;

    auto& entry = MEM.rmap_8[(addr >> 24) & 0xF];
    if (entry.array.size()) [[likely]]
    {
        return read_array<u8>(entry.array, entry.mask, addr);
    }
    else
    {
        return READ_FUNCTION<u8>[(addr >> 24) & 0xF](gba, addr);
    }
}

auto read16(Gba& gba, std::uint32_t addr) -> std::uint16_t
{
    addr &= ~0x1;
    gba.cycles++;

    auto& entry = MEM.rmap_16[(addr >> 24) & 0xF];
    if (entry.array.size()) [[likely]]
    {
        return read_array<u16>(entry.array, entry.mask, addr);
    }
    else
    {
        return READ_FUNCTION<u16>[(addr >> 24) & 0xF](gba, addr);
    }
}

auto read32(Gba& gba, std::uint32_t addr) -> std::uint32_t
{
    addr &= ~0x3;
    gba.cycles++;

    auto& entry = MEM.rmap_32[(addr >> 24) & 0xF];
    if (entry.array.size()) [[likely]]
    {
        return read_array<u32>(entry.array, entry.mask, addr);
    }
    else
    {
        return READ_FUNCTION<u32>[(addr >> 24) & 0xF](gba, addr);
    }
}

auto write8(Gba& gba, std::uint32_t addr, std::uint8_t value) -> void
{
    gba.cycles++;
    auto& entry = MEM.wmap_8[(addr >> 24) & 0xF];
    if (entry.array.size()) [[likely]]
    {
        write_array<u8>(entry.array, entry.mask, addr, value);
    }
    else
    {
        WRITE_FUNCTION<u8>[(addr >> 24) & 0xF](gba, addr, value);
    }
}

auto write16(Gba& gba, std::uint32_t addr, std::uint16_t value) -> void
{
    addr &= ~0x1;
    gba.cycles++;

    auto& entry = MEM.wmap_16[(addr >> 24) & 0xF];
    if (entry.array.size()) [[likely]]
    {
        write_array<u16>(entry.array, entry.mask, addr, value);
    }
    else
    {
        WRITE_FUNCTION<u16>[(addr >> 24) & 0xF](gba, addr, value);
    }
}

auto write32(Gba& gba, std::uint32_t addr, std::uint32_t value) -> void
{
    addr &= ~0x3;
    gba.cycles++;

    auto& entry = MEM.wmap_32[(addr >> 24) & 0xF];
    if (entry.array.size()) [[likely]]
    {
        write_array<u32>(entry.array, entry.mask, addr, value);
    }
    else
    {
        WRITE_FUNCTION<u32>[(addr >> 24) & 0xF](gba, addr, value);
    }
}

} // namespace gba::mem
