#pragma once
#include <stdint.h>

/*
 * Clover-style Dual-Boot Menu for HelioDarwin (Infinix X676C / MT6789).
 * Faithful 1:1 recreation of Clover EFI bootloader UI:
 *   - Brushed aluminum metallic gradient theme
 *   - Sleek Apple (iOS 16) and Android (XOS) OS disk badges
 *   - Neon cyan glowing selection indicator
 *   - 5-second countdown timer before default boot
 *   - Full physical key navigation (Volume Up/Down, Power)
 *
 * Returns:
 *   0: Boot iOS 16 (HelioDarwin)
 *   1: Boot Android (Slot B)
 */
int clover_menu(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h);
