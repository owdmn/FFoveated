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

// ids to identify supported codecs
typedef enum {
	LIBX264,
	LIBX265,
	LIBVPX,
} enc_id;

typedef struct params {
	float delta_min;
	float delta_max;
	float delta_cur;
	float std_min;
	float std_max;
	float std_cur;
} params;