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

void pexit_(const char *msg, const char *file, const int line)
{
	char buf[1024];

	snprintf(buf, sizeof(buf), "%s:%d: %s", file, line, msg);
	perror(buf);
	exit(EXIT_FAILURE);
}

Queue *queue_init(size_t capacity)
{
	Queue *q;

	q = (Queue *) malloc(sizeof(Queue));
	if (!q)
		pexit("malloc failed");

	// allocate one additional element for signalling
	q->data = malloc((capacity+1) * sizeof(void *));
	if (!q->data)
		pexit("malloc failed");

	q->front = 0;
	q->rear  = 0;
	q->capacity = capacity;

	q->mutex = SDL_CreateMutex();
	q->full  = SDL_CreateCond();
	q->empty = SDL_CreateCond();
	if (!(q->mutex && q->full && q->empty))
		pexit("SDL_CreateMutex or SDL_CreateCond failed");
	return q;
}

void queue_free(Queue **q)
{
	Queue *qd = *q;

	SDL_DestroyMutex(qd->mutex);
	SDL_DestroyCond(qd->full);
	SDL_DestroyCond(qd->empty);
	free(qd->data);
	free(*q);
}

void queue_append(Queue *q, void *data)
{
	unsigned int new_rear;

	if (SDL_LockMutex(q->mutex))
		pexit(SDL_GetError());

	new_rear = (q->rear + 1) % (q->capacity + 1);
	//check if full
	if (new_rear == q->front) {
		if (SDL_CondWait(q->full, q->mutex))
			pexit(SDL_GetError());
	}
	q->data[q->rear] = data;
	q->rear = new_rear;
	/* at least one item is now queued*/
	if (SDL_CondSignal(q->empty))
		pexit(SDL_GetError());

	if (SDL_UnlockMutex(q->mutex))
		pexit(SDL_GetError());

}

void *queue_extract(Queue *q)
{
	void *data;

	if (SDL_LockMutex(q->mutex))
		pexit(SDL_GetError());

	//check if empty
	if (q->front == q->rear) {
		if (SDL_CondWait(q->empty, q->mutex))
			pexit(SDL_GetError());
	}

	data = q->data[q->front];
	q->front = (q->front + 1) % (q->capacity + 1);

	if (SDL_CondSignal(q->full))
		pexit(SDL_GetError());
	if (SDL_UnlockMutex(q->mutex))
		pexit(SDL_GetError());

	return data;
}

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
		if(rc->abort) {
			printf("exiting reader loop");
			break;
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

	return rc;
}

void reader_free(rdr_ctx **rc)
{
	rdr_ctx *r;

	r = *rc;
	free(r->filename);
	queue_free(&r->packets);
	avformat_free_context(r->fctx);
	free(*rc);
	*rc = NULL;
}

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

void realloc_texture(win_ctx *wc, AVFrame *frame)
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

void center_rect(SDL_Rect *rect, win_ctx *wc, AVFrame *f)
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
	printf("rem: %ld, upts: %ld, now: %ld, delta: %ld\n", uremaining , upts, now, delta);
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
