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

static void set_codec_options(AVDictionary **opt, enc_id id)
{
	switch (id) {
	case LIBX264:
		av_dict_set(opt, "preset", "ultrafast", 0);
		av_dict_set(opt, "tune", "zerolatency", 0);
		av_dict_set(opt, "aq-mode", "1", 0);
		av_dict_set(opt, "gop-size", "3", 0);
		break;
	case LIBX265:
		av_dict_set(opt, "preset", "ultrafast", 0);
		av_dict_set(opt, "tune", "zerolatency", 0);
		av_dict_set(opt, "x265-params", "aq-mode=1", 0);
		av_dict_set(opt, "gop-size", "3", 0);
		break;
	default:
		pexit("trying to set options for unsupported codec");
	}
}

enc_ctx *encoder_init(enc_id id, dec_ctx *dc)
{
	enc_ctx *ec;
	AVCodecContext *avctx;
	AVCodec *codec;
	AVDictionary *options = NULL;

	ec = malloc(sizeof(enc_ctx));
	if (!ec)
		pexit("malloc failed");

	switch (id) {
	case LIBX264:
		set_codec_options(&options, LIBX264);
		codec = avcodec_find_encoder_by_name("libx264");
		break;
	case LIBX265:
		set_codec_options(&options, LIBX265);
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

	avctx->time_base	= dc->avctx->time_base;
	avctx->pix_fmt		= codec->pix_fmts[0]; //first supported pixel format
	avctx->width		= dc->avctx->width;
	avctx->height		= dc->avctx->height;

	if (avcodec_open2(avctx, avctx->codec, &options) < 0)
		pexit("avcodec_open2 failed");

	ec->frames = dc->frames;
	/* output queues have length 1 to enforce RT processing */
	ec->packets = queue_init(1);
	ec->timestamps = queue_init(1);

	ec->avctx = avctx;
	ec->options = options;
	ec->id = id;

	return ec;
}

void encoder_free(enc_ctx **ec)
{
	enc_ctx *e;

	e = *ec;
	queue_free(&e->frames);
	avcodec_free_context(&e->avctx);
	av_dict_free(&e->options);
	free(e);
	*ec = NULL;
}

static void supply_frame(AVCodecContext *avctx, AVFrame *frame)
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
	enc_ctx *ec = (enc_ctx *) ptr;
	AVFrame *frame;
	AVPacket *pkt;
	AVFrameSideData *sd;
	float *descr;
	size_t descr_size = 4*sizeof(float);
	int ret;
	int64_t *timestamp;

	pkt = av_packet_alloc(); //NULL check in loop.

	for (;;) {
		if (!pkt)
			pexit("av_packet_alloc failed");

		ret = avcodec_receive_packet(ec->avctx, pkt);
		if (ret == 0) {
			queue_append(ec->packets, pkt);
			pkt = av_packet_alloc();
			continue;
		} else if (ret == AVERROR(EAGAIN)) {
			frame = queue_extract(ec->frames);

			if (!frame)
				break;

			sd = av_frame_new_side_data(frame, AV_FRAME_DATA_FOVEATION_DESCRIPTOR, descr_size);
			if (!sd)
				pexit("side data allocation failed");
			descr = foveation_descriptor();
			sd->data = (uint8_t *) descr;

			frame->pict_type = 0; //keep undefined to prevent warnings
			supply_frame(ec->avctx, frame);
			av_frame_free(&frame);

			timestamp = malloc(sizeof(int64_t));
			if (!timestamp)
				perror("malloc failed");
			*timestamp = av_gettime_relative();

			queue_append(ec->timestamps, timestamp);

		} else if (ret == AVERROR_EOF) {
			break;
		} else if (ret == AVERROR(EINVAL)) {
			pexit("avcodec_receive_packet failed");
		}
	}

	queue_append(ec->packets, NULL);
	queue_append(ec->timestamps, NULL);
	avcodec_close(ec->avctx);
	avcodec_free_context(&ec->avctx);
	return 0;
}


dec_ctx *source_decoder_init(rdr_ctx *rc, int queue_capacity)
{
	AVCodecContext *avctx;
	dec_ctx *dc;
	int ret;
	int index = rc->stream_index;
	AVStream *stream = rc->fctx->streams[index];
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

	dc = malloc(sizeof(dec_ctx));
	if (!dc)
		pexit("malloc failed");

	dc->packets = rc->packets;
	dc->frames = queue_init(queue_capacity);
	dc->avctx = avctx;

	return dc;
}

static void supply_packet(AVCodecContext *avctx, AVPacket *packet)
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
	dec_ctx *dc = (dec_ctx *) ptr;
	AVCodecContext *avctx = dc->avctx;
	AVFrame *frame;
	AVPacket *packet;

	frame = av_frame_alloc();
	for (;;) {
		if (!frame)
			pexit("av_frame_alloc failed");

		ret = avcodec_receive_frame(avctx, frame);
		if (ret == 0) {
			// valid frame - enqueue and allocate new buffer
			queue_append(dc->frames, frame);
			frame = av_frame_alloc();
			continue;
		} else if (ret == AVERROR(EAGAIN)) {
			//provide another packet to the decoder
			packet = queue_extract(dc->packets);
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
	queue_append(dc->frames, NULL);
	avcodec_close(avctx);
	return 0;
}

dec_ctx *fov_decoder_init(enc_ctx *ec)
{
	AVCodecContext *avctx;
	AVCodec *codec;
	dec_ctx *dc;
	int ret;

	codec = avcodec_find_decoder(ec->avctx->codec->id);

	if (!codec)
		pexit("avcodec_find_decoder_by_name failed");

	avctx = avcodec_alloc_context3(codec);
	if (!avctx)
		pexit("avcodec_alloc_context3 failed");

	ret = avcodec_open2(avctx, codec, NULL);
	if (ret < 0)
		pexit("avcodec_open2 failed");

	dc = malloc(sizeof(dec_ctx));
	if (!dc)
		pexit("malloc failed");

	dc->packets = ec->packets;
	dc->frames = queue_init(1);
	dc->avctx = avctx;

	return dc;
}

void decoder_free(dec_ctx **dc)
{
	dec_ctx *d;

	d = *dc;
	avcodec_free_context(&d->avctx);
	queue_free(&d->packets);
	free(d);
	*dc = NULL;
}

params *params_limit_init(enc_id id)
{
	params *p;

	p = malloc(sizeof(params));
	if (!p)
		pexit("malloc failed");

	switch (id) {
	case LIBX264:
		p->delta_min = 0;
		p->delta_max = 51;
		p->std_min = 0;
		p->std_max = 2;
		break;
	default:
		pexit("requested params for unsupported codec");
	}
	return p;
}
