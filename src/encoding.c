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

#include "encoding.h"

/**
 * Create and initialize an encoder context
 *
 * Calls pexit in case of a failure
 * @param dec_ctx context of the previous decoder
 * @param queue_capacity output packet queue capacity
 * @return encoder_context with initialized fields and opened decoder
 */
encoder_context *encoder_init(decoder_context *dec_ctx, int queue_capacity, window_context *w_ctx)
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
	enc_ctx->lag_queue = create_queue(queue_capacity);
	enc_ctx->avctx = avctx;
	enc_ctx->options = options;
	enc_ctx->w_ctx = w_ctx;

	return enc_ctx;
}