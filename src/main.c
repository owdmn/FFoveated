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
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>

#ifdef ET
#include "et.h"
#include "iViewXAPI.h"
#endif

rdr_ctx *rc;
dec_ctx *src_dc, *fov_dc;
enc_ctx *ec;
win_ctx *wc;

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
void event_loop(void)
{
	SDL_Event event;

	if (wc->time_start != -1)
		pexit("Error: call set_timing first");

	rc->abort = 0;

	for (;;) {
		/* check for events to handle, meanwhile just render frames */
		if (frame_refresh(wc))
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
					rc->abort = 1;
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

	SDL_Thread *reader, *src_decoder, *encoder, *fov_decoder;
	const int queue_capacity = 32;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	paths = parse_lines(argv[1]);
	wc = window_init();
	setup_ivx(wc->window, LIBX264);

	for (int i = 0; paths[i]; i++) {

		rc = reader_init(paths[i], queue_capacity);
		src_dc = source_decoder_init(rc, queue_capacity);
		ec = encoder_init(LIBX264, src_dc);
		fov_dc = fov_decoder_init(ec);

		reader = SDL_CreateThread(reader_thread, "reader", rc);
		src_decoder = SDL_CreateThread(decoder_thread, "src_decoder", src_dc);
		encoder = SDL_CreateThread(encoder_thread, "encoder", ec);
		fov_decoder = SDL_CreateThread(decoder_thread, "fov_decoder", fov_dc);

		set_window_source(wc, fov_dc->frames, ec->timestamps, src_dc->avctx->time_base);
		event_loop();

		SDL_WaitThread(reader, NULL);
		printf("here\n");
		SDL_WaitThread(src_decoder, NULL);
		SDL_WaitThread(encoder, NULL);
		SDL_WaitThread(fov_decoder, NULL);

		decoder_free(&fov_dc);
		encoder_free(&ec);
		decoder_free(&src_dc);
		reader_free(&rc);
	}

	free_lines(&paths);
	return EXIT_SUCCESS;
}
