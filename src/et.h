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

#ifdef ET
#include <iViewXAPI.h>
#endif

#include <SDL2/SDL.h>

typedef struct gaze_struct {
	int screen_x;
	int screen_y;
	double diam_l; //pupil diameter
	double diam_r;
	double distance; //mean eye-screen dist
	SDL_mutex *mutex;
} gaze_struct;

typedef struct tracker_position {
	double screen_width;
	double screen_height;
	double tracker_depth;
	double tracker_height;
	double tracker_angle; //20° with the SMI bracket
} tracker_position;
