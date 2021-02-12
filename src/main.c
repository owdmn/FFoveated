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
enc_ctx *ec;
win_ctx *wc;

void display_usage(int argc, char *progname)
{
	if (argc != 2) {
		printf("usage:\n$ %s videofile \n", progname);
		exit(EXIT_FAILURE);
	}
}

/**
 * Loop: Render frames and react to events.
 * Calls pexit in case of a failure.
 */
void event_loop(int run)
{
	SDL_Event event;
	int fn = 0; //frame number
	int fps = src_dc->frame_rate.num / src_dc->frame_rate.den;
	char msgbuf[1024];

	static float qp_offset = 0;
	set_qp_offset(qp_offset);

	int reduce[] =  {10,  5,  3,  2,  2, 1, 1, 1, 1, 1};
	int lift[] =    {25, 20, 17, 15, 10, 8, 8, 5, 5, 5};
	int delay[] =   { 1,  1,  1,  1,  1, 2, 2, 2, 2, 2};

	fprintf(stderr, "fps: %d", fps);
	if (fps > 60 || fps < 22) {
		pexit("questionable frame rate");
	}

	if (wc->time_start != -1)
		pexit("Error: call set_timing first");

	while (1) {
		fn++;
		// check for events to handle, meanwhile just render frames
		if (frame_refresh(wc))
			break;

		if (fn % (fps * delay[run]) == 0) {
			// reduce qp every second by reduce[run]
			qp_offset = get_qp_offset() + reduce[run];
			set_qp_offset(qp_offset);
		}

		SDL_PumpEvents();
		while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
			switch (event.type) {
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_q:
					pexit("q pressed");
					break;
				case SDLK_SPACE:
					rc->abort = 1;
					wc->abort = 1;
					//increase by upfkt[3un]
					printf("run: %d, qp_offset: %f, lift[run]: %d\n", run, qp_offset, lift[run]);
					qp_offset =  (qp_offset - lift[run]) > 0 ? (qp_offset - lift[run]) : 0;
					sprintf(msgbuf, "space pressed, setting qp_offset to: %f", qp_offset);
					#ifdef ET
					log_message(ec, msgbuf);
					#endif
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

	display_usage(argc, argv[0]);

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	setup_ivx(LIBX264);
	wc = window_init();
	set_ivx_window(wc->window);

	for (int run = 0; run < 10; run++) {
		rc = reader_init(argv[1], queue_capacity);
		src_dc = source_decoder_init(rc, queue_capacity);
		ec = encoder_init(LIBX264, src_dc, argv[1]);
		fov_dc = fov_decoder_init(ec);

		reader = SDL_CreateThread(reader_thread, "reader", rc);
		src_decoder = SDL_CreateThread(decoder_thread, "src_decoder", src_dc);
		encoder = SDL_CreateThread(encoder_thread, "encoder", ec);
		fov_decoder = SDL_CreateThread(decoder_thread, "fov_decoder", fov_dc);

		SDL_DetachThread(reader);
		SDL_DetachThread(src_decoder);
		SDL_DetachThread(encoder);
		SDL_DetachThread(fov_decoder);

		SDL_SetWindowFullscreen(wc->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_RaiseWindow(wc->window);
		set_window_source(wc, fov_dc->frames, ec->timestamps, src_dc->avctx->time_base);
		event_loop(0);
		pause(wc->window);
	}

	free_lines(&paths);
	return EXIT_SUCCESS;
}
