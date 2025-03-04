//******************************************************************************
//* File:   oat frameserve main.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//
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

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <cpptoml.h>

#include "../../lib/utility/IOFormat.h"

#include "TestFrame.h"
#include "FileReader.h"
#include "WebCam.h"
#ifdef USE_FLYCAP
    #include "PGGigECam.h"
#endif

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

void printUsage(po::options_description options) {
    std::cout << "Usage: frameserve [INFO]\n"
              << "   or: frameserve TYPE SINK [CONFIGURATION]\n"
              << "Serve image stream to a frame SINK\n\n"
              << "TYPE:\n"
              << "  wcam: Onboard or USB webcam.\n"
              << "  gige: Point Grey GigE camera.\n"
              << "  file: Video from file (*.mpg, *.avi, etc.).\n"
              << "  test: Write-free static image server for performance testing.\n\n"
              << "SINK:\n"
              << "  User-supplied name of the memory segment to publish frames "
              << "to (e.g. raw).\n\n"
              << options << "\n";
}

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int) {
    quit = 1;
}

void run(const std::shared_ptr<oat::FrameServer>& server) {

    try {

        server->connectToNode();

        while (!quit && !source_eof) {
            source_eof = server->serveFrame();
        }

    } catch (const boost::interprocess::interprocess_exception &ex) {

        // Error code 1 indicates a SIGNINT during a call to wait(), which
        // is normal behavior
        if (ex.get_error_code() != 1)
            throw;
    }
}

int main(int argc, char *argv[]) {

    std::signal(SIGINT, sigHandler);

    std::string sink;
    std::string type;
    std::string file_path;
    double frames_per_second = 30;
    size_t index = 0;
    std::vector<std::string> config_fk;
    bool config_used = false;
    po::options_description visible_options("OPTIONAL ARGUMENTS");

    std::unordered_map<std::string, char> type_hash;
    type_hash["wcam"] = 'a';
    type_hash["gige"] = 'b';
    type_hash["file"] = 'c';
    type_hash["test"] = 'd';

    try {

        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;

        po::options_description config("CONFIGURATION");
        config.add_options()
                ("index,i", po::value<size_t>(&index),
                "Index of camera to capture images from.")
                ("file,f", po::value<std::string>(&file_path),
                "Path to video file if \'file\' is selected as the server TYPE.\n"
                "Path to image file if \'test\' is selected as the server TYPE.")
                ("fps,r", po::value<double>(&frames_per_second),
                "Frames per second. Overriden by information in configuration file if provided.")
                ("config,c", po::value<std::vector<std::string> >()->multitoken(),
                "Configuration file/key pair.")
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), "Camera TYPE.")
                ("sink", po::value<std::string>(&sink),
                "The name of the sink through which images collected by the camera will be served.\n")
                ;

        po::positional_options_description positional_options;
        positional_options.add("type", 1);
        positional_options.add("sink", 1);

        visible_options.add(options).add(config);

        po::options_description all_options("ALL OPTIONS");
        all_options.add(options).add(config).add(hidden);

        po::variables_map variable_map;
        po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .positional(positional_options)
                .run(),
                variable_map);
        po::notify(variable_map);

        // Use the parsed options
        if (variable_map.count("help")) {
            printUsage(visible_options);
            return 0;
        }

        if (variable_map.count("version")) {
            std::cout << "Oat Frame Server version "
                      << Oat_VERSION_MAJOR
                      << "."
                      << Oat_VERSION_MINOR
                      << "\n";
            std::cout << "Written by Jonathan P. Newman in the MWL@MIT.\n";
            std::cout << "Licensed under the GPL3.0.\n";
            return 0;
        }

        if (!variable_map.count("type")) {
            printUsage(visible_options);
            std::cout << oat::Error("A TYPE must be specified. Exiting.\n");
            return -1;
        }

        if (!variable_map.count("sink")) {
            printUsage(visible_options);
            std::cout << oat::Error("A SINK must be specified. Exiting.\n");
            return -1;
        }

        if (!variable_map["config"].empty()) {

            config_fk = variable_map["config"].as<std::vector<std::string> >();

            if (config_fk.size() == 2) {
                config_used = true;
            } else {
                printUsage(visible_options);
                std::cerr << oat::Error("Configuration must be supplied as file key pair.\n");
                return -1;
            }
        }

        if ((type.compare("file") == 0 || type.compare("test") == 0 )
            && !variable_map.count("file")) {
            printUsage(visible_options);
            std::cout << oat::Error("When TYPE=file or test, a file path must be specified. Exiting.\n");
            return -1;
        }

    } catch (std::exception& e) {
        std::cerr << oat::Error(e.what()) << "\n";
        return 1;
    } catch (...) {
        std::cerr << oat::Error("Exception of unknown type.") << "\n";
    }

    // Create the specified TYPE of detector
    std::shared_ptr<oat::FrameServer> server;

    switch (type_hash[type]) {
        case 'a':
        {
            server = std::make_shared<oat::WebCam>(sink);
            break;
        }
        case 'b':
        {

#ifndef USE_FLYCAP
            std::cerr << oat::Error("Oat was not compiled with Point-Grey "
                    "flycapture support, so TYPE=gige is not available.\n");
            return -1;
#else
            server = 
                std::make_shared<oat::PGGigECam>(sink, index, frames_per_second);
#endif
            break;
        }
        case 'c':
        {
            server = 
                std::make_shared<oat::FileReader>(sink, file_path, frames_per_second);
            break;
        }
        case 'd':
        {
            server = std::make_shared<oat::TestFrame>(sink, file_path);
            break;
        }
        default:
        {
            printUsage(visible_options);
            std::cout << oat::Error("Invalid TYPE specified. Exiting.\n");
            return -1;
        }
    }

    // The business
    try {
        
        // TODO: For most of the server types, these methods don't do much or
        // nothing. Should they really be part of the FrameServer interface?
        if (config_used)
            server->configure(config_fk[0], config_fk[1]);
        else
            server->configure();


        // Tell user
        std::cout << oat::whoMessage(server->name(),
                  "Steaming to sink " + oat::sinkText(sink) + ".\n")
                  << oat::whoMessage(server->name(),
                  "Press CTRL+C to exit.\n");

        // Infinite loop until ctrl-c or end of stream signal
        run(server);

        // Tell user
        std::cout << oat::whoMessage(server->name(), "Exiting.\n");

        // Exit
        return 0;

    } catch (const cpptoml::parse_exception &ex) {
        std::cerr << oat::whoError(server->name(),
                     "Failed to parse configuration file " + config_fk[0] + "\n")
                  << oat::whoError(server->name(), ex.what()) << "\n";
    } catch (const std::runtime_error &ex) {
        std::cerr << oat::whoError(server->name(), ex.what()) << "\n";
    } catch (const cv::Exception &ex) {
        std::cerr << oat::whoError(server->name(), ex.what()) << "\n";
    } catch (const boost::interprocess::interprocess_exception &ex) {
        std::cerr << oat::whoError(server->name(), ex.what()) << "\n";
    } catch (...) {
        std::cerr << oat::whoError(server->name(), "Unknown exception.\n");
    }

    // Exit failure
    return -1;
}
