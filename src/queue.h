/*
 * Copyright (C) 2020 Oliver Wiedemann
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
#include <SDL2/SDL.h>

/**
 * Container for a generic queue and associated metadata.
 *
 * Signalling is implemented by having data[rear] always point to an
 * unused location, therefore capacity+1 elements have to be allocated.
 */
typedef struct Queue {
	void **data;
	size_t capacity;
	unsigned int front;
	unsigned int rear;
	SDL_mutex *mutex;
	SDL_cond *full;
	SDL_cond *empty;
} Queue;

/**
 * Create and initialize a queue structure.
 *
 * Allocates storage on the heap.
 * Use free_queue to dispose of pointers acquired through this function.
 * @param capacity number of elements the queue is able to store.
 * @return initialized queue. See enqueue, dequeue, free_queue.
 */
Queue *queue_init(size_t capacity);

/**
 * Free a Queue.
 *
 * The mutex and the full/empty condition variables are destroyed.
 * Calls free on both q->data and subsequently q itself, which is set to NULL.
 * This function does not take care of any remaining elements in the queue!
 * These have to be handled manually. Caution: This can lead to data leaks.
 */
void queue_free(Queue **q);

/**
 * Add data to end of the queue.
 *
 * Blocks if there is no space left, waiting for a signal on the full condition variable.
 * @param q Queue acquired through queue_init.
 * @param data will be appended to q->data.
 */
void queue_append(Queue *q, void *data);

/**
 * Extract the first element of a queue.
 *
 * Blocks if there is no element in q, waiting for a signal on the empty condition variable.
 * Pointers are not safely removed from the queue (not overwritten) and might still be
 * accessible at a later point in time.
 * @param q pointer to a valid Queue acquired through queue_init.
 * @return void* the first element of q.
 */
void *queue_extract(Queue *q);
