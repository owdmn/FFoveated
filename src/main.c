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

/**
 * Container passed to file_reader through SDL_CreateThread
 */
typedef struct file_reader_context {
	char *filename;
	Queue *packet_queue;
} file_reader_context;


/**
 * Read a video file and put the contained AVPackets in a queue.
 *
 * Open and demultiplex the file given in reader_ctx->filename.
 * Identify the "best" video stream index, usually there will only be one.
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
int file_reader(void *ptr)
{
	file_reader_context *reader_ctx = (file_reader_context *) ptr;
	int ret, stream_index;	/* index of the desired stream to select packages*/
	AVFormatContext *format_ctx;
	AVPacket pkt, *pkt_p;

	format_ctx = avformat_alloc_context();
	if (!format_ctx)
		pexit("avformat_alloc_context failed");

	ret = avformat_open_input(&format_ctx, reader_ctx->filename, NULL, NULL);
	if (ret < 0)
		pexit("avformat_open_input failed");

	stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (stream_index == AVERROR_STREAM_NOT_FOUND || stream_index == AVERROR_DECODER_NOT_FOUND)
		pexit("video stream or decoder not found");

	format_ctx->streams[stream_index]->discard = AVDISCARD_DEFAULT;

	while (1) {
		ret = av_read_frame(format_ctx, &pkt);
		if (ret == AVERROR_EOF)
			break;
		else if (ret < 0)
			pexit("av_read_frame failed");

		/* discard non-video packages */
		if (pkt.stream_index != stream_index)
			continue;

		if (pkt.buf) {
			pkt_p = malloc(sizeof(AVPacket));
			av_copy_packet(pkt_p, &pkt); //FIXME: deprecated in FFmpeg
			enqueue(reader_ctx->packet_queue, pkt_p);
		}
	}
	/* finally enqueue NULL to enter draining mode */
	enqueue(reader_ctx->packet_queue, NULL);
	avformat_close_input(&format_ctx);
	return 0;
}

void display_usage(char *progname)
{
	printf("usage:\n$ %s infile\n", progname);
}

int main(int argc, char **argv)
{
	char **video_files;
	file_reader_context reader_ctx;
	SDL_Thread *reader_thread;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	video_files = parse_file_lines(argv[1]);

	for (int i = 0; video_files[i]; i++) {

		reader_ctx.filename = video_files[0];
		reader_ctx.packet_queue = create_queue(32);
		reader_thread = SDL_CreateThread(file_reader, "file_reader_thread", &reader_ctx);
		SDL_WaitThread(reader_thread, NULL);
		break; //DEMO
	}

	if (SDL_Init(SDL_INIT_VIDEO))
		pexit("SDL_Init failed");

	return EXIT_SUCCESS;
}
