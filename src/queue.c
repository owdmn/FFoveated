/*
 * Copyright (C) 2020 Oliver Wiedemann
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in/ the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "queue.h"

/**
 * Print formatted error message referencing the affeted source file,
 * line and the errno status through perror (3), then exit with EXIT_FAILURE.
 * Likely used through the pexit macro for comfort.
 *
 * @param msg error message
 * @param file usually the __FILE__ macro
 * @param line usually the __LINE__ macro
 */
static void pexit_(const char *msg, const char *file, const int line)
{
	char buf[1024];

	snprintf(buf, sizeof(buf), "%s:%d: %s", file, line, msg);
	perror(buf);
	exit(EXIT_FAILURE);
}

// Convenience macro to report runtime errors with debug information.
#define pexit(s) pexit_(s, __FILE__, __LINE__)

Queue *queue_init(size_t capacity)
{
	Queue *q;

	q = (Queue *) malloc(sizeof(Queue));
	if (!q)
		pexit("malloc failed");

	// allocate one additional element for signalling
	q->data = malloc((capacity+1) * sizeof(void *));
	if (!q->data)
		pexit("malloc failed");

	q->front = 0;
	q->rear  = 0;
	q->capacity = capacity;

	q->mutex = SDL_CreateMutex();
	q->full  = SDL_CreateCond();
	q->empty = SDL_CreateCond();
	if (!(q->mutex && q->full && q->empty))
		pexit("SDL_CreateMutex or SDL_CreateCond failed");
	return q;
}

/*
static void queue_flush(Queue *q, void (*free_fkt)(void *))
{

}
*/

void queue_free(Queue **q)
{
	Queue *qd = *q;

	SDL_DestroyMutex(qd->mutex);
	SDL_DestroyCond(qd->full);
	SDL_DestroyCond(qd->empty);
	free(qd->data);
	free(*q);
}

void queue_append(Queue *q, void *data)
{
	unsigned int new_rear;

	if (SDL_LockMutex(q->mutex))
		pexit(SDL_GetError());

	new_rear = (q->rear + 1) % (q->capacity + 1);
	//check if full
	if (new_rear == q->front) {
		if (SDL_CondWait(q->full, q->mutex))
			pexit(SDL_GetError());
	}
	q->data[q->rear] = data;
	q->rear = new_rear;
	/* at least one item is now queued*/
	if (SDL_CondSignal(q->empty))
		pexit(SDL_GetError());

	if (SDL_UnlockMutex(q->mutex))
		pexit(SDL_GetError());

}

void *queue_extract(Queue *q)
{
	void *data;

	if (SDL_LockMutex(q->mutex))
		pexit(SDL_GetError());

	//check if empty
	if (q->front == q->rear) {
		if (SDL_CondWait(q->empty, q->mutex))
			pexit(SDL_GetError());
	}

	data = q->data[q->front];
	q->front = (q->front + 1) % (q->capacity + 1);

	if (SDL_CondSignal(q->full))
		pexit(SDL_GetError());
	if (SDL_UnlockMutex(q->mutex))
		pexit(SDL_GetError());

	return data;
}
