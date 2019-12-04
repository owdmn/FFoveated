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
	Queue *packet_queue;
	Queue *frame_queue;
	AVCodecContext *avctx;
} decoder_context;


// Passed  to encoder_thread through SDL_CreateThread
typedef struct encoder_context {
	Queue *frame_queue;
	Queue *packet_queue;
	AVCodecContext *avctx;
	AVDictionary *options;
} encoder_context;


// Passed to window_thread through SDL_CreateThread
typedef struct window_context {
	Queue *frame_queue;
	SDL_Window *window;
	SDL_Texture *texture;
	int width;
	int height;
} window_context;


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


/**
 * Send a packet to the decoder, check the return value for errors.
 *
 * Calls pexit in case of a failure
 * @param avctx
 * @param packet
 */
void supply_packet(AVCodecContext *avctx, AVPacket *packet)
{
	int ret;

	ret = avcodec_send_packet(avctx, packet);
	if (ret == AVERROR(EAGAIN))
		pexit("API break: decoder send and receive returns EAGAIN");
	else if (ret == AVERROR_EOF)
		pexit("Decoder has already been flushed");
	else if (ret == AVERROR(EINVAL))
		pexit("codec invalid, not open or requires flushing");
	else if (ret == AVERROR(ENOMEM))
		pexit("memory allocation failed");
}


/**
 * Decode AVPackets and put the uncompressed AVFrames in a queue.
 *
 * Call avcodec_receive_frame in a loop, enqueue decoded frames.
 * Adds NULL packet to queue in the end.
 *
 * Calls pexit in case of a failure
 * @param *ptr will be cast to (decoder_context *)
 * @return int 0 on success.
 */
int decoder_thread(void *ptr)
{
	int ret;
	decoder_context *dec_ctx = (decoder_context *) ptr;
	AVCodecContext *avctx = dec_ctx->avctx;
	AVFrame *frame;
	AVPacket *packet;

	frame = av_frame_alloc();
	for (;;) {
		if (!frame)
			pexit("av_frame_alloc failed");

		ret = avcodec_receive_frame(avctx, frame);
		if (ret == 0) {
			// valid frame - enqueue and allocate new buffer
			enqueue(dec_ctx->frame_queue, frame);
			frame = av_frame_alloc();
			continue;
		} else if (ret == AVERROR(EAGAIN)) {
			//provide another packet to the decoder
			packet = dequeue(dec_ctx->packet_queue);
			supply_packet(avctx, packet);
			continue;
		} else if (ret == AVERROR_EOF) {
			break;
		} else if (ret == AVERROR(EINVAL)) {
			//fatal
			pexit("avcodec_receive_frame failed");
		}
	//note continue/break pattern before adding functionality here
	}

	//enqueue flush packet in
	enqueue(dec_ctx->frame_queue, NULL);
	avcodec_close(avctx);
	avcodec_free_context(&avctx);
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


/**
 * Free all members and the context pointers themselves, setting the pointers
 * to NULL. As reader and decoder contexts share pointers, this unified
 * function is used to prevent double-frees.
 *
 * @param reader_context to be freed.
 */
void context_free(reader_context **r_ctx, decoder_context **d_ctx)
{
	reader_context *r;
	decoder_context *d;

	r = *r_ctx;
	d = *d_ctx;
	avformat_free_context(r->format_ctx);
	free_queue(r->packet_queue);
	free(r->filename);

	// the only member left to free for d is the frame_queue!
	free_queue(d->frame_queue);

	free(d);
	free(r);
	*r_ctx = NULL;
	*d_ctx = NULL;
}


/**
 * Create and initialize a decoder context.
 *
 * A decoder context inherits the necessary fields from a reader context to fetch
 * AVPacktes from its packet_queue and adds a frame_queue to emit decoded AVFrames.
 *
 * Calls pexit in case of a failure.
 * @param r reader context to copy format_ctx, packet_queue and stream_index from.
 * @param queue_capacity output frame queue capacity.
 * @return decoder_context* with all members initialized.
 */
decoder_context *source_decoder_init(reader_context *r_ctx, int queue_capacity)
{
	AVCodecContext *avctx;
	decoder_context *d;
	int ret;
	int index = r_ctx->stream_index;
	AVStream *stream = r_ctx->format_ctx->streams[index];
	AVCodec *codec;

	avctx = avcodec_alloc_context3(NULL);
	if (!avctx)
		pexit("avcodec_alloc_context3 failed");

	ret = avcodec_parameters_to_context(avctx, stream->codecpar);
	if (ret < 0)
		pexit("avcodec_parameters_to_context failed");

	avctx->time_base = stream->time_base;

	codec = avcodec_find_decoder(avctx->codec_id);
	if (!codec)
		pexit("avcodec_find_decoder failed");

	avctx->codec_id = codec->id;

	ret = avcodec_open2(avctx, codec, NULL);
	if (ret < 0)
		pexit("avcodec_open2 failed");

	d = malloc(sizeof(decoder_context));
	if (!d)
		pexit("malloc failed");

	d->packet_queue = r_ctx->packet_queue;
	d->frame_queue = create_queue(queue_capacity);
	d->avctx = avctx;

	return d;
}


/**
 * Create and initialize a decoder context.
 *
 * The decoder context inherits the necessary fields from an encoder context.
 *
 * Calls pexit in case of a failure.
 * @param r reader context to copy format_ctx, packet_queue and stream_index from.
 * @param queue_capacity output frame queue capacity.
 * @return decoder_context* with all members initialized.
 */
decoder_context *fov_decoder_init(encoder_context *enc_ctx)
{
	AVCodecContext *avctx;
	AVCodec *codec;
	decoder_context *d;
	int ret;

	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec)
		pexit("avcodec_find_decoder_by_name failed");

	avctx = avcodec_alloc_context3(codec);
	if (!avctx)
		pexit("avcodec_alloc_context3 failed");

	ret = avcodec_open2(avctx, codec, NULL);
	if (ret < 0)
		pexit("avcodec_open2 failed");

	d = malloc(sizeof(decoder_context));
	if (!d)
		pexit("malloc failed");

	d->packet_queue = enc_ctx->packet_queue;
	d->frame_queue = create_queue(1);
	d->avctx = avctx;

	return d;
}


