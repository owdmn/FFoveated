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
#include "pexit.h"
#include <libavutil/time.h>
#include <limits.h> /* PATH_MAX */

char **parse_lines(const char *pathname)
{
	FILE *fp;
	char line_buf[PATH_MAX];
	char *newline;
	char **lines;
	int used;
	int size;

	fp = fopen(pathname, "r");
	if (!fp)
		pexit("fopen failed");

	size = 32; //initial allocation
	used =	0;
	lines = malloc(size * sizeof(char *));
	if (!lines)
		pexit("malloc failed");

	/* separate and copy filenames into null-terminated strings */
	while (fgets(line_buf, PATH_MAX, fp)) {
		newline = strchr(line_buf, '\n');
		if (newline)
			*newline = '\0';	//remove trailing newline

		lines[used] = strdup(line_buf);
		used++;
		if (used == size) {
			size = size*2;
			lines = realloc(lines, size * sizeof(char *));
			if (!lines)
				pexit("realloc failed");
		}
	}
	lines[used] = NULL;		//termination symbol

	return lines;
}

void free_lines(char ***lines)
{
	char **c;

	for (c = *lines; *c; c++)
		free(*c);
	free(*lines);
	*lines = NULL;
}

int reader_thread(void *ptr)
{
	rdr_ctx *rc = (rdr_ctx *) ptr;
	int ret;

	AVPacket *pkt;

	while (1) {
		if (rc->abort) {
			avformat_close_input(&rc->fctx);
			queue_append(rc->packets, NULL);
			return 0;
		}

		pkt = malloc(sizeof(AVPacket));
		if (!pkt)
			pexit("malloc failed");

		ret = av_read_frame(rc->fctx, pkt);
		if (ret == AVERROR_EOF)
			break;
		else if (ret < 0)
			pexit("av_read_frame failed");

		/* discard invalid buffers and non-video packages */
		if (pkt->buf == NULL || pkt->stream_index != rc->stream_index) {
			av_packet_free(&pkt);
			continue;
		}
		queue_append(rc->packets, pkt);
	}
	/* finally enqueue NULL to enter draining mode */
	queue_append(rc->packets, NULL);
	avformat_close_input(&rc->fctx);
	reader_free(&rc);
	return 0;
}

rdr_ctx *reader_init(char *filename, int queue_capacity)
{
	rdr_ctx *rc;
	int ret;
	int stream_index;
	AVFormatContext *fctx;
	Queue *packets;
	char *fn_cpy;

	// preparations: allocate, open and set required datastructures
	fctx = avformat_alloc_context();
	if (!fctx)
		pexit("avformat_alloc_context failed");

	ret = avformat_open_input(&fctx, filename, NULL, NULL);
	if (ret < 0)
		pexit("avformat_open_input failed");

	stream_index = av_find_best_stream(fctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (stream_index == AVERROR_STREAM_NOT_FOUND || stream_index == AVERROR_DECODER_NOT_FOUND)
		pexit("video stream or decoder not found");

	fctx->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	packets = queue_init(queue_capacity);

	// allocate and set the context
	rc = malloc(sizeof(rdr_ctx));
	if (!rc)
		pexit("malloc failed");

	fn_cpy = malloc(strlen(filename));
	if (!fn_cpy)
		pexit("malloc failed");
	strncpy(fn_cpy, filename, strlen(filename));

	rc->fctx = fctx;
	rc->stream_index = stream_index;
	rc->filename = fn_cpy;
	rc->packets = packets;
	rc->abort = 0;

	return rc;
}

void reader_free(rdr_ctx **rc)
{
	rdr_ctx *r;

	r = *rc;
	free(r->filename);
	avformat_free_context(r->fctx);
	free(*rc);
	*rc = NULL;
}


