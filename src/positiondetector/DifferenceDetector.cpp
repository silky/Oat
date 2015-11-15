//******************************************************************************
//* File:   DifferenceDetector.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//*
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu)
//* All right reserved.
//* This file is part of the Oat project.
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
//****************************************************************************

#include "OatConfig.h" // Generated by CMake

#include <string>
#include <opencv2/opencv.hpp>
#include <cpptoml.h>

#include "../../lib/datatypes/Position2D.h"
#include "../../lib/utility/IOFormat.h"
#include "../../lib/utility/OatTOMLSanitize.h"

#include "DetectorFunc.h"
#include "DifferenceDetector.h"

namespace oat {

DifferenceDetector::DifferenceDetector(const std::string &frame_source_address,
                                           const std::string &position_sink_address) :
  PositionDetector(frame_source_address, position_sink_address)
, tuning_image_title_(position_sink_address + "_tuning")
{
    // Cannot use initializer because if this is set to 0, blur_on
    // must be set to false
    set_blur_size(2);
}

oat::Position2D DifferenceDetector::detectPosition(cv::Mat &frame) {

    if (tuning_on_)
        tune_frame_ = frame.clone();

    applyThreshold(frame);

    // Threshold frame will be destroyed by the transform below, so we need to use
    // it to form the frame that will be shown in the tuning window here
    if (tuning_on_)
         tune_frame_.setTo(0, threshold_frame_ == 0);

    siftContours(threshold_frame_, 
                 object_position_, 
                 object_area_,
                 min_object_area_, 
                 max_object_area_);

    tune(tune_frame_);

    return object_position_;
}

void DifferenceDetector::configure(const std::string& config_file,
                                     const std::string& config_key) {

    // Available options
    std::vector<std::string> options {"blur", 
                                      "diff_threshold", 
                                      "min_area",
                                      "max_area",
                                      "tune"};

    // This will throw cpptoml::parse_exception if a file
    // with invalid TOML is provided
   auto config = cpptoml::parse_file(config_file);

    // See if a camera configuration was provided
    if (config->contains(config_key)) {

        // Get this components configuration table
        auto this_config = config->get_table(config_key);

        // Check for unknown options in the table and throw if you find them
        oat::config::checkKeys(options, this_config);

        // Blur
        {
            int64_t val;
            oat::config::getValue(this_config, "blur", val, (int64_t)0);
            set_blur_size(val);
        }

        // Difference threshold
        {
            int64_t val;
            oat::config::getValue(this_config, "diff_threshold", val, (int64_t)0);
            difference_intensity_threshold_ = val;
        }

        // Minimum object area
        oat::config::getValue(this_config, "min_area", min_object_area_, 0.0);

        // Maximum object area
        oat::config::getValue(this_config, "max_area", max_object_area_, 0.0);

        // Tuning
        oat::config::getValue(this_config, "tune", tuning_on_);
        if (tuning_on_) {
            createTuningWindows();
        }

    } else {
        throw (std::runtime_error(oat::configNoTableError(config_key, config_file)));
    }

}

void DifferenceDetector::tune(cv::Mat &frame) {

    if (tuning_on_) {
        std::string msg = cv::format("Object not found");

        // Plot a circle representing found object
        if (object_position_.position_valid) {

            // TODO: object_area_ is not set, so this will be 0!
            auto radius = std::sqrt(object_area_ / PI);
            cv::Point center;
            center.x = object_position_.position.x;
            center.y = object_position_.position.y;
            cv::circle(frame, center, radius, cv::Scalar(0, 0, 255), 4);
            msg = cv::format("(%d, %d) pixels",
                    (int) object_position_.position.x,
                    (int) object_position_.position.y);
        }

        int baseline = 0;
        cv::Size textSize = cv::getTextSize(msg, 1, 1, 1, &baseline);
        cv::Point text_origin(
                frame.cols - textSize.width - 10,
                frame.rows - 2 * baseline - 10);

        cv::putText(frame, msg, text_origin, 1, 1, cv::Scalar(0, 255, 0));

        if (!tuning_windows_created_)
            createTuningWindows();

        cv::imshow(tuning_image_title_, frame);
        cv::waitKey(1);

    } else if (!tuning_on_ && tuning_windows_created_) {

        // TODO: Window will not actually close!!
        // Destroy the tuning windows
        cv::destroyWindow(tuning_image_title_);
        tuning_windows_created_ = false;
    }
}
//void DifferenceDetector::siftBlobs() {
//
//    cv::Mat thresh_cpy = threshold_frame_.clone();
//    std::vector< std::vector < cv::Point > > contours;
//    std::vector< cv::Vec4i > hierarchy;
//    cv::Rect objectBoundingRectangle;
//
//    //these two vectors needed for output of findContours
//    //find contours of filtered image using openCV findContours function
//    //findContours(temp,contours,hierarchy,CV_RETR_CCOMP,CV_CHAIN_APPROX_SIMPLE );// retrieves all contours
//    cv::findContours(thresh_cpy, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE); // retrieves external contours
//
//    //if contours vector is not empty, we have found some objects
//    if (contours.size() > 0) {
//        object_position_.position_valid = true;
//    } else
//        object_position_.position_valid = false;
//
//    if (object_position_.position_valid) {
//        
//        // TODO: This is wrong. Look at hsvdetector for efficient and correct
//        //       implementation
//        //the largest contour is found at the end of the contours vector
//        //we will simply assume that the biggest contour is the object we are looking for.
//        std::vector< std::vector<cv::Point> > largestContourVec;
//        largestContourVec.push_back(contours.at(contours.size() - 1));
//
//        //make a bounding rectangle around the largest contour then find its centroid
//        //this will be the object's final estimated position.
//        objectBoundingRectangle = cv::boundingRect(largestContourVec.at(0));
//        object_position_.position.x = objectBoundingRectangle.x + 0.5 * objectBoundingRectangle.width;
//        object_position_.position.y = objectBoundingRectangle.y + 0.5 * objectBoundingRectangle.height;
//    }

//void DifferenceDetector::tune(cv::Mat &frame) {
//
//    if (tuning_on_) {
//
//        std::string msg = cv::format("Object not found"); 
//
//        // Plot a circle representing found object
//        if (object_position_.position_valid) {
//            cv::cvtColor(threshold_frame_, threshold_frame_, cv::COLOR_GRAY2BGR);
//            cv::rectangle(threshold_frame_, objectBoundingRectangle.tl(), objectBoundingRectangle.br(), cv::Scalar(0, 0, 255), 2);
//            msg = cv::format("(%d, %d) pixels", (int) object_position_.position.x, (int) object_position_.position.y);
//
//        }
//
//        int baseline = 0;
//        cv::Size textSize = cv::getTextSize(msg, 1, 1, 1, &baseline);
//        cv::Point text_origin(
//                threshold_frame_.cols - textSize.width - 10,
//                threshold_frame_.rows - 2 * baseline - 10);
//
//        cv::putText(threshold_frame_, msg, text_origin, 1, 1, cv::Scalar(0, 255, 0));
//    }
//}

void DifferenceDetector::applyThreshold(cv::Mat &frame) {

    if (last_image_set_) {
        cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY);
        cv::absdiff(frame, last_image_, threshold_frame_);
        cv::threshold(threshold_frame_, threshold_frame_, difference_intensity_threshold_, 255, cv::THRESH_BINARY);
        if (blur_on_) {
            cv::blur(threshold_frame_, threshold_frame_, blur_size_);
        }
        cv::threshold(threshold_frame_, threshold_frame_, difference_intensity_threshold_, 255, cv::THRESH_BINARY);
        last_image_ = frame.clone(); // Get a copy of the last image
    } else {
        threshold_frame_ = frame.clone();
        cv::cvtColor(threshold_frame_, threshold_frame_, cv::COLOR_BGR2GRAY);
        last_image_ = frame.clone();
        cv::cvtColor(last_image_, last_image_, cv::COLOR_BGR2GRAY);
        last_image_set_ = true;
    }
}

