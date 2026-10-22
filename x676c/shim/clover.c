/*
 * Clover-style Dual-Boot Menu for HelioDarwin (Infinix X676C / MT6789).
 * Faithful 1:1 recreation of Clover EFI bootloader UI:
 *   - Brushed aluminum metallic slate theme with horizontal grain
 *   - Apple (iOS 16) and Android (XOS) OS disk badges
 *   - Neon cyan glowing selection aura and pointer
 *   - 5-second countdown timer before default boot
 *   - Physical key navigation via MTK Keypad (0x10010004)
 */
#include "clover.h"
#include "font.h"
#include "hw.h"

#define MTK_KPD_PA       0x10010000UL
#define MTK_KPD_MEM1     (MTK_KPD_PA + 0x0004UL)

/* Colors */
#define COLOR_WHITE      0xffffffffU
#define COLOR_SILVER     0xffd8e0e8U
#define COLOR_GRAY       0xff8c96a4U
#define COLOR_DARK_GRAY  0xff4a5260U
#define COLOR_CYAN_GLOW  0xff00d4ffU
#define COLOR_CYAN_DIM   0xff006888U
#define COLOR_ANDROID    0xff3ddc84U
#define COLOR_APPLE      0xffffffffU
#define COLOR_BAR_BG     0xff222832U
#define COLOR_BAR_FG     0xff00d4ffU

