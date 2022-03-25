// Copyright 2022 TotalJustice.
// SPDX-License-Identifier: GPL-3.0-only

#include "arm7tdmi/arm7tdmi.hpp"
#include "bit.hpp"
#include "gba.hpp"
#include "mem.hpp"
#include <bit>
#include <cstdint>
#include <cstdio>

namespace gba::arm7tdmi::thumb {

// page 124 (5.7)
template<
    bool L, // 0=STR, 1=LDR
    bool B // 0=word, 1=byte
>
auto load_store_with_register_offset(Gba& gba, uint16_t opcode) -> void
{
    const auto Ro = bit::get_range<6, 8>(opcode);
    const auto Rb = bit::get_range<3, 5>(opcode);
    const auto Rd = bit::get_range<0, 2>(opcode);

    const auto base = get_reg(gba, Rb);
    const auto offset = get_reg(gba, Ro);
    const auto addr = base + offset;

    if constexpr (L) // LDR
    {
        std::uint32_t result = 0;

        if constexpr (B) // byte
        {
            result = mem::read8(gba, addr);
        }
        else // word
        {
            result = mem::read32(gba, addr & ~0x3);
            result = std::rotr(result, (addr & 0x3) * 8);
        }

        set_reg_thumb(gba, Rd, result);
    }
    else // STR
    {
        const auto value = get_reg(gba, Rd);

        if constexpr (B) // byte
        {
            mem::write8(gba, addr, value);
        }
        else // word
        {
            mem::write32(gba, addr & ~0x3, value);
        }
    }
}

} // namespace gba::arm7tdmi::thumb
