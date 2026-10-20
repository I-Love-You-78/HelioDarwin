#pragma once
#include <stdint.h>

/*
 * Boot splash drawn before xnu takes over, Clover style: black screen, a
 * white apple in the middle; xnu's verbose console (-v) then scrolls over it.
 * Pure integer geometry (the shim builds with -mgeneral-regs-only), no image
 * data. fb is w x h pixels a8r8g8b8 with stride_px pixels per row.
 */
void splash_draw(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h);
