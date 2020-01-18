/*
 * Copyright (C) 2020 Oliver Wiedemann
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

#include "window.h"
#include "pexit.h"

win_ctx *window_init()
{
	win_ctx *wc;
	SDL_Window *window;
	Uint32 flags;
	SDL_Renderer *renderer;
	SDL_DisplayMode dm;
	int disp_index;

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);


	flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP;
	window = SDL_CreateWindow("FFoveated",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		1, 1, flags);
	if (!window)
		pexit("SDL_CreateWindow failed");

	disp_index = SDL_GetWindowDisplayIndex(window);
	SDL_GetDesktopDisplayMode(disp_index, &dm);

	renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
		pexit(SDL_GetError());

	wc = malloc(sizeof(win_ctx));
	if (!wc)
		pexit("malloc failed");
	wc->window = window;
	wc->texture = NULL;
	return wc;
}

/**
 * (Re)allocate a the texture member of a window_context
 *
 * If no existing texture is present, create a suitably sized one.
 * If the existing texture and the new frame to be rendered agree in dimensions,
 * leave the texture unmodified and return. If they disagree, destroy the old
 * texture and create a suitable one instead.
 * Calls pexit in case of a failure
 * @param w_ctx window context whose texture member is being updated.
 * @param frame frame to be rendered to the texture.
 */
static void realloc_texture(win_ctx *wc, AVFrame *frame)
{
	int ret;
	int old_width;
	int old_height;
	int old_access;
	Uint32 old_format;

	if (wc->texture) {
		/* texture already exists - check if we need to modify it */
		ret = SDL_QueryTexture(wc->texture, &old_format, &old_access,
											   &old_width, &old_height);
		if (ret < 0)
			pexit("SDL_QueryTexture failed");

		/* if the specs agree, don't change it, otherwise detroy it */
		if (frame->width == old_width && frame->height == old_height)
			return;
		SDL_DestroyTexture(wc->texture);
	}

	wc->texture = SDL_CreateTexture(SDL_GetRenderer(wc->window),
										   SDL_PIXELFORMAT_YV12,
										   SDL_TEXTUREACCESS_TARGET,
										   frame->width, frame->height);
	if (!wc->texture)
		pexit("SDL_CreateTexture failed");
}

/**
 * Calculate a centered rectangle within a window with a suitable aspect ratio.
 *
 * In order to display a frame in a window with unsuitable aspect ratio,
 * we render the frame to a centered rectangle with correct aspect ratio.
 * Empty areas will be filled with black bars.
 * @param rect will be modified to fit the desired aspect ratio in the window.
 * @param w_ctx window_context, read width and height of the window
 * @param f AVFrame to be displayed
 */
static void center_rect(SDL_Rect *rect, win_ctx *wc, AVFrame *f)
{
	int win_w, win_h;

	SDL_GetWindowSize(wc->window, &win_w, &win_h);
	AVRational ratio = av_make_q(f->width, f->height); //aspect ratio

	// check if the frame fully fits into the window
	if (win_h >= f->height && win_w >= f->width) {
		rect->x = (win_w - f->width) / 2;
		rect->y  = (win_h - f->height) / 2;
		rect->w = f->width;
		rect->h = f->height;
	} else { //frame does not fit completely, do a fit
		// fix height to window, adapt width according to frame
		rect->h = win_h;
		rect->w = av_rescale(rect->h, ratio.num, ratio.den);
		// if that does not fit, fix width to window, adapt height
		if (rect->w > win_w) {
			rect->w = win_w;
			rect->h = av_rescale(win_w, ratio.den, ratio.num);
		}
		// margins for black bars if aspect ratio does not fit
		rect->x = (win_w - rect->w) / 2;
		rect->y = (win_h - rect->h) / 2;
	}
}

void flush_window_source(win_ctx *wc)
{
	void *p;
	AVFrame *f;

	while ((p = queue_extract(wc->timestamps)))
		free(p);
	queue_free(&wc->timestamps);
	while ((f = queue_extract(wc->frames)))
		av_frame_free(&f);
	queue_free(&wc->frames);
}

int frame_refresh(win_ctx *wc)
{
	AVFrame *f;
	SDL_Renderer *ren;
	SDL_Rect rect;

	int64_t upts; // presentation time in micro seconds
	int64_t uremaining; //remaining time in micro seconds

	int64_t *enc_time;//encoding time
	int64_t now;
	#ifdef DEBUG
	int64_t delta;
	#endif

	f = queue_extract(wc->frames);
	if (!f) {
		printf("frame refresh returns 1\n");
		return 1;
	}
	enc_time = queue_extract(wc->timestamps);


	ren = SDL_GetRenderer(wc->window);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	realloc_texture(wc, f);
	SDL_UpdateYUVTexture(wc->texture, NULL,
						f->data[0], f->linesize[0],
						f->data[1], f->linesize[1],
						f->data[2], f->linesize[2]);
	center_rect(&rect, wc, f);
	SDL_RenderCopy(ren, wc->texture, NULL, &rect);
	now = av_gettime_relative();

	//add an initial delay bc we won't be able to display at 0
	if (wc->time_start == -1)
		wc->time_start = now + 100000;

	//XXX: why is the factor 2 here necessary? Can't find this in the docs, but it works consistently...
	upts = (2 * 1000000 * f->pts * wc->time_base.num) / wc->time_base.den; //pts relative to zero
	uremaining = wc->time_start + upts - now;

	#ifdef DEBUG
	delta = now - *enc_time;
	printf("rem: %ld, upts: %ld, now: %ld, delta: %ld\n", uremaining, upts, now, delta);
	#endif

	if (uremaining > 0)
		av_usleep(uremaining);
	else
		pexit("presentation lag");

	SDL_RenderPresent(ren);
	av_frame_free(&f);
	free(enc_time);
	return 0;
}

void set_window_source(win_ctx *wc, Queue *frames, Queue *timestamps, AVRational time_base)
{
	wc->frames = frames;
	wc->timestamps = timestamps;
	wc->time_base = time_base;
	wc->time_start = -1;
}
