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




/**
 *
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


	while (1) {
		ret = av_read_frame(format_ctx, &pkt);
		if (ret < 0) {
			if (ret == AVERROR_EOF)
				break;
			else
				pexit("av_read_frame returned error");
		}

		/* discard non-video packages */
		if (pkt.stream_index != stream_index)
			continue;

		if (pkt.buf) {
			pkt_p = malloc(sizeof(AVPacket));
			av_copy_packet(pkt_p, &pkt);
			enqueue(reader_ctx->packet_queue, pkt_p);
		}
	}

	avformat_close_input(&format_ctx);
	return 0;
}

void display_usage(char *progname)
{
	printf("usage:\n$ %s infile\n", progname);
}

int main(int argc, char **argv)
{
	char **video_files = NULL;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	video_files = parse_file_lines(argv[1]);

	for (int i = 0; video_files[i]; i++)
		printf("video_files[i]: %s\n", video_files[i]);

	if (SDL_Init(SDL_INIT_VIDEO))
		pexit("SDL_Init failed");

	return EXIT_SUCCESS;
}
