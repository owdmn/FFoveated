/*
 * Copyright (C) 2019 Oliver Wiedemann
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "io.h"
#include "codec.h"
#include "pexit.h"
#include "window.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#ifdef ET
#include "et.h"
#include "iViewXAPI.h"
#endif

rdr_ctx *rc;
dec_ctx *src_dc, *fov_dc;
rep_enc_ctx *ec;
wtr_ctx *wt;

void display_usage(char *progname)
{
	printf("replicate a foveated video trial");
	printf("usage:\n$ %s source dest xcoords ycoords qp_offset sigma\n", progname);
}

int main(int argc, char **argv)
{
	char **xcoords, **ycoords, **qoffsets, **sigmas;
	SDL_Thread *reader, *src_decoder, *encoder, *writer;
	const int queue_capacity = 32;

	if (argc != 7) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	xcoords = parse_lines(argv[3]);
	ycoords = parse_lines(argv[4]);
	qoffsets = parse_lines(argv[5]);
	sigmas = parse_lines(argv[6]);

	printf(argv[1]);
	rc = reader_init(argv[1], queue_capacity);
	src_dc = source_decoder_init(rc, queue_capacity);
	ec = replicate_encoder_init(LIBX264, src_dc, xcoords, ycoords, qoffsets, sigmas);
	wt = writer_init(argv[2], ec->packets, rc->fctx, src_dc->avctx);

	reader = SDL_CreateThread(reader_thread, "reader", rc);
	src_decoder = SDL_CreateThread(decoder_thread, "src_decoder", src_dc);
	encoder = SDL_CreateThread(replicate_encoder_thread, "encoder", ec);
	writer = SDL_CreateThread(writer_thread, "writer", wt);

	SDL_WaitThread(reader, NULL);
	SDL_WaitThread(src_decoder, NULL);
	SDL_WaitThread(encoder, NULL);
	SDL_WaitThread(writer, NULL);


	return EXIT_SUCCESS;
}
