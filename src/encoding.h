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
#include "decoding.h"
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
 * Create and initialize an encoder context
 *
 * Calls pexit in case of a failure
 * @param dec_ctx context of the previous decoder
 * @param queue_capacity output packet queue capacity
 * @return encoder_context with initialized fields and opened decoder
 */
encoder_context *encoder_init(decoder_context *dec_ctx, int queue_capacity, window_context *w_ctx);

/**
 * Supply the given codec with a frame, handle errors appropriately.
 *
 * Calls pexit in dase of a failure.
 * @param avctx context of the codec being supplied
 * @param frame the frame to be supplied
 */
void supply_frame(AVCodecContext *avctx, AVFrame *frame);
