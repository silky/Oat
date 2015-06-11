//******************************************************************************
//* Copyright (c) Jon Newman (jpnewman at mit snail edu) 
//* All right reserved.
//* This file is part of the Simple Tracker project.
//* This is free software: you can redistribute it and/or modify
//* it under the terms of the GNU General Public License as published by
//* the Free Software Foundation, either version 3 of the License, or
//* (at your option) any later version.
//* This software is distributed in the hope that it will be useful,
//* but WITHOUT ANY WARRANTY; without even the implied warranty of
//* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//* GNU General Public License for more details.
//* You should have received a copy of the GNU General Public License
//* along with this source code.  If not, see <http://www.gnu.org/licenses/>.
//******************************************************************************

#include "BackgroundSubtractor.h"

#include <string>
#include <iostream>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include "../../lib/cpptoml/cpptoml.h"

BackgroundSubtractor::BackgroundSubtractor(const std::string& source_name, const std::string& sink_name) :
  FrameFilter(source_name, sink_name) { }

void BackgroundSubtractor::configure(const std::string& config_file, const std::string& config_key) {
    
    cpptoml::table config;

    try {
        config = cpptoml::parse_file(config_file);
    } catch (const cpptoml::parse_exception& e) {
        std::cerr << "Failed to parse " << config_file << ": " << e.what() << std::endl;
    }

    try {
        // See if a camera configuration was provided
        if (config.contains(config_key)) {

            auto this_config = *config.get_table(config_key);

            std::string mask_path;
            if (this_config.contains("background")) {
                mask_path = *this_config.get_as<std::string>("background");
            }
            
            try {
                background_img = cv::imread(mask_path, CV_LOAD_IMAGE_GRAYSCALE);
                background_set = true;
            } catch (cv::Exception& e) {
                std::cout << "CV Exception: " << e.what() << "\n";
                std::cout << "The provided background image will not be used.\n"
                          << "Instead, the first frame captured will be used.\n";
                background_set = false;
            }

        } else {
            std::cerr << "No background subtraction configuration named \"" + config_key + "\" was provided. Exiting." << std::endl;
            exit(EXIT_FAILURE);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

/**
 * Set the background image to be used during subsequent subtraction operations.
 * The frame_source must have previously populated the the shared cv::Mat object.
 * 
 */
void BackgroundSubtractor::setBackgroundImage(const cv::Mat& frame) {

    background_img = frame.clone();
    background_set = true;
}
/**
 * Subtract a previously set background image from an input image to produce
 * the output matrix.
 * 
 */
void BackgroundSubtractor::filterAndServe() {
    
    // Only proceed with processing if we are getting a valid frame
    if (frame_source.getSharedMat(current_frame)) {

        if (background_set) {

            try {
                current_frame = current_frame - background_img;
            } catch (cv::Exception& e) {
                std::cout << "CV Exception: " << e.what() << "\n";
            }

        } else {

            // First image is always used as the default background image
            setBackgroundImage(current_frame);
        }

        // Push filtered frame forward, along with frame_source sample number
        frame_sink.pushMat(current_frame, frame_source.get_current_sample_number());
    }
}
