/*
 * Boot splash. With a logo image at build time (mklogo.py -> out/logo.h,
 * HAVE_LOGO_IMAGE) the image is drawn as is; otherwise a procedural apple,
 * antialiased. The body outline is a closed Catmull-Rom
 * spline through hand-placed control points (right half below, mirrored for
 * the left; the stem notch is part of it), flattened into a polygon; the
 * bite is a circle cut out of it, the leaf a lens tilted by 40 degrees.
 * Each pixel takes 4x4 samples; coverage becomes a grey level.
 *
 * Fixed point throughout: 1/64 of a logo unit, where 100 units = LOGO_S
 * pixels and y points down. No FPU (-mgeneral-regs-only).
 */
#include "splash.h"

#define LOGO_S		240	/* pixels per 100 logo units */
#define SUB		4	/* samples per pixel per axis */
#define FIX		64	/* fixed-point steps per logo unit */
#define SEG		12	/* polygon segments per spline span */
#define MAX_X		8	/* outline crossings per scanline */

/*
 * Right half of the body outline, from the top notch to the bottom dip, in
 * HALF logo units (unit = 1/124 of the body width). Sampled off a reference
 * silhouette with 24 rays at equal angles from the body centre, which keeps
 * the control points evenly spaced and the spline free of ripples.
 */
static const int16_t half[][2] = {
	{ 0, -86 }, { 13, -88 }, { 29, -95 }, { 47, -99 }, { 64, -97 },
	{ 81, -92 }, { 95, -82 }, { 108, -69 }, { 116, -54 }, { 122, -37 },
	{ 124, -20 }, { 124, -3 }, { 123, 14 }, { 120, 29 }, { 115, 44 },
	{ 109, 59 }, { 103, 73 }, { 94, 86 }, { 85, 99 }, { 75, 111 },
	{ 62, 122 }, { 46, 125 }, { 29, 121 }, { 13, 115 }, { 0, 113 },
};
#define NHALF		(int)(sizeof(half) / sizeof(half[0]))
#define NCTRL		(2 * NHALF - 2)
#define NPOLY		(NCTRL * SEG)

static int32_t poly[NPOLY][2];

/* Control point i of the closed loop: right half, then the left mirrored. */
static void ctrl(int i, int32_t *x, int32_t *y)
{
	i = (i % NCTRL + NCTRL) % NCTRL;
	if (i < NHALF) {
		*x = half[i][0] * FIX / 2;
		*y = half[i][1] * FIX / 2;
	} else {
		*x = -half[NCTRL - i][0] * FIX / 2;
		*y = half[NCTRL - i][1] * FIX / 2;
	}
}

/* Catmull-Rom point at t = k / SEG between p1 and p2, integer only. */
static int32_t cr(int32_t p0, int32_t p1, int32_t p2, int32_t p3, int64_t k)
{
	int64_t n = SEG, t = k, t2 = k * k, t3 = k * k * k;
	int64_t v = 2 * p1 * n * n * n + (-p0 + p2) * t * n * n +
		    (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 * n +
		    (-p0 + 3 * p1 - 3 * p2 + p3) * t3;
	return (int32_t)(v / (2 * n * n * n));
}

static void build_outline(void)
{
	for (int i = 0; i < NCTRL; i++) {
		int32_t x[4], y[4];
		for (int j = 0; j < 4; j++)
			ctrl(i - 1 + j, &x[j], &y[j]);
		for (int k = 0; k < SEG; k++) {
			poly[i * SEG + k][0] = cr(x[0], x[1], x[2], x[3], k);
			poly[i * SEG + k][1] = cr(y[0], y[1], y[2], y[3], k);
		}
	}
}

/* Sorted x of every outline edge crossing height y (even-odd spans). */
static int crossings(int32_t y, int32_t *xs)
{
	int n = 0;

	for (int i = 0; i < NPOLY && n < MAX_X; i++) {
		const int32_t *a = poly[i], *b = poly[(i + 1) % NPOLY];
		if ((a[1] > y) == (b[1] > y))
			continue;
		int32_t x = a[0] + (int32_t)((int64_t)(y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]));
		int j = n++;
		for (; j > 0 && xs[j - 1] > x; j--)
			xs[j] = xs[j - 1];
		xs[j] = x;
	}
	return n;
}

static int in_spans(int32_t x, const int32_t *xs, int n)
{
	int in = 0;
	for (int i = 0; i < n && xs[i] <= x; i++)
		in = !in;
	return in;
}

