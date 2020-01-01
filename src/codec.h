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
} enc_id;

// Passed to decoder_thread through SDL_CreateThread
typedef struct decoder_context {
	Queue *packet_queue;
	Queue *frame_queue;
	AVCodecContext *avctx;
	enc_id id;
} decoder_context;

// Passed  to encoder_thread through SDL_CreateThread
typedef struct encoder_context {
	Queue *frame_queue;
	Queue *packet_queue;
	Queue *lag_queue; //timestamps to measure encoding-decoding-display lag
	AVCodecContext *avctx;
	AVDictionary *options;
	window_context *w_ctx; //required for fake-foveation using the mouse pointer
	enc_id id;
} encoder_context;

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
encoder_context *encoder_init(enc_id id, decoder_context *dec_ctx, window_context *w_ctx);

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
decoder_context *source_decoder_init(reader_context *r_ctx, int queue_capacity);

/**
 * Send a packet to the decoder, check the return value for errors.
 *
 * Calls pexit in case of a failure
 * @param avctx
 * @param packet
 */
void supply_packet(AVCodecContext *avctx, AVPacket *packet);

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
decoder_context *fov_decoder_init(encoder_context *e_ctx);

/**
 * Free the decoder_context and associated data, set d_ctx to NULL.
 *
 * Does NOT free packet_queue, which is freed through freeing its source
 * context, e.g. a reader or an encoder.
 * Finally, set d_ctx to NULL.
 * @param d_ctx decoder context to be freed.
 */
void decoder_free(decoder_context **d_ctx);
