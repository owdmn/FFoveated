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

#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


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
    char **video_files;
    int used;
    int size;

    fp = fopen(pathname, "r");
    if (!fp) {
        perror("Error: fopen failed in parse_files_lines: ");
        exit(EXIT_FAILURE);
    }

    size = 32; // initial allocation
    used =  0;
    video_files = malloc(size * sizeof(char*));
    if (!video_files) {
        perror("Error: malloc() failes in parse_file_lines: ");
        exit(EXIT_FAILURE);
    }

    /* separate and copy filenames into null-terminated strings */
    while (fgets(line_buf, PATH_MAX, fp)) {
        if(newline = strchr(line_buf, '\n')) {
            *newline = '\0'; // remove trailing newline
        }

        video_files[used] = strdup(line_buf);
        used++;
        if (used == size) {
            size = size*2;
            video_files = realloc(video_files, size * sizeof(char*));
            if (!video_files) {
                perror("Error: realloc failed in parse_file_lines: ");
                exit(EXIT_FAILURE);
            }
        }
    }
    video_files[used] = NULL; //termination symbol

    return video_files;
}


int main (int argc, char **argv)
{
    char **video_files = NULL;

    if (argc != 2) {
        exit(EXIT_FAILURE);
    }

    video_files = parse_file_lines(argv[1]);

    for (int i=0; video_files[i]; i++) {
        printf("video_files[i]: %s\n", video_files[i]);
    }

}
