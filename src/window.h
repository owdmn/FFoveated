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

#pragma once
#include "queue.h"
#include <SDL2/SDL.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>

// Passed to window_thread through SDL_CreateThread
typedef struct win_ctx {
	Queue *frames;
	Queue *timestamps;
	SDL_Window *window;
	SDL_Texture *texture;
	int64_t time_start;
	AVRational time_base;
} win_ctx;

/**
 * Create and initialize a window_context.
 *
 * Initialize SDL, create a window, create a renderer for the window.
 * The texture member is initialized to NULL and has to be handled with respect
 * to an AVFrame through the realloc_texture function!
 *
 * Calls pexit in case of a failure.
 * @return window_context with initialized defaults
 */
win_ctx *window_init();

/**
 * Display the next frame in the queue to the window.
 *
 * Dequeue the next frame from w_ctx->frame_queue, render it to w_ctx->window
 * in a centered rectangle, adding black bars for undefined regions.
 *
 * @param w_ctx supplying the window and frame_queue
 * @return 0 on success, 1 if the frame_queue is drained (returned NULL).
 */
int frame_refresh(win_ctx *w_ctx);

/**
 * Update a window in order to display a new input video.
 *
 * Set frame and timestamp queues, set the time_base to match the new input videos
 * time_base and set the start_time to -1.
 * @param wc window context to update
 * @param frames new input queue for frames to be displayed
 * @param timestamps new encoder timestamp queue
 * @param time_base new time base to display frames at correct pts
 */
void set_window_source(win_ctx *wc, Queue *frames, Queue *timestamps, AVRational time_base);

/**
 * Empty and free all associated window input queues.
 *
 * @param wc window context to flush
 */
void flush_window_source(win_ctx *wc);
