/* Host preview of splash.c: writes raw a8r8g8b8 1080x2400 to argv[1]. */
#include <stdio.h>
#include <stdlib.h>
#include "splash.h"

#define W 1080
#define H 2400

int main(int argc, char **argv)
{
	uint32_t *fb = malloc(W * H * 4);
	FILE *f;

	if (argc < 2 || !fb || !(f = fopen(argv[1], "wb")))
		return 1;
	splash_draw(fb, W, W, H);
	fwrite(fb, 4, W * H, f);
	fclose(f);
	return 0;
}
