#pragma once
#include "avr11.h"
#include <array>
#include <stdint.h>
#include <stdio.h>

class KT11 {

  public:
    std::array<uint16_t, 4> SR;

    template <bool wr>
    inline uint32_t decode(const uint16_t a, const uint16_t mode) {
        uint16_t amsk = SR[3] & 020?0177777 : 07777;

        if ((SR[0] & 1) == 0) {
            if (SR[3] & 20)
                return a > 0157777 ? ((uint32_t)a) + 017600000 : a;
            else
            return a > 0157777 ? ((uint32_t)a) + 0600000 : a;
        }
        const auto i = (a >> 13);
 
        if (wr && !pages[mode][i].write()) {
            SR[0] = (1 << 13) | 1;
            SR[0] |= (a >> 12) & ~1;
            if (mode) {
                SR[0] |= (1 << 5) | (1 << 6);
            }
            //SR2 = cpu.PC;

            printf("mmu::decode write to read-only page %06o\n", a);
            trap(0250); // intfault
        }
        if (!pages[mode][i].read()) {
            SR[0] = (1 << 15) | 1;
            SR[0] |= (a >> 12) & ~1;
            if (mode) {
                SR[0] |= (1 << 5) | (1 << 6);
            }
            // SR2 = cpu.PC;
            printf("mmu::decode read from no-access page %06o PDR:%06o Ndx:%d Mode:%d\r\n", a, pages[mode][i].pdr,i,mode);
            trap(0250); // intfault
        }
        const auto block = (a >> 6) & 0177;
        const auto disp = a & 077;
        // if ((p.ed() && (block < p.len())) || (!p.ed() && (block > p.len())))
        // {
        if (pages[mode][i].ed() ? (block < pages[mode][i].len())
                                : (block > pages[mode][i].len())) {
            SR[0] = (1 << 14) | 1;
            SR[0] |= (a >> 12) & ~1;
            if (mode) {
                SR[0] |= (1 << 5) | (1 << 6);
            }
            // SR2 = cpu.PC;
            //printf("page length exceeded, address %06o (block %03o) is beyond "
            //       "length "
            //       "%03o\r\n",
            //       a, block, pages[mode][i].len());
            SR[0] |= 0200;
            trap(0250); // intfault
        }
        if constexpr (wr) {
            pages[mode][i].pdr |= 1 << 6;
        }
        const auto aa = ((((pages[mode][i].addr() & amsk) + block) << 6) + disp) & 017777777;
        // printf("decode: slow %06o -> %06o\n", a, aa);
        return aa;
    }

    uint16_t read16(uint32_t a);
    void write16(uint32_t a, uint16_t v);

    struct page {
        uint16_t par, pdr;

        inline uint32_t addr() { return par; }
        inline uint8_t len() { return (pdr >> 8) & 0xff; }
        inline bool read() { return (pdr & 2) == 2; }
        inline bool write() { return (pdr & 6) == 6; };
        inline bool ed() { return pdr & 8; }
    };

    std::array<std::array<struct page, 16>, 4> pages;
};