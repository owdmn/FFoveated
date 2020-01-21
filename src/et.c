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

#include "et.h"
#include "pexit.h"

static gaze *gs;
static lab_setup *ls;
static SDL_Window *win;
static params *p;

float *foveation_descriptor()
{
	float *fd;
	int x, y;
	int win_x, win_y, w, h;

	SDL_GetWindowSize(win, &w, &h);
	SDL_GetWindowPosition(win, &win_x, &win_y);

	fd = malloc(4*sizeof(float));
	if (!fd)
		pexit("malloc failed");

	#ifdef ET
	// eye-tracking
	x = (float) (gs->gazeX_mean - win_x) / w;
	y = (float) (gs->gazeY_mean - win_y) / h;

	fd[0] = x;
	fd[1] = y;
	fd[2] = 0.3;
	fd[3] = 40;

	#else
	// fake mouse motion dummy values
	SDL_GetMouseState(&x, &y);

	fd[0] = (float) x / w;
	fd[1] = (float) y / h;
	fd[2] = 0.3;
	fd[3] = 50;
	#endif
	return fd;
}

#ifdef ET
int __stdcall update_gaze(struct SampleStruct sampleData)
{
	double x, y, z; //mean eye coordinates for distance
	//double theta;

	SDL_LockMutex(gs->mutex);
	gs->left.x = sampleData.leftEye.eyePositionX;
	gs->left.y = sampleData.leftEye.eyePositionY;
	gs->left.z = sampleData.leftEye.eyePositionZ;
	gs->right.x = sampleData.rightEye.eyePositionX;
	gs->right.y = sampleData.rightEye.eyePositionY;
	gs->right.z = sampleData.rightEye.eyePositionZ;
	gs->right.gazeX = sampleData.rightEye.gazeX;
	gs->left.gazeX = sampleData.leftEye.gazeX;
	gs->right.gazeY = sampleData.rightEye.gazeY;
	gs->left.gazeY = sampleData.leftEye.gazeY;

	gs->left.diam = sampleData.leftEye.diam;
	gs->right.diam = sampleData.rightEye.diam;

	/* mean eye coordinates in 3d space */
	x = (gs->left.x + gs->right.x) / 2;
	y = (gs->left.y + gs->right.y) / 2;
	z = (gs->left.z + gs->right.z) / 2;

	gs->gazeX_mean = (gs->left.gazeX + gs->right.gazeX) / 2;
	gs->gazeY_mean = (gs->left.gazeY + gs->right.gazeY) / 2;

	//theta = ls->camera_inclination;


	//shift + rotate to map camera coordinates to screen-specific 3d coordinates
	//calculate screen pixel coordinates in 3d space
	//calculate distance

	// distance towards eyetracker, not screen!!!
	gs->distance = sqrt(x*x + y*y + z*z);

	SDL_UnlockMutex(gs->mutex);

	//SDL_UnlockMutex(gs->mutex);
	return 0;
}
#endif

void setup_ivx(SDL_Window *w, enc_id id)
{

	// common setup for ET and non-ET applications
	win = w;
	gs = malloc(sizeof(gaze));
	if (!gs)
		pexit("malloc failed");

	ls = malloc(sizeof(lab_setup));
	if (!ls)
		pexit("malloc failed");

	// Hardcoded 15.6" 16:9 FHD notebook display dimensions
	ls->screen_width = 345;
	ls->screen_height = 194;
	ls->camera_x = 0;
	ls->camera_z = 0;
	ls->camera_inclination = 20; //degrees upward for the SMI bracket
	gs->mutex = SDL_CreateMutex();
	p = params_limit_init(id);

	#ifdef ET

	struct AccuracyStruct accuracyData;
	struct SystemInfoStruct systemData;
	struct CalibrationStruct calibrationData;
	struct SpeedModeStruct speedData;

	int ret_calibrate = 0;
	int ret_validate = 0;
	int ret_connect = 0;

	char localhost[] = "127.0.0.1";

	ret_connect = iV_Connect(localhost, 4444, localhost, 5555);

	switch (ret_connect) {
	case RET_SUCCESS:
		fprintf(stderr, "Successfully connected to SMI Server\n");
		break;
	case ERR_COULD_NOT_CONNECT:
		fprintf(stderr, "Error: Could not connect to SMI Server\n");
		break;
	default:
		fprintf(stderr, "Error: iV_Connect() returned: %d\n", ret_connect);
		exit(1);
	}

	iV_GetSpeedModes(&speedData);

	iV_ShowEyeImageMonitor();
	iV_ShowTrackingMonitor();
	getchar();

	// Eyetracker calibration
	calibrationData.method = 2;
	calibrationData.speed = 1;
	calibrationData.displayDevice = 0;
	calibrationData.targetShape = 2;
	calibrationData.foregroundBrightness = 250;
	calibrationData.backgroundBrightness = 230;
	calibrationData.autoAccept = 2;
	calibrationData.targetSize = 20;
	calibrationData.visualization = 1;
	strncpy(calibrationData.targetFilename, "", 256);

	iV_SetupCalibration(&calibrationData);
	// start calibration
	ret_calibrate = iV_Calibrate();

	iV_SetSampleCallback(update_gaze);
	#endif
}