//void DifferenceDetector::tune() {
//
//    if (tuning_on_) {
//        if (!tuning_windows_created_) {
//            createTuningWindows();
//        }
//        cv::imshow(tuning_image_title_, threshold_frame_);
//        cv::waitKey(1);
//
//    } else if (!tuning_on_ && tuning_windows_created_) {
//
//        // TODO: Window will not actually close!!
//
//        // Destroy the tuning windows
//        cv::destroyWindow(tuning_image_title_);
//        tuning_windows_created_ = false;
//    }
//}

void DifferenceDetector::createTuningWindows() {

#ifdef OAT_USE_OPENGL
    try {
        cv::namedWindow(tuning_image_title_, cv::WINDOW_OPENGL & cv::WINDOW_KEEPRATIO);
    } catch (cv::Exception& ex) {
        whoWarn(name_, "OpenCV not compiled with OpenGL support. Falling back to OpenCV's display driver.\n");
        cv::namedWindow(tuning_image_title_, cv::WINDOW_NORMAL & cv::WINDOW_KEEPRATIO);
    }
#else
    cv::namedWindow(tuning_image_title_, cv::WINDOW_NORMAL);
#endif

    // Create sliders and insert them into window
    cv::createTrackbar("THRESH", tuning_image_title_, 
            &difference_intensity_threshold_, 256);
    cv::createTrackbar("BLUR", tuning_image_title_, 
            &blur_size_.height, 50, &diffDetectorBlurSliderChangedCallback, this);
    cv::createTrackbar("MIN AREA", tuning_image_title_, 
            &dummy0_, 10000, &diffDetectorMinAreaSliderChangedCallback, this);
    cv::createTrackbar("MAX AREA", tuning_image_title_, 
            &dummy1_, 10000, &diffDetectorMaxAreaSliderChangedCallback, this);
    tuning_windows_created_ = true;
}

void DifferenceDetector::set_blur_size(int value) {

    if (value > 0) {
        blur_on_ = true;
        blur_size_ = cv::Size(value, value);
    } else {
        blur_on_ = false;
    }
}

// Non-member GUI callback functions
void diffDetectorBlurSliderChangedCallback(int value, void * object) {
    auto diff_detector = static_cast<DifferenceDetector *>(object);
    diff_detector->set_blur_size(value);
}

void diffDetectorMinAreaSliderChangedCallback(int value, void * object) {
    auto diff_detector = static_cast<DifferenceDetector *>(object);
    diff_detector->set_min_object_area(static_cast<double>(value));
}

void diffDetectorMaxAreaSliderChangedCallback(int value, void * object) {
    auto diff_detector = static_cast<DifferenceDetector *>(object);
    diff_detector->set_max_object_area(static_cast<double>(value));
}

} /* namespace oat */
