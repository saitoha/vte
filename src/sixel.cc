/*
 * Copyright (C) Hayaki Saito
 * originally written by kmiya@cluti (https://github.com/saitoha/sixel/blob/master/fromsixel.c)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>   /* isdigit */
#include <string.h>  /* memcpy */
#include <glib.h>

#include "sixel.h"

#define SIXEL_RGB(r, g, b) ((r) + ((g) << 8) +  ((b) << 16))
#define PALVAL(n,a,m) (((n) * (a) + ((m) / 2)) / (m))
#define SIXEL_XRGB(r,g,b) SIXEL_RGB(PALVAL(r, 255, 100), PALVAL(g, 255, 100), PALVAL(b, 255, 100))

static int const sixel_default_color_table[] = {
	SIXEL_XRGB(0,  0,  0),   /*  0 Black    */
	SIXEL_XRGB(20, 20, 80),  /*  1 Blue     */
	SIXEL_XRGB(80, 13, 13),  /*  2 Red      */
	SIXEL_XRGB(20, 80, 20),  /*  3 Green    */
	SIXEL_XRGB(80, 20, 80),  /*  4 Magenta  */
	SIXEL_XRGB(20, 80, 80),  /*  5 Cyan     */
	SIXEL_XRGB(80, 80, 20),  /*  6 Yellow   */
	SIXEL_XRGB(53, 53, 53),  /*  7 Gray 50% */
	SIXEL_XRGB(26, 26, 26),  /*  8 Gray 25% */
	SIXEL_XRGB(33, 33, 60),  /*  9 Blue*    */
	SIXEL_XRGB(60, 26, 26),  /* 10 Red*     */
	SIXEL_XRGB(33, 60, 33),  /* 11 Green*   */
	SIXEL_XRGB(60, 33, 60),  /* 12 Magenta* */
	SIXEL_XRGB(33, 60, 60),  /* 13 Cyan*    */
	SIXEL_XRGB(60, 60, 33),  /* 14 Yellow*  */
	SIXEL_XRGB(80, 80, 80),  /* 15 Gray 75% */
};

/*
 *  HLS-formatted color handling.
 *  (0 degree = blue, double-hexcone model)
 *  http://odl.sysworks.biz/disk$vaxdocdec021/progtool/d3qsaaa1.p64.bkb
 */
static int
hls_to_rgb(int hue, int lum, int sat)
{
    double min, max;
    int r, g, b;

    if (sat == 0) {
        r = g = b = lum;
    }

    /* https://wikimedia.org/api/rest_v1/media/math/render/svg/17e876f7e3260ea7fed73f69e19c71eb715dd09d */
    max = lum + sat * (100 - (lum > 50 ? 1 : -1) * ((lum << 1) - 100)) / 200.0;

    /* https://wikimedia.org/api/rest_v1/media/math/render/svg/f6721b57985ad83db3d5b800dc38c9980eedde1d */
    min = lum - sat * (100 - (lum > 50 ? 1 : -1) * ((lum << 1) - 100)) / 200.0;

    /* HLS hue color ring is roteted -120 degree from HSL's one. */
    hue = (hue + 240) % 360;

    /* https://wikimedia.org/api/rest_v1/media/math/render/svg/937e8abdab308a22ff99de24d645ec9e70f1e384 */
    switch (hue / 60) {
    case 0:  /* 0 <= hue < 60 */
        r = max;
        g = (min + (max - min) * (hue / 60.0));
        b = min;
        break;
    case 1:  /* 60 <= hue < 120 */
        r = min + (max - min) * ((120 - hue) / 60.0);
        g = max;
        b = min;
        break;
    case 2:  /* 120 <= hue < 180 */
        r = min;
        g = max;
        b = (min + (max - min) * ((hue - 120) / 60.0));
        break;
    case 3:  /* 180 <= hue < 240 */
        r = min;
        g = (min + (max - min) * ((240 - hue) / 60.0));
        b = max;
        break;
    case 4:  /* 240 <= hue < 300 */
        r = (min + (max - min) * ((hue - 240) / 60.0));
        g = min;
        b = max;
        break;
    case 5:  /* 300 <= hue < 360 */
    default:
        r = max;
        g = min;
        b = (min + (max - min) * ((360 - hue) / 60.0));
        break;
    }

    return SIXEL_XRGB(r, g, b);
}

