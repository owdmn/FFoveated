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

#include "helpers.h"

void pexit_(const char *msg, const char *file, const int line)
{
	char buf[1024];

	snprintf(buf, sizeof(buf), "%s:%d: %s", file, line, msg);
	perror(buf);
	exit(EXIT_FAILURE);
}


Queue *create_queue(size_t capacity)
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

void free_queue(Queue *q)
{
	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCond(q->full);
	SDL_DestroyCond(q->empty);
	free(q->data);
	free(q);
}


void enqueue(Queue *q, void *data)
{
	unsigned int new_rear;

	if (SDL_LockMutex(q->mutex))
		pexit(SDL_GetError());

	new_rear = (q->rear + 1) % (q->capacity + 1);

	//check if full
	if (new_rear == q->front) {
		fprintf(stderr, "queue is full, waiting\n");
		if (SDL_CondWait(q->full, q->mutex))
			pexit(SDL_GetError());
		fprintf(stderr, "got full signal!\n");
	}

	q->data[q->rear] = data;
	q->rear = new_rear;

	/* at least one item is now queued*/
	if (SDL_CondSignal(q->empty))
		pexit(SDL_GetError());

	if (SDL_UnlockMutex(q->mutex))
		pexit(SDL_GetError());

}

void *dequeue(Queue *q)
{
	void *data;

	if (SDL_LockMutex(q->mutex))
		pexit(SDL_GetError());

	//check if empty
	if (q->front == q->rear) {
		fprintf(stderr, "queue is empty, waiting\n");
		if (SDL_CondWait(q->empty, q->mutex))
			pexit(SDL_GetError());
		fprintf(stderr, "got empty signal");
	}

	data = q->data[q->front];
	q->front = (q->front + 1) % (q->capacity + 1);

	if (SDL_CondSignal(q->full))
		pexit(SDL_GetError());
	if (SDL_UnlockMutex(q->mutex))
		pexit(SDL_GetError());

	return data;
}


char **parse_file_lines(const char *pathname)
{
	FILE *fp;
	char line_buf[PATH_MAX];
	char *newline;
	char **lines;
	int used;
	int size;

	fp = fopen(pathname, "r");
	if (!fp)
		pexit("fopen failed");

	size = 32; //initial allocation
	used =	0;
	lines = malloc(size * sizeof(char *));
	if (!lines)
		pexit("malloc failed");

	/* separate and copy filenames into null-terminated strings */
	while (fgets(line_buf, PATH_MAX, fp)) {
		newline = strchr(line_buf, '\n');
		if (newline)
			*newline = '\0';	//remove trailing newline

		lines[used] = strdup(line_buf);
		used++;
		if (used == size) {
			size = size*2;
			lines = realloc(lines, size * sizeof(char *));
			if (!lines)
				pexit("realloc failed");
		}
	}
	lines[used] = NULL;		//termination symbol

	return lines;
}