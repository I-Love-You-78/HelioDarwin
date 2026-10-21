#pragma once
#include <stdint.h>

/*
 * Show the tail of the previous boot's console log (the ramoops console
 * zone, see plog.c) on the panel, before plog_init() starts a new one.
 * With Android gone this is how an xnu boot attempt is read: the next shim
 * boot displays what it printed. Returns 0 when there was no log.
 */
int logview_show(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h);

/* The renderer behind it, for any ring buffer (host tests use it too). */
void logview_render(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h,
    volatile const char *data, uint32_t start, uint32_t size, uint32_t cap);