static int
set_default_color(sixel_image_t *image)
{
	int i;
	int n;
	int r;
	int g;
	int b;

	/* palette initialization */
	for (n = 1; n < 17; n++) {
		image->palette[n] = sixel_default_color_table[n - 1];
	}

	/* colors 17-232 are a 6x6x6 color cube */
	for (r = 0; r < 6; r++) {
		for (g = 0; g < 6; g++) {
			for (b = 0; b < 6; b++) {
				image->palette[n++] = SIXEL_RGB(r * 51, g * 51, b * 51);
			}
		}
	}

	/* colors 233-256 are a grayscale ramp, intentionally leaving out */
	for (i = 0; i < 24; i++) {
		image->palette[n++] = SIXEL_RGB(i * 11, i * 11, i * 11);
	}

	for (; n < DECSIXEL_PALETTE_MAX; n++) {
		image->palette[n] = SIXEL_RGB(255, 255, 255);
	}

	return (0);
}

static int
sixel_image_init(
    sixel_image_t    *image,
    int              width,
    int              height,
    int              fgcolor,
    int              bgcolor,
    int              use_private_register)
{
	int status = (-1);
	size_t size;

	size = (size_t)(width * height) * sizeof(sixel_color_no_t);
	image->width = width;
	image->height = height;
	image->data = (sixel_color_no_t *)g_malloc(size);
	image->ncolors = 2;
	image->use_private_register = use_private_register;

	if (image->data == NULL) {
		status = (-1);
		goto end;
	}
	memset(image->data, 0, size);

	image->palette[0] = bgcolor;

	if (image->use_private_register)
		image->palette[1] = fgcolor;

	image->palette_modified = 0;

	status = (0);

end:
	return status;
}


static int
image_buffer_resize(
    sixel_image_t   *image,
    int              width,
    int              height)
{
	int status = (-1);
	size_t size;
	sixel_color_no_t *alt_buffer;
	int n;
	int min_height;

	size = (size_t)(width * height) * sizeof(sixel_color_no_t);
	alt_buffer = (sixel_color_no_t *)g_malloc(size);
	if (alt_buffer == NULL) {
		/* free source image */
		g_free(image->data);
		image->data = NULL;
		status = (-1);
		goto end;
	}

	min_height = height > image->height ? image->height: height;
	if (width > image->width) {  /* if width is extended */
		for (n = 0; n < min_height; ++n) {
			/* copy from source image */
			memcpy(alt_buffer + width * n,
			       image->data + image->width * n,
			       (size_t)image->width * sizeof(sixel_color_no_t));
			/* fill extended area with background color */
			memset(alt_buffer + width * n + image->width,
			       0,
			       (size_t)(width - image->width) * sizeof(sixel_color_no_t));
		}
	} else {
		for (n = 0; n < min_height; ++n) {
			/* copy from source image */
			memcpy(alt_buffer + width * n,
			       image->data + image->width * n,
			       (size_t)width * sizeof(sixel_color_no_t));
		}
	}

	if (height > image->height) {  /* if height is extended */
		/* fill extended area with background color */
		memset(alt_buffer + width * image->height,
		       0,
		       (size_t)(width * (height - image->height)) * sizeof(sixel_color_no_t));
	}

	/* free source image */
	g_free(image->data);

	image->data = alt_buffer;
	image->width = width;
	image->height = height;

	status = (0);

end:
	return status;
}

static void
sixel_image_deinit(sixel_image_t *image)
{
	g_free(image->data);
	image->data = NULL;
}

int
sixel_parser_init(sixel_state_t *st,
                  int fgcolor, int bgcolor,
                  int use_private_register)
{
	int status = (-1);

	st->state = PS_DCS;
	st->pos_x = 0;
	st->pos_y = 0;
	st->max_x = 0;
	st->max_y = 0;
	st->attributed_pan = 2;
	st->attributed_pad = 1;
	st->attributed_ph = 0;
	st->attributed_pv = 0;
	st->repeat_count = 1;
	st->color_index = 16;
	st->nparams = 0;
	st->param = 0;

	/* buffer initialization */
	status = sixel_image_init(&st->image, 1, 1, fgcolor, bgcolor, use_private_register);

	return status;
}