/**
 * Create and initialize an encoder context
 *
 * Calls pexit in case of a failure
 * @param dec_ctx context of the previous decoder
 * @param queue_capacity output packet queue capacity
 * @return encoder_context with initialized fields and opened decoder
 */
encoder_context *encoder_init(decoder_context *dec_ctx, int queue_capacity)
{
	int ret;
	encoder_context *enc_ctx;
	AVCodecContext *avctx;
	AVCodec *codec;
	AVDictionary *options = NULL;

	enc_ctx = malloc(sizeof(encoder_context));
	if (!enc_ctx)
		pexit("malloc failed");

	codec = avcodec_find_encoder_by_name("libx264");
	if (!codec)
		pexit("encoder not found");

	avctx = avcodec_alloc_context3(codec);
	if (!avctx)
		pexit("avcodec_alloc_context3 failed");

	avctx->time_base = dec_ctx->avctx->time_base;
	avctx->pix_fmt = codec->pix_fmts[0]; //first supported pixel format
	avctx->width = dec_ctx->avctx->width;
	avctx->height = dec_ctx->avctx->height;

	ret = 0;
	ret |= av_dict_set(&options, "preset", "ultrafast", 0);
	ret |= av_dict_set(&options, "tune", "zerolatency", 0);
	ret |= av_dict_set(&options, "aq-mode", "variance", 0);
	ret |= av_dict_set(&options, "gop-size", "3", 0);

	ret = avcodec_open2(avctx, avctx->codec, &options);
	if (ret < 0)
		pexit("avcodec_open2 failed");

	enc_ctx->frame_queue = dec_ctx->frame_queue;
	enc_ctx->packet_queue = create_queue(queue_capacity);
	enc_ctx->avctx = avctx;
	enc_ctx->options = options;

	return enc_ctx;
}


/**
 * Supply the given codec with a frame, handle errors appropriately.
 *
 * Calls pexit in dase of a failure.
 * @param avctx context of the codec being supplied
 * @param frame the frame to be supplied
 */

void supply_frame(AVCodecContext *avctx, AVFrame *frame)
{
	int ret;

	ret = avcodec_send_frame(avctx, frame);
	if (ret == AVERROR(EAGAIN))
		pexit("API break: encoder send and receive returns EAGAIN");
	else if (ret == AVERROR_EOF)
		pexit("Encoder has already been flushed");
	else if (ret == AVERROR(EINVAL))
		pexit("codec invalid, not open or requires flushing");
	else if (ret == AVERROR(ENOMEM))
		pexit("memory allocation failed");
}


/**
 * Encode AVFrames and put the compressed AVPacktes in a queue
 *
 * Call avcodec_receive_packet in a loop, enqueue encoded packets
 * Adds NULL packet to queue in the end.
 *
 * Calls pexit in case of a failure
 * @param ptr will be casted to (encoder_context *)
 * @return int 0 on success
 */
