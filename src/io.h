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

#include "queue.h"
#include <libavformat/avformat.h>

// Passed to reader_thread through SDL_CreateThread
typedef struct rdr_ctx {
	char *filename;
	int stream_index;
	Queue *packets;
	AVFormatContext *fctx;
	int abort;
} rdr_ctx;

// Passed to writer_thread through SDL_CreateThread
typedef struct wtr_ctx {
	Queue *packets;
	AVFormatContext *fctx;
} wtr_ctx;

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
 * Free the reader_context and all private resources.
 * The output queue is not freed! The receiver has to take
 * care of this.
 * set r-ctx to NULL.
 * @param r_ctx reader context to be freed.
 */
void reader_free(rdr_ctx **rc);

/**
 * Create and initialize a writer context
 */
wtr_ctx *writer_init(char *filename, Queue *packets, rdr_ctx *rc, AVCodecContext *enc_ctx);

/**
 * Accept packets from a queue and write them to multiplexed container
 * on disk.
 */
int writer_thread(void *ptr);