int
sixel_parser_set_default_color(sixel_state_t *st)
{
	return set_default_color(&st->image);
}

int
sixel_parser_finalize(sixel_state_t *st, unsigned char *pixels)
{
	int status = (-1);
	int sx;
	int sy;
	sixel_image_t *image = &st->image;
	int x, y;
	sixel_color_no_t *src;
	unsigned char *dst;
	int color;

	if (++st->max_x < st->attributed_ph)
		st->max_x = st->attributed_ph;

	if (++st->max_y < st->attributed_pv)
		st->max_y = st->attributed_pv;

	sx = st->max_x;
	sy = st->max_y;

	if (image->width > sx || image->height > sy) {
		status = image_buffer_resize(image, sx, sy);
		if (status < 0)
			goto end;
	}

	if (image->use_private_register && image->ncolors > 2 && !image->palette_modified) {
		status = set_default_color(image);
		if (status < 0)
			goto end;
	}

	src = st->image.data;
	dst = pixels;
	for (y = 0; y < st->image.height; ++y) {
		for (x = 0; x < st->image.width; ++x) {
			color = st->image.palette[*src++];
			*dst++ = color >> 16 & 0xff;   /* b */
			*dst++ = color >> 8 & 0xff;    /* g */
			*dst++ = color >> 0 & 0xff;    /* r */
			*dst++ = 0xff;                 /* a */
		}
	}

	status = (0);

end:
	return status;
}

