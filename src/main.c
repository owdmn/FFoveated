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
#include "decoding.h"
#include "encoding.h"

#ifdef ET
#include "et.h"
#include "iViewXAPI.h"
gaze_struct *gaze;
tracker_struct *tracker;
#endif

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
void event_loop(window_context *w_ctx)
{
	SDL_Event event;
	int queue_drained = 0;

	if (w_ctx->time_start != -1)
		pexit("Error: call set_timing first");

	for (;;) {
		/* check for events to handle, meanwhile just render frames */
		while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
			if (frame_refresh(w_ctx)) {
				queue_drained = 1;
				break;
			}
			SDL_PumpEvents();
		}
		if (queue_drained)
			break;

		switch (event.type) {
		case SDL_KEYDOWN:
			switch (event.key.keysym.sym) {
			case SDLK_q:
				pexit("q pressed");
			}
			break;
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_RESIZED:
				w_ctx->width = event.window.data1;
				w_ctx->height = event.window.data2;
				SDL_DestroyTexture(w_ctx->texture);
				w_ctx->texture = NULL;
			break;
			}
			break;
		case SDL_MOUSEMOTION:
			SDL_GetMouseState(&w_ctx->mouse_x, &w_ctx->mouse_y);
			break;
		}

	}
}

int main(int argc, char **argv)
{
	char **video_paths;
	float screen_width, screen_height; //physical display dimensions in mm
	reader_context *r_ctx;
	decoder_context *source_d_ctx, *fov_d_ctx;
	encoder_context *e_ctx;
	window_context *w_ctx;
	SDL_Thread *reader, *source_decoder, *encoder, *fov_decoder;
	const int queue_capacity = 32;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	// FIXME: Hardcoded 15.6" 16:9 FHD notebook display dimensions
	screen_width = 345;
	screen_height = 194;

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

#ifdef ET
	setup_ivx();
#endif

	video_paths = parse_lines(argv[1]);
	w_ctx = window_init(screen_width, screen_height);

	for (int i = 0; video_paths[i]; i++) {

		r_ctx = reader_init(video_paths[i], queue_capacity);
		source_d_ctx = source_decoder_init(r_ctx, queue_capacity);
		e_ctx = encoder_init(LIBX265, source_d_ctx, 1, w_ctx);
		fov_d_ctx = fov_decoder_init(e_ctx->packet_queue);

		reader = SDL_CreateThread(reader_thread, "reader_thread", r_ctx);
		source_decoder = SDL_CreateThread(decoder_thread, "source_decoder_thread", source_d_ctx);
		encoder = SDL_CreateThread(encoder_thread, "encoder_thread", e_ctx);
		fov_decoder = SDL_CreateThread(decoder_thread, "fov_decoder_thread", fov_d_ctx);

		set_window_queues(w_ctx, fov_d_ctx->frame_queue, e_ctx->lag_queue);
		set_window_timing(w_ctx, source_d_ctx->avctx->time_base);
		event_loop(w_ctx);

		SDL_WaitThread(reader, NULL);
		SDL_WaitThread(source_decoder, NULL);
		SDL_WaitThread(encoder, NULL);
		SDL_WaitThread(fov_decoder, NULL);

		decoder_free(&fov_d_ctx);
		encoder_free(&e_ctx);
		decoder_free(&source_d_ctx);
		reader_free(&r_ctx);
	}

	free_lines(&video_paths);
	return EXIT_SUCCESS;
}
