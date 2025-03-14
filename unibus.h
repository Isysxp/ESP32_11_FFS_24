#pragma once
#include "kl11.h"
#include "rk11.h"
#include "kw11.h"
#include "pc11.h"
#include "lp11.h"
#include "rl11.h"
#include "dl11.h"
#include <stdint.h>

const uint32_t IOBASE_18BIT = 0760000;
const uint32_t IOBASE_22BIT = 017760000;

const uint32_t MEMSIZE22 = (4096-1024) * 1024;
const uint32_t MEMSIZE = (256-8) * 1024;

class UNIBUS {

  public:
    uint16_t *core;
    KL11 cons;
    RK11 rk11;
    KW11 kw11;
    PC11 ptr;
    LP11 lp11;
    RL11 rl11;
    DL11 dl11;

    void write16(uint32_t a, uint16_t v);
    uint16_t read16(uint32_t a);
    uint32_t umap[32];

    void reset();
    uint32_t remap(uint32_t);

};