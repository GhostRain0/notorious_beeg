// Copyright 2022 TotalJustice.
// SPDX-License-Identifier: GPL-3.0-only

#include "arm7tdmi/arm7tdmi.hpp"
#include "bit.hpp"
#include "gba.hpp"
#include "mem.hpp"
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace gba::arm7tdmi::thumb {

// page 138 (5.14)
template<
    bool L, // 0=push, 1=pop
    bool R  // 0=non, 1=store lr/load pc
>
auto push_pop_registers(Gba& gba, uint16_t opcode) -> void
{
    std::uint16_t Rlist = bit::get_range<0, 7>(opcode);
    auto addr = get_sp(gba);

    // assert(Rlist && "empty rlist edge case");
    if (!Rlist && !R)
    {
        gba_log("[push_pop_registers] empty rlist edge case\n");
    }

    if constexpr (L) // pop
    {
        if constexpr (R)
        {
            Rlist = bit::set<PC_INDEX>(Rlist, true);
        }

        while (Rlist)
        {
            const auto reg_index = std::countr_zero(Rlist);
            const auto value = mem::read32(gba, addr);

            set_reg(gba, reg_index, value);

            addr += 4;
            Rlist &= ~(1 << reg_index);
        }

        set_sp(gba, addr);
    }
    else // push
    {
        if constexpr (R)
        {
            Rlist = bit::set<LR_INDEX>(Rlist, true);
        }

        // because pop decrements but loads lowest addr first
        // we subract the addr now and count up.
        // SEE: https://github.com/jsmolka/gba-tests/blob/a6447c5404c8fc2898ddc51f438271f832083b7e/thumb/memory.asm#L374
        addr -= std::popcount(Rlist) * 4;
        set_sp(gba, addr);

        while (Rlist)
        {
            const auto reg_index = std::countr_zero(Rlist);
            const auto value = get_reg(gba, reg_index);

            mem::write32(gba, addr, value);

            addr += 4;
            Rlist &= ~(1 << reg_index);
        }
    }
}

} // namespace gba::arm7tdmi::thumb
