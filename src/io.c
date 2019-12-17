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

Queue *create_queue(size_t capacity)
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

void free_queue(Queue *q)
{
	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCond(q->full);
	SDL_DestroyCond(q->empty);
	free(q->data);
	free(q);
}

void enqueue(Queue *q, void *data)
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

void *dequeue(Queue *q)
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
	for(c=*lines; *c; c++)
		free(*c);
	free(*lines);
	*lines = NULL;
}

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

reader_context *reader_init(char *filename, int queue_capacity)
{
	reader_context *r;
	int ret;
	int stream_index;
	AVFormatContext *format_ctx;
	Queue *packet_queue;
	char *fn_cpy;

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

	fn_cpy = malloc(strlen(filename));
	if (!fn_cpy)
		pexit("malloc failed");
	strncpy(fn_cpy, filename, strlen(filename));

	r->format_ctx = format_ctx;
	r->stream_index = stream_index;
	r->filename = fn_cpy;
	r->packet_queue = packet_queue;

	return r;
}

window_context *window_init(float screen_width, float screen_height)
{
	window_context *w_ctx;
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

	w_ctx = malloc(sizeof(window_context));
	if (!w_ctx)
		pexit("malloc failed");
	w_ctx->window = window;
	w_ctx->width = dm.w;
	w_ctx->height = dm.h;
	w_ctx->texture = NULL;
	w_ctx->screen_width = screen_width;
	w_ctx->screen_height = screen_height;
	return w_ctx;
}

void realloc_texture(window_context *w_ctx, AVFrame *frame)
{
	int ret;
	int old_width;
	int old_height;
	int old_access;
	Uint32 old_format;

	if (w_ctx->texture) {
		/* texture already exists - check if we need to modify it */
		ret = SDL_QueryTexture(w_ctx->texture, &old_format, &old_access,
											   &old_width, &old_height);
		if (ret < 0)
			pexit("SDL_QueryTexture failed");

		/* if the specs agree, don't change it, otherwise detroy it */
		if (frame->width == old_width && frame->height == old_height)
			return;
		SDL_DestroyTexture(w_ctx->texture);
	}

	w_ctx->texture = SDL_CreateTexture(SDL_GetRenderer(w_ctx->window),
										   SDL_PIXELFORMAT_YV12,
										   SDL_TEXTUREACCESS_TARGET,
										   frame->width, frame->height);
	if (!w_ctx->texture)
		pexit("SDL_CreateTexture failed");
}

void center_rect(SDL_Rect *rect, window_context *w_ctx, AVFrame *f)
{
	int width, height, x, y;
	AVRational aspect_ratio = av_make_q(f->width, f->height);

	// check if the frame fully fits into the window
	if (w_ctx->height >= f->height && w_ctx->width >= f->width) {
		x = (w_ctx->width - f->width) / 2;
		y = (w_ctx->height - f->height) / 2;
		width = f->width;
		height = f->height;
	} else { //frame does not fit completely, do a fit
		// fix height to window, adapt width according to frame
		height = w_ctx->height;
		width = av_rescale(height, aspect_ratio.num, aspect_ratio.den);
		// if that does not fit, fix width to window, adapt height
		if (width > w_ctx->width) {
			width = w_ctx->width;
			height = av_rescale(width, aspect_ratio.den, aspect_ratio.num);
		}
		// margins for black bars if aspect ratio does not fit
		x = (w_ctx->width - width) / 2;
		y = (w_ctx->height - height) / 2;
	}

	rect->x = x; //left
	rect->y = y; //top
	rect->w = width;
	rect->h = height;
}

int frame_refresh(window_context *w_ctx)
{
	AVFrame *frame;
	SDL_Renderer *r = SDL_GetRenderer(w_ctx->window);
	SDL_Rect rect;
	int64_t upts; // presentation time in micro seconds
	int64_t uremaining; //remaining time in micro seconds
	int64_t *encoder_timestamp;
	#ifdef debug
	double delay;
	#endif

	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);

	frame = dequeue(w_ctx->frame_queue);
	if (!frame)
		return 1;

	realloc_texture(w_ctx, frame);
	SDL_UpdateYUVTexture(w_ctx->texture, NULL, frame->data[0], frame->linesize[0],
									frame->data[1], frame->linesize[1],
									frame->data[2], frame->linesize[2]);

	center_rect(&rect, w_ctx, frame);
	SDL_RenderCopy(r, w_ctx->texture, NULL, &rect);

	//add an initial delay to avoid lags when upts == 0
	if (w_ctx->time_start == -1)
		w_ctx->time_start = av_gettime_relative() + 1000;

	//XXX: why is the factor 2 here necessary? Can't find this in the docs, but it works consistently...
	upts = (2 * 1000000 * frame->pts * w_ctx->time_base.num) / w_ctx->time_base.den;
	uremaining = w_ctx->time_start + upts - av_gettime_relative();

	encoder_timestamp = dequeue(w_ctx->lag_queue);
	free(encoder_timestamp);
	#ifdef debug
	delay = (av_gettime_relative() - *encoder_timestamp) / 1000000;
	fprintf(stdout, "remaining: %ld upts: %ld, frame->pts %ld, num %d, den %d, time %ld, delay %lf\n",
	uremaining, upts, frame->pts, w_ctx->time_base.num, w_ctx->time_base.den, av_gettime_relative(), delay);
	#endif

	if (uremaining > 0)
		av_usleep(uremaining);
	else
		pexit("presentation lag");

	SDL_RenderPresent(r);

	return 0;
}

void set_window_queues(window_context *w_ctx, Queue *frames, Queue *lags)
{
	w_ctx->frame_queue = frames;
	w_ctx->lag_queue = lags;
}

void set_window_timing(window_context *w_ctx, AVRational time_base)
{
	w_ctx->time_base = time_base;
	w_ctx->time_start = -1;
}
