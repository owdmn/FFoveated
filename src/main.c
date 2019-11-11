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

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print formatted error message referencing the affeted source file,
 * line and the errno status through perror, then exit with EXIT_FAILURE.
 * Can be used through the pexit macro for comfort.
 *
 * @param msg error message
 * @param file usually the __FILE__ macro
 * @param line usually the __LINE__ macro
 */
void pexit_(const char *msg, const char *file, const int line)
{
	char buf[1024];

	snprintf(buf, sizeof(buf), "%s:%d: %s", file, line, msg);
	perror(buf);
	exit(EXIT_FAILURE);
}
#define pexit(s) pexit_(s, __FILE__, __LINE__)


/**
 * Parse a file line by line, return an array of char* pointers
 *
 * At most PATH_MAX characters per line are supported, the main purpose
 * is to parse a file containing pathnames.
 * Each line is sanitized: A trailing newline characters are replaced
 * with a nullbyte. The returned pointer array is also NULL terminated.
 * All contained pointers and the array itself must be passed to free()
 *
 * @param pathname path to an ascii file to be opened and parsed
 * @return NULL-terminated array of char* to line contents
 */
char **parse_file_lines(const char *pathname)
{
	FILE *fp;
	char line_buf[PATH_MAX];
	char *newline;
	char **lines;
	int used;
	int size;

	fp = fopen(pathname, "r");
	if (!fp) {
		perror("Error: fopen failed.");
		exit(EXIT_FAILURE);
	}

	size = 32; //initial allocation
	used =	0;
	lines = malloc(size * sizeof(char *));
	if (!lines) {
		perror("Error: malloc failed.");
		exit(EXIT_FAILURE);
	}

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
			if (!lines) {
				perror("Error: realloc failed.");
				exit(EXIT_FAILURE);
			}
		}
	}
	lines[used] = NULL;		//termination symbol

	return lines;
}

void display_usage(char *progname)
{
	printf("usage:\n$ %s infile\n", progname);
}

int main(int argc, char **argv)
{
	char **video_files = NULL;

	if (argc != 2) {
		display_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	video_files = parse_file_lines(argv[1]);

	for (int i = 0; video_files[i]; i++)
		printf("video_files[i]: %s\n", video_files[i]);
}
