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

/**
 * ids for supported encoders
 */
typedef enum {
	LIBX264,
	LIBX265,
	LIBVPX,
} enc_id;

// Passed to decoder_thread through SDL_CreateThread
typedef struct dec_ctx {
	Queue *packets; //input
	Queue *frames;  //output
	AVCodecContext *avctx;
	enc_id id;
} dec_ctx;

// Passed  to encoder_thread through SDL_CreateThread
typedef struct enc_ctx {
	Queue *packets; //output
	Queue *frames;  //input
	Queue *timestamps; //timestamps to measure encoding-decoding-display lag
	AVCodecContext *avctx;
	AVDictionary *options;
	win_ctx *wc; //required for fake-foveation using the mouse pointer
	enc_id id;
} enc_ctx;

/**
 * Create and initialize a realtime (re)encoder context
 *
 * Output queues have length 1 to enforce consumption of already processed
 * frames before futher frames can be added. Further buffering is unnecessary
 * in real-time applications.
 * Calls pexit in case of a failure
 * @param id internal id of supported codecs
 * @param dec_ctx context of the supplying decoder
 * @param w_ctx window context, necessary für fake-gaze through the mouse pointer
 * @return encoder_context with initialized fields and opened decoder
 */
enc_ctx *encoder_init(enc_id id, dec_ctx *dc, win_ctx *wc);

/**
 * Free the encoder context and associated data.
 *
 * frame_queue and w_ctx have to be freed by the respecive decoder_free
 * and window_free funcions.
 * @param e_ctx encoder_context to be freed.
 */
void encoder_free(enc_ctx **ec);

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
float *foveation_descriptor(win_ctx *wc);
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
dec_ctx *source_decoder_init(rdr_ctx *rc, int queue_capacity);

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
int decoder_thread(void *ptr);

/**
 * Create and initialize a decoder context.
 *
 * The decoder context inherits the necessary fields from an encoder context.
 *
 * Calls pexit in case of a failure.
 * @param e_ctx foveated encoder context
 * @return decoder_context* with all members initialized.
 */
dec_ctx *fov_decoder_init(enc_ctx *ec);

/**
 * Free the decoder_context and associated data, set d_ctx to NULL.
 *
 * Does NOT free packet_queue, which is freed through freeing its source
 * context, e.g. a reader or an encoder.
 * Finally, set d_ctx to NULL.
 * @param d_ctx decoder context to be freed.
 */
void decoder_free(dec_ctx **dc);
