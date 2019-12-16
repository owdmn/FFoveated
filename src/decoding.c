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

#include "decoding.h"

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

decoder_context *fov_decoder_init(Queue *packet_queue)
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

	d->packet_queue = packet_queue;
	d->frame_queue = create_queue(1);
	d->avctx = avctx;

	return d;
}