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

#include <SDL2/SDL.h>
#include "common.h"
#include "codec.h"
#include "io.h"
#ifdef ET
#include <iViewXAPI.h>
#endif

/* Data to be set as REDGeometryStruct */
typedef struct lab_setup {
	double screen_width; // in mm mm
	double screen_height;
	double camera_x;
	double camera_z;
	double camera_inclination;
} lab_setup;

typedef struct eye_data {
	double diam;
	double x; //3d coordinates in mm relative to the camera
	double y;
	double z;
	double gazeX;
	double gazeY;
} eye_data;

typedef struct gaze {
	int mean_x;
	int mean_y;
	eye_data left;
	eye_data right;
	double distance; //mean eye-screen distance
	SDL_mutex *mutex;
} gaze;

/**
 * Setup eye-tracking (or pseudo-foveation)
 *
 * @param w SDL_Window, used for mouse-based pseudofoveation
 * @param id codec id to infer parameters
 */
void setup_ivx(SDL_Window *w, enc_id id);

/**
 * Allocate a foveation descriptor to pass to an encoder as AVSideData
 *
 * @param wc required for pseudo-foveation through the mouse pointer.
 * @return float* 4-tuple: x and y coordinate, stddev and max quality offset
 */
float *foveation_descriptor();
