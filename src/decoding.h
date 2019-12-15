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
#include "io.h"

// Passed to decoder_thread through SDL_CreateThread
typedef struct decoder_context {
	Queue *packet_queue;
	Queue *frame_queue;
	AVCodecContext *avctx;
} decoder_context;


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
