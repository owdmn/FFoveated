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

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
#include "io.h"
#include "codec.h"

#ifdef ET
#include "et.h"
#include "iViewXAPI.h"
#endif

typedef struct {
	rdr_ctx *rc;
	dec_ctx *src_dc, *fov_dc;
	enc_ctx *ec;
	win_ctx *wc;
} ctx;

ctx c;


void display_usage(char *progname)
{
	printf("usage:\n$ %s infile\n", progname);
}

/**
 * Loop: Render frames and react to events.
 *
 * Calls pexit in case of a failure.
 * @param w_ctx window_context to which rendering and event handling applies.
 */
void event_loop()
{
	SDL_Event event;

	if (c.wc->time_start != -1)
		pexit("Error: call set_timing first");

	for (;;) {
		/* check for events to handle, meanwhile just render frames */
		if (frame_refresh(c.wc))
			break;

		SDL_PumpEvents();
		if (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_q:
					pexit("q pressed");
					break;
				case SDLK_SPACE:
					fprintf(stderr, "space pressed\n");
					c.rc->abort = 1;
					break;
			}
			break;
			}
		}

	}
}

int main(int argc, char **argv)
{
	char **paths;
	enc_id id;

	SDL_Thread *reader, *src_decoder, *encoder, *fov_decoder;
	const int queue_capacity = 32;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	id = LIBX264;

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	paths = parse_lines(argv[1]);
	c.wc = window_init();
	setup_ivx(c.wc->window, id);

	for (int i = 0; paths[i]; i++) {

		c.rc = reader_init(paths[i], queue_capacity);
		c.src_dc = source_decoder_init(c.rc, queue_capacity);
		c.ec = encoder_init(id, c.src_dc);
		c.fov_dc = fov_decoder_init(c.ec);

		reader = SDL_CreateThread(reader_thread, "reader", c.rc);
		src_decoder = SDL_CreateThread(decoder_thread, "src_decoder", c.src_dc);
		encoder = SDL_CreateThread(encoder_thread, "encoder", c.ec);
		fov_decoder = SDL_CreateThread(decoder_thread, "fov_decoder", c.fov_dc);

		set_window_source(c.wc, c.fov_dc->frames, c.ec->timestamps, c.src_dc->avctx->time_base);
		event_loop(&c);

		SDL_WaitThread(reader, NULL);
		printf("here\n");
		SDL_WaitThread(src_decoder, NULL);
		SDL_WaitThread(encoder, NULL);
		SDL_WaitThread(fov_decoder, NULL);

		decoder_free(&c.fov_dc);
		encoder_free(&c.ec);
		decoder_free(&c.src_dc);
		reader_free(&c.rc);
	}

	free_lines(&paths);
	return EXIT_SUCCESS;
}
