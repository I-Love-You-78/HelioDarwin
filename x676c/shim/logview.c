/*
 * Previous-boot log on the panel: the last screenful of the ramoops console
 * zone, oldest line at the top, 8x16 glyphs doubled to 16x32 (67 columns x
 * 75 rows on 1080x2400). The zone is a ring (header sig/start/size, see
 * plog.c): with size < capacity the text is data[0..size), else it starts
 * at `start` and wraps.
 */
#include "logview.h"
#include "font.h"

#define PRZ_BASE	0x4811f000UL	/* same zone as plog.c */
#define PRZ_CAP		(0x40000UL - 12)
#define PRZ_SIG		0x43474244U

#define SCALE		2
#define CELL_W		(8 * SCALE)
#define CELL_H		(16 * SCALE)
#define FG		0xffe0e0e0U
#define BG		0xff000000U
#define HDR_FG		0xff40ff40U

struct ring {
	volatile const char *data;
	uint32_t start, size, cap;
};

static char ring_at(const struct ring *r, uint32_t i)
{
	return r->data[r->size < r->cap ? i : (r->start + i) % r->cap];
}

static void put_glyph(volatile uint32_t *fb, uint32_t stride, uint32_t col, uint32_t row,
    char c, uint32_t fg)
{
	const unsigned char *g = font8x16[(c >= 0x20 && c < 0x7f ? c : '?') - 0x20];

	for (uint32_t gy = 0; gy < 16; gy++) {
		for (uint32_t sy = 0; sy < SCALE; sy++) {
			volatile uint32_t *line = fb + (uint64_t)(row * CELL_H + gy * SCALE + sy) * stride +
			    col * CELL_W;
			for (uint32_t gx = 0; gx < 8; gx++) {
				uint32_t px = (g[gy] & (0x80 >> gx)) ? fg : BG;	/* PSF: MSB is the left pixel */
				for (uint32_t sx = 0; sx < SCALE; sx++)
					line[gx * SCALE + sx] = px;
			}
		}
	}
}

/* Index where the last `want` wrapped lines of the text begin. */
static uint32_t tail_start(const struct ring *r, uint32_t cols, uint32_t want)
{
	uint32_t i = r->size, lines = 0, col = 0;

	if (i && ring_at(r, i - 1) == '\n')
		i--;	/* a final newline does not open an empty line */
	while (i > 0) {
		char c = ring_at(r, i - 1);
		if (c == '\n') {
			if (++lines == want)
				return i;
			col = 0;
		} else if (c != '\r' && ++col > cols) {
			if (++lines == want)
				return i;
			col = 1;
		}
		i--;
	}
	return 0;
}

void logview_render(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h,
    volatile const char *data, uint32_t start, uint32_t size, uint32_t cap)
{
	struct ring r = { data, start, size, cap };
	uint32_t cols = w / CELL_W, rows = h / CELL_H;
	static const char title[] = "previous boot log (pstore):";

	for (uint32_t y = 0; y < h; y++)
		for (uint32_t x = 0; x < w; x++)
			fb[(uint64_t)y * stride_px + x] = BG;
	for (uint32_t i = 0; title[i]; i++)
		put_glyph(fb, stride_px, i, 0, title[i], HDR_FG);

	uint32_t row = 1, col = 0;
	for (uint32_t i = tail_start(&r, cols, rows - 1); i < size && row < rows; i++) {
		char c = ring_at(&r, i);
		if (c == '\r')
			continue;
		if (c == '\n') {
			row++;
			col = 0;
			continue;
		}
		if (col == cols) {
			if (++row == rows)
				break;
			col = 0;
		}
		put_glyph(fb, stride_px, col++, row, c, FG);
	}
}

int logview_show(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h)
{
	volatile const uint32_t *hdr = (volatile const uint32_t *)PRZ_BASE;
	uint32_t start = hdr[1], size = hdr[2];

	if (hdr[0] != PRZ_SIG || !size || size > PRZ_CAP || start >= PRZ_CAP)
		return 0;
	logview_render(fb, stride_px, w, h, (volatile const char *)(PRZ_BASE + 12), start, size, PRZ_CAP);
	return 1;
}
