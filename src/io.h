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

#pragma once

#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>

/**
 * Container for a generic queue and associated metadata.
 *
 * Signalling is implemented by having data[rear] always point to an
 * unused location, therefore capacity+1 elements have to be allocated.
 */
typedef struct Queue {
	void **data;
	size_t capacity;
	unsigned int front;
	unsigned int rear;
	SDL_mutex *mutex;
	SDL_cond *full;
	SDL_cond *empty;
} Queue;

// Passed to reader_thread through SDL_CreateThread
typedef struct rdr_ctx {
	char *filename;
	int stream_index;
	Queue *packets;
	AVFormatContext *fctx;
} rdr_ctx;

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
 * Print formatted error message referencing the affeted source file,
 * line and the errno status through perror (3), then exit with EXIT_FAILURE.
 * Likely used through the pexit macro for comfort.
 *
 * @param msg error message
 * @param file usually the __FILE__ macro
 * @param line usually the __LINE__ macro
 */
void pexit_(const char *msg, const char *file, const int line);

/**
 * Convenience macro to report runtime errors with debug information.
 */
#define pexit(s) pexit_(s, __FILE__, __LINE__)

/**
 * Create and initialize a queue structure.
 *
 * Allocates storage on the heap.
 * Use free_queue to dispose of pointers acquired through this function.
 * @param capacity number of elements the queue is able to store.
 * @return initialized queue. See enqueue, dequeue, free_queue.
 */
Queue *queue_init(size_t capacity);

/**
 * Free a Queue.
 *
 * The mutex and the full/empty condition variables are destroyed.
 * Calls free on both q->data and subsequently q itself, which is set to NULL.
 * This function does not take care of any remaining elements in the queue!
 * These have to be handled manually. Caution: This can lead to data leaks.
 */
void queue_free(Queue **q);

/**
 * Add data to end of the queue.
 *
 * Blocks if there is no space left, waiting for a signal on the full condition variable.
 * @param q Queue acquired through queue_init.
 * @param data will be appended to q->data.
 */
void queue_append(Queue *q, void *data);

/**
 * Extract the first element of a queue.
 *
 * Blocks if there is no element in q, waiting for a signal on the empty condition variable.
 * Pointers are not safely removed from the queue (not overwritten) and might still be
 * accessible at a later point in time.
 * @param q pointer to a valid Queue acquired through queue_init.
 * @return void* the first element of q.
 */
void *queue_extract(Queue *q);

/**
 * Parse a file line by line.
 *
 * At most PATH_MAX characters per line are supported, the purpose is to parse
 * a file containing pathnames. Each line is sanitized: A trailing newline
 * character is replaced with a nullbyte. The returned pointer array is also NULL
 * terminated. The resulting array be freed by using free_lines.
 * @param pathname of an ascii file to be opened and parsed
 * @return NULL-terminated array of char* to line contents
 */
char **parse_lines(const char *pathname);

/**
 * Free an array of char pointers allocated by parse_lines.
 *
 * Free each char* in the array, finally free the array itself and set it to NULL.
 * @param lines char* array to be freed.
 */
void free_lines(char ***lines);


/**
 * Read a video file and put the contained AVPackets in a queue.
 *
 * Call av_read_frame repeatedly. Filter the returned packets by their stream
 * index, discarding everything but video packets (e.g. audio or subtitles).
 * Enqueue video packets in reader_ctx->packets.
 * Upon EOF, enqueue a NULL pointer.
 *
 * This function is to be used through SDL_CreateThread.
 * The resulting thread will block if the packets queue runs full.
 * @param void *ptr will be cast to (rdr_ctx *)
 * @return int
 */
int reader_thread(void *ptr);

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
 * Create and initialize a reader context.
 *
 * Open and demultiplex the file given in reader_ctx->filename.
 * Identify the "best" video stream index, usually there will only be one.
 * Must be freed through reader_free.
 *
 * Calls pexit in case of a failure.
 * @param filename the file the reader thread will try to open
 * @return reader_context* to a heap-allocated instance.
 */
rdr_ctx *reader_init(char *filename, int queue_capacity);

/**
 * Free the reader_context and all allocated resources,
 * set r-ctx to NULL.
 * @param r_ctx reader context to be freed.
 */
void reader_free(rdr_ctx **rc);

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
void realloc_texture(win_ctx *wc, AVFrame *frame);

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
void center_rect(SDL_Rect *rect, win_ctx *w_ctx, AVFrame *f);

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