static inline uint32_t rgb(int r, int g, int b)
{
	r = r < 0 ? 0 : (r > 255 ? 255 : r);
	g = g < 0 ? 0 : (g > 255 ? 255 : g);
	b = b < 0 ? 0 : (b > 255 ? 255 : b);
	return 0xff000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void put_pixel(volatile uint32_t *fb, uint32_t stride, int x, int y, uint32_t col)
{
	if (x >= 0 && x < 1080 && y >= 0 && y < 2400) {
		fb[(uint64_t)y * stride + x] = col;
	}
}

/* Draws a character from font8x16 with scaling */
static void clover_draw_char(volatile uint32_t *fb, uint32_t stride,
    int x0, int y0, char c, uint32_t fg, int scale)
{
	const unsigned char *g = font8x16[(c >= 0x20 && c < 0x7f ? c : '?') - 0x20];
	for (int gy = 0; gy < 16; gy++) {
		for (int sy = 0; sy < scale; sy++) {
			int py = y0 + gy * scale + sy;
			if (py < 0 || py >= 2400) continue;
			for (int gx = 0; gx < 8; gx++) {
				if (g[gy] & (0x80 >> gx)) {
					for (int sx = 0; sx < scale; sx++) {
						int px = x0 + gx * scale + sx;
						if (px >= 0 && px < 1080) {
							fb[(uint64_t)py * stride + px] = fg;
						}
					}
				}
			}
		}
	}
}

static int clover_str_len(const char *s)
{
	int n = 0;
	while (s && s[n]) n++;
	return n;
}

static void clover_draw_string(volatile uint32_t *fb, uint32_t stride,
    int x0, int y0, const char *str, uint32_t fg, int scale)
{
	while (str && *str) {
		clover_draw_char(fb, stride, x0, y0, *str, fg, scale);
		x0 += 8 * scale;
		str++;
	}
}

static void clover_draw_string_centered(volatile uint32_t *fb, uint32_t stride,
    int y0, const char *str, uint32_t fg, int scale, int w)
{
	int len = clover_str_len(str);
	int text_w = len * 8 * scale;
	int x0 = (w - text_w) / 2;
	clover_draw_string(fb, stride, x0, y0, str, fg, scale);
}

/* Draws a filled rectangle */
static void clover_fill_rect(volatile uint32_t *fb, uint32_t stride,
    int x0, int y0, int w, int h, uint32_t col)
{
	for (int y = y0; y < y0 + h; y++) {
		if (y < 0 || y >= 2400) continue;
		for (int x = x0; x < x0 + w; x++) {
			if (x >= 0 && x < 1080) {
				fb[(uint64_t)y * stride + x] = col;
			}
		}
	}
}

/* Brushed metal background with horizontal grain */
static void clover_draw_bg(volatile uint32_t *fb, uint32_t stride, int w, int h)
{
	for (int y = 0; y < h; y++) {
		/* Slate gradient: dark at top/bottom, lighter in center */
		int mid_dist = (y > 1100 ? y - 1100 : 1100 - y);
		int base = 48 - (mid_dist * 24 / 1300);
		if (base < 18) base = 18;

		volatile uint32_t *row = fb + (uint64_t)y * stride;
		for (int x = 0; x < w; x++) {
			/* Brushed aluminum grain noise */
			int grain = (int)((x * 13 + y * 29) % 7) - 3;
			int r = base + grain;
			int g = base + 4 + grain;
			int b = base + 10 + grain;
			row[x] = rgb(r, g, b);
		}
	}
}

/* Draws Clover-style metallic disk platter badge */
static void clover_draw_disk(volatile uint32_t *fb, uint32_t stride,
    int cx, int cy, int radius, int selected)
{
	int r2 = radius * radius;
	int inner_r2 = (radius - 12) * (radius - 12);

	for (int dy = -radius; dy <= radius; dy++) {
		int y = cy + dy;
		if (y < 0 || y >= 2400) continue;
		for (int dx = -radius; dx <= radius; dx++) {
			int d2 = dx * dx + dy * dy;
			if (d2 <= r2) {
				int x = cx + dx;
				if (x < 0 || x >= 1080) continue;

				if (d2 > inner_r2) {
					/* Beveled metallic ring */
					int rim_light = 120 - dy * 60 / radius;
					uint32_t rim_col = selected ?
					    rgb(rim_light / 2, rim_light, rim_light + 40) :
					    rgb(rim_light, rim_light, rim_light);
					fb[(uint64_t)y * stride + x] = rim_col;
				} else {
					/* Inner dark metallic face */
					int face = selected ? 38 - dy * 12 / radius : 24 - dy * 8 / radius;
					fb[(uint64_t)y * stride + x] = rgb(face, face + 4, face + 8);
				}
			}
		}
	}
}

/* Draws glowing selector box around the active badge */
static void clover_draw_selector(volatile uint32_t *fb, uint32_t stride, int cx, int cy, int size)
{
	int half = size / 2;
	int x0 = cx - half - 16;
	int y0 = cy - half - 16;
	int box_w = size + 32;
	int box_h = size + 32;

	/* Outer glow border (4 pixels thick) */
	for (int y = y0; y <= y0 + box_h; y++) {
		if (y < 0 || y >= 2400) continue;
		for (int x = x0; x <= x0 + box_w; x++) {
			if (x < 0 || x >= 1080) continue;
			int edge_x = (x < x0 + 4) || (x > x0 + box_w - 4);
			int edge_y = (y < y0 + 4) || (y > y0 + box_h - 4);
			if (edge_x || edge_y) {
				fb[(uint64_t)y * stride + x] = COLOR_CYAN_GLOW;
			}
		}
	}

	/* Indicator triangle beneath the badge */
	int tri_y = y0 + box_h + 12;
	for (int ty = 0; ty < 16; ty++) {
		int py = tri_y + ty;
		if (py < 0 || py >= 2400) continue;
		for (int tx = -ty; tx <= ty; tx++) {
			int px = cx + tx;
			if (px >= 0 && px < 1080) {
				fb[(uint64_t)py * stride + px] = COLOR_CYAN_GLOW;
			}
		}
	}
}

/* Draws stylized Apple logo icon inside disk */
static void clover_draw_apple_icon(volatile uint32_t *fb, uint32_t stride, int cx, int cy, int selected)
{
	uint32_t col = selected ? COLOR_WHITE : COLOR_GRAY;

	/* Apple body: overlapping rounded discs */
	for (int dy = -40; dy <= 45; dy++) {
		int y = cy + dy;
		if (y < 0 || y >= 2400) continue;
		for (int dx = -40; dx <= 40; dx++) {
			int x = cx + dx;
			if (x < 0 || x >= 1080) continue;

			/* Main body circle */
			int in_body = (dx * dx + (dy - 5) * (dy - 5) <= 38 * 38);
			/* Bite cutout on right side */
			int in_bite = ((dx - 32) * (dx - 32) + (dy - 10) * (dy - 10) <= 18 * 18);
			/* Bottom indent */
			int in_bot_notch = (dx * dx + (dy - 45) * (dy - 45) <= 12 * 12);
			/* Top indent */
			int in_top_notch = (dx * dx + (dy + 38) * (dy + 38) <= 10 * 10);

			if (in_body && !in_bite && !in_bot_notch && !in_top_notch) {
				fb[(uint64_t)y * stride + x] = col;
			}
		}
	}

	/* Leaf: oval tilted right */
	for (int dy = -62; dy <= -42; dy++) {
		int y = cy + dy;
		if (y < 0 || y >= 2400) continue;
		for (int dx = 0; dx <= 24; dx++) {
			int x = cx + dx;
			if (x < 0 || x >= 1080) continue;
			int d = (dx - 10) * (dx - 10) + (dy + 52) * (dy + 52);
			if (d <= 12 * 12) {
				fb[(uint64_t)y * stride + x] = col;
			}
		}
	}
}

/* Draws stylized Android Bugdroid icon inside disk */
static void clover_draw_android_icon(volatile uint32_t *fb, uint32_t stride, int cx, int cy, int selected)
{
	uint32_t col = selected ? COLOR_ANDROID : rgb(50, 140, 80);

	/* Head dome (half circle) */
	for (int dy = -45; dy <= -12; dy++) {
		int y = cy + dy;
		if (y < 0 || y >= 2400) continue;
		for (int dx = -38; dx <= 38; dx++) {
			int x = cx + dx;
			if (x < 0 || x >= 1080) continue;
			if (dx * dx + (dy + 12) * (dy + 12) <= 38 * 38) {
				/* Eyes cutout */
				int eye_left = ((dx + 16) * (dx + 16) + (dy + 26) * (dy + 26) <= 5 * 5);
				int eye_right = ((dx - 16) * (dx - 16) + (dy + 26) * (dy + 26) <= 5 * 5);
				if (!eye_left && !eye_right) {
					fb[(uint64_t)y * stride + x] = col;
				}
			}
		}
	}

	/* Head antennae */
	for (int i = 0; i < 18; i++) {
		put_pixel(fb, stride, cx - 18 - i, cy - 44 - i, col);
		put_pixel(fb, stride, cx - 17 - i, cy - 44 - i, col);
		put_pixel(fb, stride, cx + 18 + i, cy - 44 - i, col);
		put_pixel(fb, stride, cx + 17 + i, cy - 44 - i, col);
	}

	/* Body torso */
	for (int dy = -8; dy <= 38; dy++) {
		int y = cy + dy;
		if (y < 0 || y >= 2400) continue;
		for (int dx = -38; dx <= 38; dx++) {
			int x = cx + dx;
			if (x < 0 || x >= 1080) continue;
			fb[(uint64_t)y * stride + x] = col;
		}
	}
}

/* Renders the full Clover Dual-Boot UI */
static void clover_render_ui(volatile uint32_t *fb, uint32_t stride,
    int w, int h, int selected, int seconds_left, int paused)
{
	/* Background */
	clover_draw_bg(fb, stride, w, h);

	/* Header Title */
	clover_draw_string_centered(fb, stride, 180, "CLOVER BOOT MANAGER", COLOR_SILVER, 3, w);
	clover_draw_string_centered(fb, stride, 250, "HelioDarwin Bootloader  v1.0  |  Helio G99 MT6789", COLOR_GRAY, 2, w);

	/* Center row disk badges */
	int badge_y = 900;
	int apple_x = 320;
	int android_x = 760;
	int radius = 95;

	/* Draw Disks */
	clover_draw_disk(fb, stride, apple_x, badge_y, radius, selected == 0);
	clover_draw_disk(fb, stride, android_x, badge_y, radius, selected == 1);

	/* Draw OS Icons */
	clover_draw_apple_icon(fb, stride, apple_x, badge_y, selected == 0);
	clover_draw_android_icon(fb, stride, android_x, badge_y, selected == 1);

	/* Draw Selector on Active Badge */
	if (selected == 0) {
		clover_draw_selector(fb, stride, apple_x, badge_y, radius * 2);
	} else {
		clover_draw_selector(fb, stride, android_x, badge_y, radius * 2);
	}

	/* OS Badge Labels */
	clover_draw_string_centered(fb, stride, badge_y + radius + 45,
	    selected == 0 ? "iOS 16 (HelioDarwin)" : "iOS 16",
	    selected == 0 ? COLOR_WHITE : COLOR_DARK_GRAY, 2, apple_x * 2);
	clover_draw_string_centered(fb, stride, badge_y + radius + 45,
	    selected == 1 ? "Android (Slot B)" : "Android",
	    selected == 1 ? COLOR_WHITE : COLOR_DARK_GRAY, 2, android_x * 2 - (android_x - apple_x));

	/* Detailed OS Description Section */
	int desc_y = 1260;
	if (selected == 0) {
		clover_draw_string_centered(fb, stride, desc_y,
		    "Boot iOS 16.7.16 from iosroot", COLOR_WHITE, 3, w);
		clover_draw_string_centered(fb, stride, desc_y + 60,
		    "iPhone X (D22AP) persona | CoreAnimation | MaliMetal GPU", COLOR_GRAY, 2, w);
		clover_draw_string_centered(fb, stride, desc_y + 105,
		    "Target root: /dev/block/by-name/iosroot (HFS+/APFS)", COLOR_DARK_GRAY, 2, w);
	} else {
		clover_draw_string_centered(fb, stride, desc_y,
		    "Boot Android 12/13 from Slot B", COLOR_WHITE, 3, w);
		clover_draw_string_centered(fb, stride, desc_y + 60,
		    "Infinix XOS | Linux 5.10-mtk | Factory Firmware", COLOR_GRAY, 2, w);
		clover_draw_string_centered(fb, stride, desc_y + 105,
		    "Active slot switch via boot_control misc block", COLOR_DARK_GRAY, 2, w);
	}

	/* Countdown Timer Section */
	int timer_y = 1580;
	if (!paused && seconds_left > 0) {
		char msg[64];
		int n = 0;
		const char *prefix = "Booting iOS 16 automatically in ";
		while (*prefix) msg[n++] = *prefix++;
		msg[n++] = '0' + (seconds_left % 10);
		msg[n++] = 's';
		msg[n++] = '.';
		msg[n++] = '.';
		msg[n++] = '\0';

		clover_draw_string_centered(fb, stride, timer_y, msg, COLOR_CYAN_GLOW, 2, w);

		/* Countdown Progress Bar */
		int bar_w = 460;
		int bar_h = 10;
		int bar_x = (w - bar_w) / 2;
		clover_fill_rect(fb, stride, bar_x, timer_y + 40, bar_w, bar_h, COLOR_BAR_BG);
		int fill_w = (bar_w * seconds_left) / 5;
		clover_fill_rect(fb, stride, bar_x, timer_y + 40, fill_w, bar_h, COLOR_BAR_FG);
	} else {
		clover_draw_string_centered(fb, stride, timer_y,
		    "Automatic countdown paused", COLOR_GRAY, 2, w);
		clover_draw_string_centered(fb, stride, timer_y + 40,
		    "Press POWER button to boot selected OS", COLOR_WHITE, 2, w);
	}

	/* Bottom Clover Toolbar */
	int footer_y = 2150;
	clover_draw_string_centered(fb, stride, footer_y,
	    "[Vol +/-] Switch OS    [Power] Boot Selection", COLOR_SILVER, 2, w);
	clover_draw_string_centered(fb, stride, footer_y + 50,
	    "HelioDarwin Project - Built for Infinix Note 12 2023", COLOR_DARK_GRAY, 2, w);
}

int clover_menu(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h)
{
	int selected = 0;      /* 0: iOS 16, 1: Android */
	int paused = 0;
	int seconds_left = 5;
	uint32_t last_mem = 0xffffU;

	clover_render_ui(fb, stride_px, w, h, selected, seconds_left, paused);

	/* Countdown / key poll loop (ticks every 20ms) */
	for (int ticks = 0; ticks < 250 || paused; ticks++) {
		wait_ms(20);
		wdt_kick();

		/* Update countdown */
		if (!paused) {
			int rem = 5 - (ticks / 50);
			if (rem < 0) rem = 0;
			if (rem != seconds_left) {
				seconds_left = rem;
				clover_render_ui(fb, stride_px, w, h, selected, seconds_left, paused);
			}
			if (seconds_left <= 0) {
				break; /* Timer expired: boot selected */
			}
		}

		/* Poll MTK Keypad (0x10010004) */
		uint32_t mem = rd32(MTK_KPD_MEM1);
		uint32_t changed = mem ^ last_mem;
		last_mem = mem;

		/* Bit 0: Volume Up (active low 0) */
		if ((changed & 1) && !(mem & 1)) {
			selected = 0; /* iOS 16 */
			paused = 1;
			clover_render_ui(fb, stride_px, w, h, selected, 0, paused);
		}

		/* Bit 1: Volume Down (active low 0) */
		if ((changed & 2) && !(mem & 2)) {
			selected = 1; /* Android */
			paused = 1;
			clover_render_ui(fb, stride_px, w, h, selected, 0, paused);
		}

		/* Bit 8: Power Key (active low 0) -> Confirm selection */
		if ((changed & (1 << 8)) && !(mem & (1 << 8))) {
			break;
		}
	}

	return selected;
}
