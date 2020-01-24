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

void increase_qp_offset(int stepsize)
{

}

void decrease_qp_offset(int stepsize)
{

}

static float qp_offset()
{
	return 20;
}

float *foveation_descriptor(int frame_width, int frame_height)
{
	float *fd;
	float x, y;
	int x_int, y_int;
	int win_x, win_y;
	int win_width, win_height;

	float frame_width_mm, frame_height_mm;

	SDL_GetWindowSize(win, &win_width, &win_height);
	SDL_GetWindowPosition(win, &win_x, &win_y);

	fd = malloc(4*sizeof(float));
	if (!fd)
		pexit("malloc failed");

	#ifdef ET
	SDL_LockMutex(gs->mutex);
	//gaze coordinates have their origin at the upper left screen corner, shift to upper left window corner
	x = (float) gs->gazeX_mean - win_x;
	y = (float) gs->gazeY_mean - win_y;
	SDL_UnlockMutex(gs->mutex);
	#else
	SDL_GetMouseState(&x_int, &y_int);
	//mouse coordinates have origin already at upper left window corner
	x = (float) x_int;
	y = (float) y_int;
	#endif
	//shift by border margins to make origin upper left frame corner
	x = x - ((win_width - frame_width) / 2);
	y = y - ((win_height - frame_height) / 2);
	//descriptor coordinates are relative in terms of frame width/height
	fd[0] = x / frame_width;
	fd[1] = y / frame_height;

	printf("frame_res_w: %d, frame_res_h: %d\n", frame_width, frame_height);
	frame_width_mm = ls->screen_width * (float) frame_width / ls->screen_res_w;
	frame_height_mm = ls->screen_height * (float) frame_height / ls->screen_res_h;

	/*
	 * we assume a distance of 650mm to the screen,
	 * then 2 * tan(2.5°) * 650 = 56.7mm is a reasonable choice for foveation diameter
	 */
	fd[2] = 56.7 / sqrt(pow(frame_width_mm, 2) + pow(frame_height_mm, 2));
	fd[3] = qp_offset();

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

	// distance towards eyetracker, not screen!!!
	gs->distance = sqrt(x*x + y*y + z*z);

	SDL_UnlockMutex(gs->mutex);

	return 0;
}
#endif

void set_ivx_window(SDL_Window *w)
{
	win = w;
}

void setup_ivx(enc_id id)
{

	// common setup for ET and non-ET applications
	gs = malloc(sizeof(gaze));
	if (!gs)
		pexit("malloc failed");

	ls = malloc(sizeof(lab_setup));
	if (!ls)
		pexit("malloc failed");

	// Hardcoded HP Z31x screen
	ls->screen_width = 698;
	ls->screen_height = 368;
	ls->screen_diam = sqrt(pow(ls->screen_width, 2) + pow(ls->screen_height, 2));
	ls->screen_res_w = 4096;
	ls->screen_res_h = 2160;

	//Hardcoded: T470s screen
	/*
	ls->screen_width = 310;
	ls->screen_height = 170;
	ls->screen_diam = sqrt(pow(ls->screen_width, 2) + pow(ls->screen_height, 2));
	ls->screen_res_w = 2560;
	ls->screen_res_h = 1440;
	*/

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
	struct REDGeometryStruct geometry;

	int ret_calibrate = 0;
	int ret_validate = 0;
	int ret_connect = 0;

	char *et_host = "134.34.231.186";

	ret_connect = iV_Connect(et_host, 4444, "127.0.0.1", 5555);

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

	geometry.redGeometry = standalone; //mode
	geometry.stimX = ls->screen_width;
	geometry.stimY = ls->screen_height;
	geometry.redInclAngle = 20;  //degrees
	geometry.redStimDistHeight = 25; //mm
	geometry.redStimDistDepth =  80; //mm
	strncpy(geometry.setupName, "HPZ31x", 256);

	iV_SetREDGeometry(&geometry);

	iV_ShowEyeImageMonitor();
	iV_ShowTrackingMonitor();
	SDL_Delay(3000);

	// Eyetracker calibration
	calibrationData.method = 3;
	calibrationData.speed = 0;
	calibrationData.displayDevice = 0;
	calibrationData.targetShape = 2;
	calibrationData.foregroundBrightness = 250;
	calibrationData.backgroundBrightness = 230;
	calibrationData.autoAccept = 2;
	calibrationData.targetSize = 30;
	calibrationData.visualization = 1;
	strncpy(calibrationData.targetFilename, "", 256);

	iV_SetupCalibration(&calibrationData);
	// start calibration
	ret_calibrate = iV_Calibrate();

	iV_SetSampleCallback(update_gaze);
	#endif
}
