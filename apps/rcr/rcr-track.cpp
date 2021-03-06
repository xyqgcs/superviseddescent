/*
 * superviseddescent: A C++11 implementation of the supervised descent
 *                    optimisation method
 * File: apps/rcr/rcr-track.cpp
 *
 * Copyright 2015 Patrik Huber
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "superviseddescent/superviseddescent.hpp"
#include "superviseddescent/regressors.hpp"

#include "rcr/landmarks_io.hpp"
#include "rcr/model.hpp"

#include "cereal/cereal.hpp"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/objdetect/objdetect.hpp"

#include "boost/program_options.hpp"
#include "boost/filesystem.hpp"

#include <vector>
#include <iostream>
#include <fstream>

using namespace superviseddescent;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
using cv::Mat;
using std::vector;
using std::cout;
using std::endl;

template<class T = int>
cv::Rect_<T> get_enclosing_bbox(cv::Mat landmarks)
{
	auto num_landmarks = landmarks.cols / 2;
	double min_x_val, max_x_val, min_y_val, max_y_val;
	cv::minMaxLoc(landmarks.colRange(0, num_landmarks), &min_x_val, &max_x_val);
	cv::minMaxLoc(landmarks.colRange(num_landmarks, landmarks.cols), &min_y_val, &max_y_val);
	return cv::Rect_<T>(min_x_val, min_y_val, max_x_val - min_x_val, max_y_val - min_y_val);
};

/**
 * This app builds upon the robust cascaded regression landmark detection from
 * "Random Cascaded-Regression Copse for Robust Facial Landmark Detection", 
 * Z. Feng, P. Huber, J. Kittler, W. Christmas, X.J. Wu,
 * IEEE Signal Processing Letters, Vol:22(1), 2015.
 * It modifies the approach to track a face in a video.
 *
 * It loads a model trained with rcr-train, detects a face using OpenCV's face
 * detector, and then runs the landmark detection in each frame separately.
 */
int main(int argc, char *argv[])
{
	fs::path facedetector, inputimage, modelfile;
	try {
		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h",
				"display the help message")
			("facedetector,f", po::value<fs::path>(&facedetector)->required(),
				"full path to OpenCV's face detector (haarcascade_frontalface_alt2.xml)")
			("model,m", po::value<fs::path>(&modelfile)->required()->default_value("data/rcr/face_landmarks_model_rcr_22.bin"),
				"learned landmark detection model")
			("image,i", po::value<fs::path>(&inputimage),
				"input video file. If not specified, camera 0 will be used.")
			;
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
		if (vm.count("help")) {
			cout << "Usage: rcr-track [options]" << endl;
			cout << desc;
			return EXIT_SUCCESS;
		}
		po::notify(vm);
	}
	catch (const po::error& e) {
		cout << "Error while parsing command-line arguments: " << e.what() << endl;
		cout << "Use --help to display a list of options." << endl;
		return EXIT_SUCCESS;
	}

	rcr::detection_model rcr_model;

	// Load the learned model:
	try {
		rcr_model = rcr::load_detection_model(modelfile.string());
	}
	catch (const cereal::Exception& e) {
		cout << "Error reading the RCR model " << modelfile << ": " << e.what() << endl;
		return EXIT_FAILURE;
	}
	
	// Load the face detector from OpenCV:
	cv::CascadeClassifier face_cascade;
	if (!face_cascade.load(facedetector.string()))
	{
		cout << "Error loading the face detector " << facedetector << "." << endl;
		return EXIT_FAILURE;
	}

	cv::VideoCapture cap;
	if (inputimage.empty()) {
		cap.open(0); // no file given, open the default camera
	}
	else {
		cap.open(inputimage.string());
	}
	if (!cap.isOpened()) {  // check if we succeeded
		cout << "Couldn't open the given file or camera 0." << endl;
		return EXIT_FAILURE;
	}

	cv::namedWindow("video", 1);
	Mat image;
	using namespace std::chrono;
	time_point<system_clock> start, end;

	bool have_face = false;
	rcr::LandmarkCollection<cv::Vec2f> current_landmarks;

	for (;;)
	{
		cap >> image; // get a new frame from camera
		if (image.empty()) { // stop if we're at the end of the video
			break;
		}
		
		// Note: For now, we'll just run the face detector each frame.
		if (!have_face) {
			// Run the face detector and obtain the initial estimate using the mean landmarks:
			start = system_clock::now();
			vector<cv::Rect> detected_faces;
			face_cascade.detectMultiScale(image, detected_faces, 1.2, 2, 0, cv::Size(50, 50));
			end = system_clock::now();
			auto t_fd = duration_cast<milliseconds>(end - start).count();
			if (detected_faces.empty()) {
				cv::imshow("video", image);
				cv::waitKey(30);
				continue;
			}
			cv::rectangle(image, detected_faces[0], { 255, 0, 0 });
			
			start = system_clock::now();
			current_landmarks = rcr_model.detect(image, detected_faces[0]);
			end = system_clock::now();
			auto t_fit = duration_cast<milliseconds>(end - start).count();

			rcr::draw_landmarks(image, current_landmarks);
			//have_face = true;

			cout << "FD:" << t_fd << "\tLM: " << t_fit << endl;
		}
		else {
			// We already have a face. Do Zhenhua's magic:
			// current_landmarks are the estimate from the last frame
		/*	auto bbox = get_enclosing_bbox(current_landmarks);
			float scaling_x = get_enclosing_bbox(rcr_model.model_mean).width;
			float scaling_y = get_enclosing_bbox(rcr_model.model_mean).height;
			rcr::alignMean(current_landmarks, bbox, scaling_x, scaling_y);
		*/
			// have some condition to set have_face = false
		}

		cv::imshow("video", image);
		if (cv::waitKey(30) >= 0) break;
	}

	return EXIT_SUCCESS;
}