int encoder_thread(void *ptr)
{
	encoder_context *enc_ctx = (encoder_context *) ptr;
	AVFrame *frame;
	AVPacket *packet;
	int ret;

	packet = av_packet_alloc(); //NULL check in loop.

	for (;;) {
		if (!packet)
			pexit("av_packet_alloc failed");

		ret = avcodec_receive_packet(enc_ctx->avctx, packet);
		if (ret == 0) {
			enqueue(enc_ctx->packet_queue, packet);
			packet = av_packet_alloc();
			continue;
		} else if (ret == AVERROR(EAGAIN)) {
			frame = dequeue(enc_ctx->frame_queue);
			supply_frame(enc_ctx->avctx, frame);
		} else if (ret == AVERROR_EOF) {
			break;
		} else if (ret == AVERROR(EINVAL)) {
			pexit("avcodec_receive_packet failed");
		}
	}

	enqueue(enc_ctx->packet_queue, NULL);
	avcodec_close(enc_ctx->avctx);
	avcodec_free_context(&enc_ctx->avctx);
	return 0;
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


/**
 * Create and initialize a window_context.
 *
 * Initialize SDL, create a window, create a renderer for the window.
 * The texture member is initialized to NULL and has to be handled with respect
 * to an AVFrame through the realloc_texture function!
 *
 * Calls pexit in case of a failure.
 * @param width initial window width.
 * @param height initial window height.
 * @param fullscreen add SDL_WINDOW_FULLSCREEN flag if evaluated to true.
 * @return window_context with initialized defaults
 */
window_context *window_init(int width, int height, int fullscreen)
{
	window_context *w_ctx;
	SDL_Window *window;
	Uint32 flags;
	SDL_Renderer *renderer;

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
	if (fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN;
	window = SDL_CreateWindow("FFoveated",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		width, height, flags);
	if (!window)
		pexit("SDL_CreateWindow failed");

	renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
		pexit(SDL_GetError());

	w_ctx = malloc(sizeof(window_context));
	if (!w_ctx)
		pexit("malloc failed");
	w_ctx->window = window;
	w_ctx->width = width;
	w_ctx->height = height;
	w_ctx->texture = NULL;
	return w_ctx;
}


/**
 * Set the frame queue for the given window context to q
 *
 * @param q frame queue
 * @param w_ctx to be updated
 */
void window_set_frame_queue(Queue *q, window_context *w_ctx)
{
	w_ctx->frame_queue = q;
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
void center_rect(SDL_Rect *rect, window_context *w_ctx, AVFrame *f)
{
	int width, height, x, y;
	AVRational aspect_ratio = av_make_q(f->width, f->height);

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

	rect->x = x; //left
	rect->y = y; //top
	rect->w = width;
	rect->h = height;
}


/**
 * Display the next frame in the queue to the window.
 *
 * Dequeue the next frame from w_ctx->frame_queue, render it to w_ctx->window
 * in a centered rectangle, adding black bars for undefined regions.
 *
 * @param w_ctx supplying the window and frame_queue
 * @return 0 on success, 1 if the frame_queue is drained (returned NULL).
 */
int frame_refresh(window_context *w_ctx)
{
	AVFrame *frame;
	SDL_Renderer *r = SDL_GetRenderer(w_ctx->window);
	SDL_Rect rect;

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
	SDL_RenderPresent(r);
	return 0;
}


/**
 * Loop: Render frames and react to events.
 *
 * Calls pexit in case of a failure.
 * @param w_ctx window_context to which rendering and event handling applies.
 */
void event_loop(window_context *w_ctx)
{
	SDL_Event event;
	int queue_drained = 0;

	for (;;) {
		/* check for events to handle, meanwhile just render frames */
		while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
			if (frame_refresh(w_ctx)) {
				queue_drained = 1;
				break;
			}
			SDL_PumpEvents();
		}
		if (queue_drained)
			break;

		switch (event.type) {
		case SDL_KEYDOWN:
			switch (event.key.keysym.sym) {
			case SDLK_q:
				pexit("q pressed");
			}
			break;
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_RESIZED:
				w_ctx->width = event.window.data1;
				w_ctx->height = event.window.data2;
				SDL_DestroyTexture(w_ctx->texture);
				w_ctx->texture = NULL;
			break;
			}
		}
	}
}


int main(int argc, char **argv)
{
	char **video_files;
	reader_context *r_ctx;
	decoder_context *source_d_ctx, *fov_d_ctx;
	encoder_context *e_ctx;
	window_context *w_ctx;
	SDL_Thread *reader, *source_decoder, *encoder, *fov_decoder;
	const int queue_capacity = 32;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	video_files = parse_file_lines(argv[1]);
	w_ctx = window_init(1024, 768, 0);

	for (int i = 0; video_files[i]; i++) {

		r_ctx = reader_init(video_files[i], queue_capacity);
		source_d_ctx = source_decoder_init(r_ctx, queue_capacity);
		e_ctx = encoder_init(source_d_ctx, 1);
		fov_d_ctx = fov_decoder_init(e_ctx);

		reader = SDL_CreateThread(reader_thread, "reader_thread", r_ctx);
		source_decoder = SDL_CreateThread(decoder_thread, "source_decoder_thread", source_d_ctx);
		encoder = SDL_CreateThread(encoder_thread, "encoder_thread", e_ctx);
		fov_decoder = SDL_CreateThread(decoder_thread, "fov_decoder_thread", fov_d_ctx);

		window_set_frame_queue(fov_d_ctx->frame_queue, w_ctx);
		event_loop(w_ctx);

		SDL_WaitThread(reader, NULL);
		SDL_WaitThread(source_decoder, NULL);
		SDL_WaitThread(encoder, NULL);
		SDL_WaitThread(fov_decoder, NULL);

		// FIXME: fix free functions
		//context_free(&r_ctx, &d_ctx);
		break;
	}

	return EXIT_SUCCESS;
}