/* convert sixel data into indexed pixel bytes and palette data */
int
sixel_parser_parse(sixel_state_t *st, unsigned char *p, size_t len)
{
	int status = (-1);
	int n;
	int i;
	int x;
	int y;
	int bits;
	int sixel_vertical_mask;
	int sx;
	int sy;
	int c;
	int pos;
	unsigned char *p0 = p;
	sixel_image_t *image = &st->image;

	if (! image->data)
		goto end;

	while (p < p0 + len) {
		switch (st->state) {
		case PS_ESC:
			switch (*p) {
			case '\\':
			case 0x9c:
				p++;
				break;
			case 'P':
				st->param = -1;
				st->state = PS_DCS;
				p++;
				break;
			default:
				p++;
				break;
			}
			goto end;
		case PS_DCS:
			switch (*p) {
			case 0x1b:
				st->state = PS_ESC;
				p++;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				if (st->param < 0)
					st->param = 0;
				st->param = st->param * 10 + *p - '0';
				p++;
				break;
			case ';':
				if (st->param < 0) {
					st->param = 0;
				}
				if (st->nparams < DECSIXEL_PARAMS_MAX) {
					st->params[st->nparams++] = st->param;
				}
				st->param = 0;
				p++;
				break;
			case 'q':
				if (st->param >= 0 && st->nparams < DECSIXEL_PARAMS_MAX) {
					st->params[st->nparams++] = st->param;
				}
				if (st->nparams > 0) {
					/* Pn1 */
					switch (st->params[0]) {
					case 0:
					case 1:
						st->attributed_pad = 2;
						break;
					case 2:
						st->attributed_pad = 5;
						break;
					case 3:
					case 4:
						st->attributed_pad = 4;
						break;
					case 5:
					case 6:
						st->attributed_pad = 3;
						break;
					case 7:
					case 8:
						st->attributed_pad = 2;
						break;
					case 9:
						st->attributed_pad = 1;
						break;
					default:
						st->attributed_pad = 2;
						break;
					}
				}

				if (st->nparams > 2) {
					/* Pn3 */
					if (st->params[2] == 0)
						st->params[2] = 10;
					st->attributed_pan = st->attributed_pan * st->params[2] / 10;
					st->attributed_pad = st->attributed_pad * st->params[2] / 10;
					if (st->attributed_pan <= 0)
						st->attributed_pan = 1;
					if (st->attributed_pad <= 0)
						st->attributed_pad = 1;
				}
				st->nparams = 0;
				st->state = PS_DECSIXEL;
				p++;
				break;
			default:
				p++;
				break;
			}
			break;

		case PS_DECSIXEL:
			switch (*p) {
			case '\x1b':
				st->state = PS_ESC;
				p++;
				break;
			case '"':
				st->param = 0;
				st->nparams = 0;
				st->state = PS_DECGRA;
				p++;
				break;
			case '!':
				st->param = 0;
				st->nparams = 0;
				st->state = PS_DECGRI;
				p++;
				break;
			case '#':
				st->param = 0;
				st->nparams = 0;
				st->state = PS_DECGCI;
				p++;
				break;
			case '$':
				/* DECGCR Graphics Carriage Return */
				st->pos_x = 0;
				p++;
				break;
			case '-':
				/* DECGNL Graphics Next Line */
				st->pos_x = 0;
				if (st->pos_y < DECSIXEL_HEIGHT_MAX - 5 - 6)
					st->pos_y += 6;
				else
					st->pos_y = DECSIXEL_HEIGHT_MAX + 1;
				p++;
				break;
			default:
				if (*p >= '?' && *p <= '~') {  /* sixel characters */
					if ((image->width < (st->pos_x + st->repeat_count) || image->height < (st->pos_y + 6))
					        && image->width < DECSIXEL_WIDTH_MAX && image->height < DECSIXEL_HEIGHT_MAX) {
						sx = image->width * 2;
						sy = image->height * 2;
						while (sx < (st->pos_x + st->repeat_count) || sy < (st->pos_y + 6)) {
							sx *= 2;
							sy *= 2;
						}

						if (sx > DECSIXEL_WIDTH_MAX)
							sx = DECSIXEL_WIDTH_MAX;
						if (sy > DECSIXEL_HEIGHT_MAX)
							sy = DECSIXEL_HEIGHT_MAX;

						status = image_buffer_resize(image, sx, sy);
						if (status < 0)
							goto end;
					}

					if (st->color_index > image->ncolors)
						image->ncolors = st->color_index;

					if (st->pos_x + st->repeat_count > image->width)
						st->repeat_count = image->width - st->pos_x;

					if (st->repeat_count > 0 && st->pos_y - 5 < image->height) {
						bits = *p - '?';
						if (bits != 0) {
							sixel_vertical_mask = 0x01;
							if (st->repeat_count <= 1) {
								for (i = 0; i < 6; i++) {
									if ((bits & sixel_vertical_mask) != 0) {
										pos = image->width * (st->pos_y + i) + st->pos_x;
										image->data[pos] = st->color_index;
										if (st->max_x < st->pos_x)
											st->max_x = st->pos_x;
										if (st->max_y < (st->pos_y + i))
											st->max_y = st->pos_y + i;
									}
									sixel_vertical_mask <<= 1;
								}
							} else {
								/* st->repeat_count > 1 */
								for (i = 0; i < 6; i++) {
									if ((bits & sixel_vertical_mask) != 0) {
										c = sixel_vertical_mask << 1;
										for (n = 1; (i + n) < 6; n++) {
											if ((bits & c) == 0)
												break;
											c <<= 1;
										}
										for (y = st->pos_y + i; y < st->pos_y + i + n; ++y) {
											for (x = st->pos_x; x < st->pos_x + st->repeat_count; ++x)
												image->data[image->width * y + x] = st->color_index;
										}
										if (st->max_x < (st->pos_x + st->repeat_count - 1))
											st->max_x = st->pos_x + st->repeat_count - 1;
										if (st->max_y < (st->pos_y + i + n - 1))
											st->max_y = st->pos_y + i + n - 1;
										i += (n - 1);
										sixel_vertical_mask <<= (n - 1);
									}
									sixel_vertical_mask <<= 1;
								}
							}
						}
					}
					if (st->repeat_count > 0)
						st->pos_x += st->repeat_count;
					st->repeat_count = 1;
				}
				p++;
				break;
			}
			break;

		case PS_DECGRA:
			/* DECGRA Set Raster Attributes " Pan; Pad; Ph; Pv */
			switch (*p) {
			case '\x1b':
				st->state = PS_ESC;
				p++;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				st->param = st->param * 10 + *p - '0';
				if (st->param > DECSIXEL_PARAMVALUE_MAX)
					st->param = DECSIXEL_PARAMVALUE_MAX;
				p++;
				break;
			case ';':
				if (st->nparams < DECSIXEL_PARAMS_MAX)
					st->params[st->nparams++] = st->param;
				st->param = 0;
				p++;
				break;
			default:
				if (st->nparams < DECSIXEL_PARAMS_MAX)
					st->params[st->nparams++] = st->param;
				if (st->nparams > 0)
					st->attributed_pad = st->params[0];
				if (st->nparams > 1)
					st->attributed_pan = st->params[1];
				if (st->nparams > 2 && st->params[2] > 0)
					st->attributed_ph = st->params[2];
				if (st->nparams > 3 && st->params[3] > 0)
					st->attributed_pv = st->params[3];

				if (st->attributed_pan <= 0)
					st->attributed_pan = 1;
				if (st->attributed_pad <= 0)
					st->attributed_pad = 1;

				if (image->width < st->attributed_ph ||
				        image->height < st->attributed_pv) {
					sx = st->attributed_ph;
					if (image->width > st->attributed_ph)
						sx = image->width;

					sy = st->attributed_pv;
					if (image->height > st->attributed_pv)
						sy = image->height;

					if (sx > DECSIXEL_WIDTH_MAX)
						sx = DECSIXEL_WIDTH_MAX;
					if (sy > DECSIXEL_HEIGHT_MAX)
						sy = DECSIXEL_HEIGHT_MAX;

					status = image_buffer_resize(image, sx, sy);
					if (status < 0)
						goto end;
				}
				st->state = PS_DECSIXEL;
				st->param = 0;
				st->nparams = 0;
			}
			break;

		case PS_DECGRI:
			/* DECGRI Graphics Repeat Introducer ! Pn Ch */
			switch (*p) {
			case '\x1b':
				st->state = PS_ESC;
				p++;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				st->param = st->param * 10 + *p - '0';
				if (st->param > DECSIXEL_PARAMVALUE_MAX)
					st->param = DECSIXEL_PARAMVALUE_MAX;
				p++;
				break;
			default:
				st->repeat_count = st->param;
				if (st->repeat_count == 0)
					st->repeat_count = 1;
				st->state = PS_DECSIXEL;
				st->param = 0;
				st->nparams = 0;
				break;
			}
			break;

		case PS_DECGCI:
			/* DECGCI Graphics Color Introducer # Pc; Pu; Px; Py; Pz */
			switch (*p) {
			case '\x1b':
				st->state = PS_ESC;
				p++;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				st->param = st->param * 10 + *p - '0';
				if (st->param > DECSIXEL_PARAMVALUE_MAX)
					st->param = DECSIXEL_PARAMVALUE_MAX;
				p++;
				break;
			case ';':
				if (st->nparams < DECSIXEL_PARAMS_MAX)
					st->params[st->nparams++] = st->param;
				st->param = 0;
				p++;
				break;
			default:
				st->state = PS_DECSIXEL;
				if (st->nparams < DECSIXEL_PARAMS_MAX)
					st->params[st->nparams++] = st->param;
				st->param = 0;

				if (st->nparams > 0) {
					st->color_index = 1 + st->params[0];  /* offset 1(background color) added */
					if (st->color_index < 0)
						st->color_index = 0;
					else if (st->color_index >= DECSIXEL_PALETTE_MAX)
						st->color_index = DECSIXEL_PALETTE_MAX - 1;
				}

				if (st->nparams > 4) {
					st->image.palette_modified = 1;
					if (st->params[1] == 1) {
						/* HLS */
						if (st->params[2] > 360)
							st->params[2] = 360;
						if (st->params[3] > 100)
							st->params[3] = 100;
						if (st->params[4] > 100)
							st->params[4] = 100;
						image->palette[st->color_index]
						    = hls_to_rgb(st->params[2], st->params[3], st->params[4]);
					} else if (st->params[1] == 2) {
						/* RGB */
						if (st->params[2] > 100)
							st->params[2] = 100;
						if (st->params[3] > 100)
							st->params[3] = 100;
						if (st->params[4] > 100)
							st->params[4] = 100;
						image->palette[st->color_index]
						    = SIXEL_XRGB(st->params[2], st->params[3], st->params[4]);
					}
				}
				break;
			}
			break;
		default:
			break;
		}
	}

	status = (0);

end:
	return status;
}

void
sixel_parser_deinit(sixel_state_t *st)
{
	sixel_image_deinit(&st->image);
}
