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

static gaze *gs;
static lab_setup *ls;
static SDL_Window *win;


float *foveation_descriptor()
{
	float *fd;
	int x, y, w, h;

	fd = malloc(4*sizeof(float));
	if (!fd)
		pexit("malloc failed");

	#ifdef ET
	// eye-tracking
	fd[0] =
	fd[1] =
	fd[2] =
	fd[3] =

	#else
	// fake mouse motion dummy values

	SDL_GetMouseState(&x, &y);
	SDL_GetWindowSize(win, &w, &h);

	fd[0] = (float) x / w;
	fd[1] = (float) y / h;
	fd[2] = 0.3;
	fd[3] = 50;
	#endif
	return fd;
}

static void common_setup(SDL_Window *w)
{
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
}

#ifdef ET
int __stdcall update_gaze(struct SampleStruct sampleData)
{
	/*
	int mean_x, mean_y;
	mean_x = (sampleData.leftEye.gazeX + sampleData.rightEye.gazeX) / 2;
	mean_y = (sampleData.leftEye.gazeY + sampleData.rightEye.gazeY) / 2;
	*/
	double x, y, z; //mean eye coordinates for distance
	double theta;

	SDL_LockMutex(gs->mutex);
	gs->left.x = sampleData.leftEye.eyePositionX;
	gs->left.y = sampleData.leftEye.eyePositionY;
	gs->left.z = sampleData.leftEye.eyePositionZ;
	gs->right.x = sampleData.rightEye.eyePositionX;
	gs->right.y = sampleData.rightEye.eyePositionY;
	gs->right.z = sampleData.rightEye.eyePositionZ;

	gs->left.diam = sampleData.leftEye.diam;
	gs->right.diam = sampleData.rightEye.diam;

	/* mean eye coordinates in 3d space */
	x = (gs->left.x + gs->right.x) / 2;
	y = (gs->left.y + gs->right.y) / 2;
	z = (gs->left.z + gs->right.z) / 2;

	theta = ls->camera_inclination;


	//FIXME: Need to know the sign of these coordinate systems

	//shift + rotate to map camera coordinates to screen-specific 3d coordinates
	//calculate screen pixel coordinates in 3d space
	//calculate distance

	//gs->distance =

	gs->x = mean_x;
	gs->y = mean_y;
	//gs->distance = sampleData.leftEye.

	//SDL_UnlockMutex(gs->mutex);
	return 0;
}

void setup_ivx(SDL_Window *w)
{
	common_setup(w);

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
		printf("Successfully connected to SMI Server\n");
		break;
	case ERR_COULD_NOT_CONNECT:
		printf("Error: Could not connect to SMI Server\n");
		break;
	default:
		printf("Error: iV_Connect() returned: %d\n", ret_connect);
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
	strcpy_s(calibrationData.targetFilename, 256, "");

	iV_SetupCalibration(&calibrationData);
	// start calibration
	ret_calibrate = iV_Calibrate();

	gaze = malloc(sizeof(gaze_struct));
	if (!gaze)
		pexit("malloc failed");

	iV_SetSampleCallback(update_gaze);
}
#else

void setup_ivx(SDL_Window *w)
{
	common_setup(w);
}

/*
int update_gaze(struct SampleStruct sampleData)
{
	int screen_x, screen_y;
{
	if (!gaze)
		gaze = malloc(sizeof(gaze_struct))
		pexit("gaze struct not initialized")

	screen_x = (sampleData.leftEye.gazeX + sampleData.rightEye.gazeX) / 2;
	screen_y = (sampleData.leftEye.gazeY + sampleData.rightEye.gazeY) / 2;

	SDL_LockMutex(gaze->mutex);
	gaze->

	SDL_UnlockMutex(gaze->mutex);

	return 0;
}
*/

#endif
