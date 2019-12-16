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

#ifdef ET
int __stdcall update_gaze(struct SampleStruct sampleData)
{
	int screen_x, screen_y;


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

void setup_ivx(void)
{

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

#endif
