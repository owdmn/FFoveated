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

// ids to identify supported codecs
typedef enum {
	LIBX264,
	LIBX265,
	LIBVPX,
} enc_id;

/**
 * Decoder context / status information.
 * Queues allow to consume packets and emit frames.
 * Passed to decoder_thread through SDL_CreateThread
 */
typedef struct dec_ctx {
	Queue *packets; //input
	Queue *frames;  //output
	AVCodecContext *avctx; //to access internals (time_base etc.)
	enc_id id;
} dec_ctx;

/**
 * Encoder context / status information.
 * Passed  to encoder_thread through SDL_CreateThread
 */
typedef struct enc_ctx {
	Queue *packets; //output
	Queue *frames;  //input
	Queue *timestamps; //timestamps to measure encoding-decoding-display lag
	AVCodecContext *avctx;
	AVDictionary *options; //encoder options
	win_ctx *wc; //required for fake-foveation using the mouse pointer
	enc_id id;
} enc_ctx;

/**
 * Initialize a realtime (re)encoder
 *
 * Output queues have length 1 to enforce consumption of already processed
 * frames before futher frames can be added, as additional buffering is unnecessary
 * in real time applications.
 * @param id identifies the encoder to use.
 * @param dc context of the decoder which supplies the frames, to set e.g. the time base.
 * @param w_ctx window context, necessary for pseudo-gaze emulation through the mouse pointer.
 * @return enc_ctx with fields initialized and an opened encoder
 */
enc_ctx *encoder_init(enc_id id, dec_ctx *dc, win_ctx *wc);

/**
 * Free the encoder context and associated data.
 *
 * frames and wc have to be freed separately, usually through
 * the decoder_free and window_free functions.
 * @param ec encoder context to be freed.
 */
void encoder_free(enc_ctx **ec);

/**
 * Encode AVFrames, put the resulting AVPacktes in a queue
 *
 * Call avcodec_receive_packet in a loop, enqueue encoded packets.
 * Adds NULL packet to queue in the end.
 * @param ptr will be casted to (enc_ctx *)
 * @return int 0 on success
 */
int encoder_thread(void *ptr);

/**
 * Allocate a foveation descriptor to pass to an encoder as AVSideData
 *
 * @param wc required for pseudo-foveation through the mouse pointer.
 * @return float* 4-tuple: x and y coordinate, stddev and max quality offset
 */
float *foveation_descriptor(win_ctx *wc);

/**
 * Initialize a source decoder.
 *
 * This function copies information from a reader context, to share e.g.
 * the reader's output (packets queue) and use fetch input for the decoder.
 * @param rc to copy fctx, packets and stream_index from.
 * @param queue_capacity output buffer size.
 * @return decoder_context with members initialized and an opened decoder.
 */
dec_ctx *source_decoder_init(rdr_ctx *rc, int queue_capacity);

/**
 * Decode AVPackets and put the uncompressed AVFrames in a queue.
 *
 * Call avcodec_receive_frame in a loop, enqueue decoded frames.
 * Adds NULL packet to queue in the end.
 * @param *ptr will be cast to (decoder_context *)
 * @return int 0 on success.
 */
int decoder_thread(void *ptr);

/**
 * Initialize a foveated decoder.
 *
 * @param ec used to copy e.g. the codec id from.
 * @return decoder_context* with members initialized and an opened decoder.
 */
dec_ctx *fov_decoder_init(enc_ctx *ec);

/**
 * Free the decoder_context and associated data, set d_ctx to NULL.
 *
 * Does not free packets, which is freed through freeing its source context,
 * e.g. a reader or an encoder. Set dc to NULL.
 * @param dc decoder context to be freed.
 */
void decoder_free(dec_ctx **dc);
