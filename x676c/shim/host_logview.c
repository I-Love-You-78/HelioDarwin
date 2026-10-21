/* Host preview of logview.c: renders a text file as the panel would show it. */
#include <stdio.h>
#include <stdlib.h>
#include "logview.h"

#define W 1080
#define H 2400

int main(int argc, char **argv)
{
	static char text[1 << 20];
	uint32_t *fb = malloc(W * H * 4);
	FILE *in, *out;

	if (argc < 3 || !fb || !(in = fopen(argv[1], "rb")) || !(out = fopen(argv[2], "wb")))
		return 1;
	uint32_t n = (uint32_t)fread(text, 1, sizeof(text), in);
	logview_render(fb, W, W, H, text, n, n, sizeof(text));
	fwrite(fb, 4, W * H, out);
	fclose(out);
	return 0;
}
