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
#include "helpers.h"

// Passed to reader_thread through SDL_CreateThread
typedef struct reader_context {
	char *filename;
	int stream_index;
	Queue *packet_queue;
	AVFormatContext *format_ctx;
} reader_context;


// Passed to decoder_thread through SDL_CreateThread
typedef struct decoder_context {
	int stream_index;
	Queue *packet_queue;
	Queue *frame_queue;
	AVFormatContext *format_ctx;
} decoder_context;


/**
 * Read a video file and put the contained AVPackets in a queue.
 *
 * Call av_read_frame repeatedly. Filter the returned packets by their stream
 * index, discarding everything but video packets, such as audio or subtitles.
 * Enqueue video packets in reader_ctx->packet_queue.
 * Upon EOF, enqueue a NULL pointer.
 *
 * This function is to be used through SDL_CreateThread.
 * The resulting thread will block if reader_ctx->queue is full.
 * Calls pexit in case of a failure.
 * @param void *ptr will be cast to (file_reader_context *)
 * @return int
 */
int reader_thread(void *ptr)
{
	reader_context *r_ctx = (reader_context *) ptr;
	int ret;

	AVPacket *pkt;

	while (1) {
		pkt = malloc(sizeof(AVPacket));
		if (!pkt)
			pexit("malloc failed");

		ret = av_read_frame(r_ctx->format_ctx, pkt);
		if (ret == AVERROR_EOF)
			break;
		else if (ret < 0)
			pexit("av_read_frame failed");

		/* discard invalid buffers and non-video packages */
		if (pkt->buf == NULL || pkt->stream_index != r_ctx->stream_index) {
			av_packet_free(&pkt);
			continue;
		}
		enqueue(r_ctx->packet_queue, pkt);
	}
	/* finally enqueue NULL to enter draining mode */
	enqueue(r_ctx->packet_queue, NULL);
	avformat_close_input(&r_ctx->format_ctx); //FIXME: Is it reasonable to call this here already?
	return 0;
}

void display_usage(char *progname)
{
	printf("usage:\n$ %s infile\n", progname);
}


/**
 * Create and initialize a reader context.
 *
 * Open and demultiplex the file given in reader_ctx->filename.
 * Identify the "best" video stream index, usually there will only be one.
 *
 * Calls pexit in case of a failure.
 * @param filename the file the reader thread will try to open
 * @return reader_context* to a heap-allocated instance.
 */
reader_context *reader_init(char *filename, int queue_capacity)
{
	reader_context *r;
	int ret;
	int stream_index;
	AVFormatContext *format_ctx;
	Queue *packet_queue;

	// preparations: allocate, open and set required datastructures
	format_ctx = avformat_alloc_context();
	if (!format_ctx)
		pexit("avformat_alloc_context failed");

	ret = avformat_open_input(&format_ctx, filename, NULL, NULL);
	if (ret < 0)
		pexit("avformat_open_input failed");

	stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (stream_index == AVERROR_STREAM_NOT_FOUND || stream_index == AVERROR_DECODER_NOT_FOUND)
		pexit("video stream or decoder not found");

	format_ctx->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	packet_queue = create_queue(queue_capacity);

	// allocate and set the context
	r = malloc(sizeof(reader_context));
	if (!r)
		pexit("malloc failed");

	r->format_ctx = format_ctx;
	r->stream_index = stream_index;
	r->filename = filename;
	r->packet_queue = packet_queue;

	return r;
}




int main(int argc, char **argv)
{
	char **video_files;
	reader_context *r_ctx;
	SDL_Thread *reader;
	int queue_capacity = 32;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	video_files = parse_file_lines(argv[1]);

	for (int i = 0; video_files[i]; i++) {

		r_ctx = reader_init(video_files[0], queue_capacity);
		reader = SDL_CreateThread(reader_thread, "reader_thread", &r_ctx);


		SDL_WaitThread(reader, NULL);
		break; //DEMO
	}

	if (SDL_Init(SDL_INIT_VIDEO))
		pexit("SDL_Init failed");

	return EXIT_SUCCESS;
}
