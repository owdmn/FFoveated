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

#ifdef __MINGW32__
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
 * Set the frame queue for the given window context to q
 *
 * @param q frame queue
 * @param w_ctx to be updated
 */
void window_set_queues(window_context *w_ctx, Queue *frames, Queue *lags)
{
	w_ctx->frame_queue = frames;
	w_ctx->lag_queue = lags;
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

/**
 * Set the time_base for the presentation window context.
 * Sets time_start to -1, which is required before entering event_loop.
 *
 * @param w_ctx the window context to be updated
 * @param d_ctx the cont ext of the original decoder
 */
void set_timing(window_context *w_ctx, decoder_context *d_ctx)
{
	w_ctx->time_base = d_ctx->avctx->time_base;
	w_ctx->time_start = -1;
}

#ifdef __MINGW32__
int __stdcall update_gaze(struct SampleStruct sampleData)
{
	int screen_x, screen_y;


	if (!gaze)
		gaze = malloc(sizeof(gaze_struct))
		pexit("gaze struct not initialized")

	screen_x = (sampleData.leftEye.gazeX + sampleData.rightEye.gazeX) / 2;
	screen_y = (sampleData.leftEye.gazeY + sampleData.rightEye.gazeY) / 2;


	SDL_LockMutex(gaze->mutex);
	gaze->

	SDL_UnlockMutex(gaze->mutex);
	return 0;
}

void setup_ivx(void)
{

	struct AccuracyStruct accuracyData;
	struct SystemInfoStruct systemData;
	struct CalibrationStruct calibrationData;
	struct SpeedModeStruct speedData;

	int ret_calibrate = 0;
	int ret_validate = 0;
	int ret_connect = 0;

	char localhost[] = "127.0.0.1";

	ret_connect = iV_Connect(localhost, 4444, localhost, 5555);

	switch (ret_connect) {
	case RET_SUCCESS:
		printf("Successfully connected to SMI Server\n");
		break;
	case ERR_COULD_NOT_CONNECT:
		printf("Error: Could not connect to SMI Server\n");
		break;
	default:
		printf("Error: iV_Connect() returned: %d\n", ret_connect);
		exit(1);
	}

	iV_GetSpeedModes(&speedData);

	iV_ShowEyeImageMonitor();
	iV_ShowTrackingMonitor();
	getchar();

	// Eyetracker calibration
	calibrationData.method = 2;
	calibrationData.speed = 1;
	calibrationData.displayDevice = 0;
	calibrationData.targetShape = 2;
	calibrationData.foregroundBrightness = 250;
	calibrationData.backgroundBrightness = 230;
	calibrationData.autoAccept = 2;
	calibrationData.targetSize = 20;
	calibrationData.visualization = 1;
	strcpy_s(calibrationData.targetFilename, 256, "");

	iV_SetupCalibration(&calibrationData);
	// start calibration
	ret_calibrate = iV_Calibrate();

	gaze = malloc(sizeof(gaze_struct));
	if (!gaze)
		pexit("malloc failed");

	iV_SetSampleCallback(update_gaze);
}
#endif

int main(int argc, char **argv)
{
	char **video_files;
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

#ifdef __MINGW32__
	setup_ivx();
#endif

	video_files = parse_file_lines(argv[1]);
	w_ctx = window_init(screen_width, screen_height);

	for (int i = 0; video_files[i]; i++) {

		r_ctx = reader_init(video_files[i], queue_capacity);
		source_d_ctx = source_decoder_init(r_ctx, queue_capacity);
		e_ctx = encoder_init(source_d_ctx, 1, w_ctx);
		fov_d_ctx = fov_decoder_init(e_ctx->packet_queue);

		reader = SDL_CreateThread(reader_thread, "reader_thread", r_ctx);
		source_decoder = SDL_CreateThread(decoder_thread, "source_decoder_thread", source_d_ctx);
		encoder = SDL_CreateThread(encoder_thread, "encoder_thread", e_ctx);
		fov_decoder = SDL_CreateThread(decoder_thread, "fov_decoder_thread", fov_d_ctx);

		window_set_queues(w_ctx, fov_d_ctx->frame_queue, e_ctx->lag_queue);
		set_timing(w_ctx, source_d_ctx);
		event_loop(w_ctx);

		SDL_WaitThread(reader, NULL);
		SDL_WaitThread(source_decoder, NULL);
		SDL_WaitThread(encoder, NULL);
		SDL_WaitThread(fov_decoder, NULL);

		// FIXME: fix free functions
		//context_free(&r_ctx, &d_ctx);
		break;
	}

	return EXIT_SUCCESS;
}
