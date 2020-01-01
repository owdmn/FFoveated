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

#include "codec.h"


/**
 * Create and initialize an encoder context
 *
 * Calls pexit in case of a failure
 * @param dec_ctx context of the previous decoder
 * @param queue_capacity output packet queue capacity
 * @return encoder_context with initialized fields and opened decoder
 */
encoder_context *encoder_init(enc_id id, AVCodecContext *dec_avctx, int queue_capacity, window_context *w_ctx, Queue *frame_queue)
{
	encoder_context *enc_ctx;
	AVCodecContext *avctx;
	AVCodec *codec;
	AVDictionary *options = NULL;

	enc_ctx = malloc(sizeof(encoder_context));
	if (!enc_ctx)
		pexit("malloc failed");

	switch (id) {
		case LIBX264:
		av_dict_set(&options, "preset", "ultrafast", 0);
		av_dict_set(&options, "tune", "zerolatency", 0);
		av_dict_set(&options, "aq-mode", "autovariance", 0);
		av_dict_set(&options, "gop-size", "3", 0);
		codec = avcodec_find_encoder_by_name("libx264");
		break;
		case LIBX265:
		av_dict_set(&options, "preset", "ultrafast", 0);
		av_dict_set(&options, "tune", "zerolatency", 0);
		av_dict_set(&options, "x265-params", "aq-mode=2", 0); //autovariance
		av_dict_set(&options, "gop-size", "3", 0);
		codec = avcodec_find_encoder_by_name("libx265");
		break;
		default:
		codec = NULL;
	}

	if (!codec)
		pexit("encoder not found");

	avctx = avcodec_alloc_context3(codec);
	if (!avctx)
		pexit("avcodec_alloc_context3 failed");

	avctx->time_base = dec_avctx->time_base;
	avctx->pix_fmt = codec->pix_fmts[0]; //first supported pixel format
	avctx->width = dec_avctx->width;
	avctx->height = dec_avctx->height;

	if (avcodec_open2(avctx, avctx->codec, &options) < 0)
		pexit("avcodec_open2 failed");

	enc_ctx->frame_queue = frame_queue;
	enc_ctx->packet_queue = queue_init(queue_capacity);
	enc_ctx->lag_queue = queue_init(queue_capacity);
	enc_ctx->avctx = avctx;
	enc_ctx->options = options;
	enc_ctx->w_ctx = w_ctx;

	return enc_ctx;
}

void encoder_free(encoder_context **e_ctx)
{
	encoder_context *e;

	e = *e_ctx;
	queue_free(e->packet_queue);
	queue_free(e->lag_queue);
	avcodec_free_context(&e->avctx);
	av_dict_free(&e->options);

	free(e);
	*e_ctx = NULL;
}

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

int encoder_thread(void *ptr)
{
	encoder_context *enc_ctx = (encoder_context *) ptr;
	AVFrame *frame;
	AVPacket *packet;
	AVFrameSideData *sd;
	float *descr;
	size_t descr_size = 4*sizeof(float);
	int ret;
	int64_t *timestamp;

	packet = av_packet_alloc(); //NULL check in loop.

	for (;;) {
		if (!packet)
			pexit("av_packet_alloc failed");

		ret = avcodec_receive_packet(enc_ctx->avctx, packet);
		if (ret == 0) {
			queue_append(enc_ctx->packet_queue, packet);
			packet = av_packet_alloc();
			continue;
		} else if (ret == AVERROR(EAGAIN)) {
			frame = queue_extract(enc_ctx->frame_queue);

			if (!frame)
				break;

			sd = av_frame_new_side_data(frame, AV_FRAME_DATA_FOVEATION_DESCRIPTOR, descr_size);
			if (!sd)
				pexit("side data allocation failed");
			descr = foveation_descriptor(enc_ctx->w_ctx);
			sd->data = (uint8_t *) descr;

			frame->pict_type = 0; //keep undefined to prevent warnings
			supply_frame(enc_ctx->avctx, frame);
			av_frame_free(&frame);

			timestamp = malloc(sizeof(int64_t));
			if (!timestamp)
				perror("malloc failed");
			*timestamp = av_gettime_relative();

			queue_append(enc_ctx->lag_queue, timestamp);

		} else if (ret == AVERROR_EOF) {
			break;
		} else if (ret == AVERROR(EINVAL)) {
			pexit("avcodec_receive_packet failed");
		}
	}

	queue_append(enc_ctx->packet_queue, NULL);
	avcodec_close(enc_ctx->avctx);
	avcodec_free_context(&enc_ctx->avctx);
	return 0;
}

/**
 * Allocate a foveation descriptor to pass to an encoder
 *
 * @param w_ctx window context
 * @return float* 4-tuple: x and y coordinate, standarddeviation and offset
 */
float *foveation_descriptor(window_context *w_ctx)
{
	float *f;
	int width, height;

	SDL_GetWindowSize(w_ctx->window, &width, &height);

	f = malloc(4*sizeof(float));
	if (!f)
		pexit("malloc failed");

	#ifdef __MINGW32__
	// eye-tracking
	f[0] =
	f[1] =
	f[2] =
	f[3] =

	#else
	// fake mouse motion dummy values
	SDL_GetMouseState(&w_ctx->mouse_x, &w_ctx->mouse_y);
	f[0] = (float) w_ctx->mouse_x / width;
	f[1] = (float) w_ctx->mouse_y / height;
	f[2] = 0.3;
	f[3] = 20;
	#endif
	return f;
}


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
	d->frame_queue = queue_init(queue_capacity);
	d->avctx = avctx;

	return d;
}

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
			queue_append(dec_ctx->frame_queue, frame);
			frame = av_frame_alloc();
			continue;
		} else if (ret == AVERROR(EAGAIN)) {
			//provide another packet to the decoder
			packet = queue_extract(dec_ctx->packet_queue);
			supply_packet(avctx, packet);
			av_packet_free(&packet);
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
	queue_append(dec_ctx->frame_queue, NULL);
	avcodec_close(avctx);
	return 0;
}

decoder_context *fov_decoder_init(Queue *packet_queue, enc_id id)
{
	AVCodecContext *avctx;
	AVCodec *codec;
	decoder_context *d;
	int ret;

	switch (id) {
		case LIBX264:
        printf("looking for libx264\n");
		codec = avcodec_find_decoder_by_name("libx264");
		break;
		case LIBX265:
		codec = avcodec_find_decoder_by_name("hevc");
        printf("looking for libx265: %p \n", (void *)codec);
		break;
		default:
		codec = NULL;
	}

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

	d->packet_queue = packet_queue;
	d->frame_queue = queue_init(1);
	d->avctx = avctx;

	return d;
}

void decoder_free(decoder_context **d_ctx)
{
	decoder_context *d;

	d = *d_ctx;
	avcodec_free_context(&d->avctx);
	queue_free(d->frame_queue);
	/* packet_queue is freed by reader_free! */
	free(d);
	d = NULL;
}