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

#pragma once

#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include "io.h"

// Passed  to encoder_thread through SDL_CreateThread
typedef struct encoder_context {
	Queue *frame_queue;
	Queue *packet_queue;
	Queue *lag_queue; //timestamps to measure encoding-decoding-display lag
	AVCodecContext *avctx;
	AVDictionary *options;
	window_context *w_ctx; //required for foveation...
} encoder_context;

/**
 * ids for supported encoders
 */
typedef enum {
	LIBX264,
	LIBX265,
} enc_id;

/**
 * Create and initialize an encoder context
 *
 * Calls pexit in case of a failure
 * @param avctx av codec context of the previous decoder: dec_ctx->avctx
 * @param queue_capacity output packet queue capacity
 * @return encoder_context with initialized fields and opened decoder
 */
encoder_context *encoder_init(enc_id id, AVCodecConext *avctx, int queue_capacity, window_context *w_ctx);

/**
 * Free the encoder context and associated data.
 *
 * frame_queue and w_ctx have to be freed by the respecive decoder_free
 * and window_free funcions.
 * @param e_ctx encoder_context to be freed.
 */
void encoder_free(encoder_context **e_ctx);

/**
 * Supply the given codec with a frame, handle errors appropriately.
 *
 * Calls pexit in dase of a failure.
 * @param avctx context of the codec being supplied
 * @param frame the frame to be supplied
 */
void supply_frame(AVCodecContext *avctx, AVFrame *frame);

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
int encoder_thread(void *ptr);

/**
 * Allocate a foveation descriptor to pass to an encoder
 *
 * @param w_ctx window context
 * @return float* 4-tuple: x and y coordinate, standarddeviation and offset
 */
float *foveation_descriptor(window_context *w_ctx);
