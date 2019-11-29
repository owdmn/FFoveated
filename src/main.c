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
 * Allocate an AVCodecContext and open a suitable decoder.
 *
 * Usually called through decoder_thread.
 * Calls pexit in case of a failure.
 * @param format_ctx AVFormatContext to open the decoder on.
 * @param stream_index The element of interest in format_ctx->streams
 */
AVCodecContext *open_decoder(AVFormatContext *format_ctx, int stream_index)
{
	int ret;
	AVCodecContext *codec_ctx;
	AVCodec *codec;
	AVStream *video_stream = format_ctx->streams[stream_index];

	codec_ctx = avcodec_alloc_context3(NULL);
	if (!codec_ctx)
		pexit("avcodec_alloc_context3 failed");

	ret = avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);
	if (ret < 0)
		pexit("avcodec_parameters_to_context failed");

	codec_ctx->time_base = video_stream->time_base;
	codec = avcodec_find_decoder(codec_ctx->codec_id);
	if (!codec)
		pexit("avcodec_find_decoder failed");

	codec_ctx->codec_id = codec->id;

	ret = avcodec_open2(codec_ctx, codec, NULL);
	if (ret < 0)
		pexit("avcodec_open2 failed");

	return codec_ctx;
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
 * Open a suitable decoder on dec_ctx->format_ctx with the respective stream_index.
 * Call avcodec_receive_frame in a loop,
 *
 * Calls pexit in case of a failure
 * @param *ptr will be cast to (decoder_context *)
 * return int 0 on success.
 */
int decoder_thread(void *ptr)
{
	int ret;
	decoder_context *dec_ctx = (decoder_context *) ptr;
	AVCodecContext *avctx;
	AVFrame *frame;
	AVPacket *packet;

	avctx = open_decoder(dec_ctx->format_ctx, dec_ctx->stream_index);

	frame = av_frame_alloc();
	if (!frame)
		pexit("av_frame_alloc failed");

	for (;;) {
		ret = avcodec_receive_frame(avctx, frame);
		if (ret == 0) {
			// valid frame - enqueue and allocate new buffer
			enqueue(dec_ctx->frame_queue, frame);
			frame = av_frame_alloc();
			if (!frame)
				pexit("av_frame_alloc failed");
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
decoder_context *decoder_init(reader_context *r_ctx, int queue_capacity)
{
	decoder_context *d;

	d = malloc(sizeof(decoder_context));
	if (!d)
		pexit("malloc failed");

	d->format_ctx = r_ctx->format_ctx;
	d->packet_queue = r_ctx->packet_queue;
	d->stream_index = r_ctx->stream_index;
	d->frame_queue = create_queue(queue_capacity);

	return d;
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


int main(int argc, char **argv)
{
	char **video_files;
	reader_context *r_ctx;
	decoder_context *d_ctx;
	SDL_Thread *reader, *decoder;
	const int queue_capacity = 32;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	video_files = parse_file_lines(argv[1]);

	for (int i = 0; video_files[i]; i++) {

		r_ctx = reader_init(video_files[i], queue_capacity);
		d_ctx = decoder_init(r_ctx, queue_capacity);

		reader = SDL_CreateThread(reader_thread, "reader_thread", r_ctx);
		decoder = SDL_CreateThread(decoder_thread, "decoder_thread", d_ctx);

		SDL_WaitThread(reader, NULL);
		SDL_WaitThread(decoder, NULL);
		break; //DEMO
	}

	return EXIT_SUCCESS;
}