static int in_circle(int64_t x, int64_t y, int64_t cx, int64_t cy, int64_t r)
{
	return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

/* cos/sin of 40 degrees, scaled by 1024 */
#define ROT_C	784
#define ROT_S	658

/*
 * Leaf: a lens (vesica), the intersection of two circles of radius R whose
 * centres sit LEAF_D either side of the long axis: half length
 * sqrt(R^2 - D^2) = 22.6, half width R - D = 9 logo units.
 */
#define LEAF_R	33
#define LEAF_D	24

static int in_leaf(int64_t x, int64_t y)
{
	int64_t lx = x - 12 * FIX, ly = y + 69 * FIX;
	int64_t rx = (lx * ROT_C + ly * ROT_S) / 1024;
	int64_t ry = (-lx * ROT_S + ly * ROT_C) / 1024;
	return in_circle(rx, ry, -LEAF_D * FIX, 0, LEAF_R * FIX) &&
	       in_circle(rx, ry, LEAF_D * FIX, 0, LEAF_R * FIX);
}

/* Pixel offset (in 1/SUB pixels, sample centre) -> fixed logo units. */
static int32_t to_fix(int64_t sub_px)
{
	return (int32_t)((2 * sub_px + 1) * 100 * FIX / (2 * LOGO_S * SUB));
}

#ifdef HAVE_LOGO_IMAGE
#include "logo.h"	/* mklogo.py: LOGO_W x LOGO_H alpha as (count, value) runs */

/* White over black by the image's alpha, centred where the apple goes. */
static void draw_image(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h)
{
	uint32_t x0 = (w - LOGO_W) / 2, y0 = h * 9 / 20 - LOGO_H / 2;
	const uint8_t *run = logo_rle;
	uint32_t left = 0, g = 0;

	for (uint32_t py = 0; py < h; py++) {
		volatile uint32_t *row = fb + (uint64_t)py * stride_px;
		int band = py >= y0 && py < y0 + LOGO_H;

		for (uint32_t px = 0; px < w; px++) {
			uint32_t v = 0;

			if (band && px >= x0 && px < x0 + LOGO_W) {
				if (!left) {
					left = run[0];
					g = run[1];
					run += 2;
				}
				left--;
				v = g;
			}
			row[px] = 0xff000000U | v << 16 | v << 8 | v;
		}
	}
}
#endif

/* Fallback when the build has no logo image. */
static void __attribute__((unused))
draw_procedural(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h)
{
	int64_t cx = w / 2, cy = h * 9 / 20;
	int64_t x_lo = cx - 64 * LOGO_S / 100, x_hi = cx + 64 * LOGO_S / 100;
	int64_t y_lo = cy - 98 * LOGO_S / 100, y_hi = cy + 76 * LOGO_S / 100;
	int32_t xs[SUB][MAX_X];
	int nx[SUB];

	build_outline();
	for (uint32_t py = 0; py < h; py++) {
		volatile uint32_t *row = fb + (uint64_t)py * stride_px;
		int band = py >= y_lo && py <= y_hi;
		int32_t sy[SUB];

		if (band) {
			for (int j = 0; j < SUB; j++) {
				sy[j] = to_fix(((int64_t)py - cy) * SUB + j);
				nx[j] = crossings(sy[j], xs[j]);
			}
		}
		for (uint32_t px = 0; px < w; px++) {
			unsigned cover = 0;

			if (band && px >= x_lo && px <= x_hi) {
				for (int i = 0; i < SUB; i++) {
					int32_t sx = to_fix(((int64_t)px - cx) * SUB + i);
					for (int j = 0; j < SUB; j++) {
						int body = in_spans(sx, xs[j], nx[j]) &&
						    !in_circle(sx, sy[j], 73 * FIX, -6 * FIX, 32 * FIX);
						cover += body || in_leaf(sx, sy[j]);
					}
				}
			}
			uint32_t g = cover * 255 / (SUB * SUB);
			row[px] = 0xff000000U | g << 16 | g << 8 | g;
		}
	}
}

void splash_draw(volatile uint32_t *fb, uint32_t stride_px, uint32_t w, uint32_t h)
{
#ifdef HAVE_LOGO_IMAGE
	draw_image(fb, stride_px, w, h);
#else
	draw_procedural(fb, stride_px, w, h);
#endif
}
