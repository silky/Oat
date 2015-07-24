//******************************************************************************
//* File:   oat posifilt main.cpp
//* Author: Jon Newman <jpnewman snail mit dot edu>
//
//* Copyright (c) Jon Newman (jpnewman snail mit dot edu) 
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
//****************************************************************************

#include "OatConfig.h" // Generated by CMake

#include <unordered_map>
#include <csignal>
#include <boost/program_options.hpp>

#include "../../lib/utility/IOFormat.h"
#include "../../lib/cpptoml/cpptoml.h"

#include "KalmanFilter2D.h"
#include "HomographyTransform2D.h"
#include "RegionFilter2D.h"

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

void printUsage(po::options_description options) {
    std::cout << "Usage: posifilt [INFO]\n"
              << "   or: posifilt TYPE SOURCE SINK [CONFIGURATION]\n"
              << "Filter positions from SOURCE and published filtered positions "
              << "to SINK.\n\n"
              << "TYPE\n"
              << "  kalman: Kalman filter\n"
              << "  homo: homography transform\n"
              << "  region: position region annotation\n\n"
              << "SOURCE:\n"
              << "  User-supplied name of the memory segment to receive " 
              << "positions from (e.g. rpos).\n\n"
              << "SINK:\n"
              << "  User-supplied name of the memory segment to publish "
              << "positions to (e.g. rpos).\n\n"
              << options << "\n";
}

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int s) {
    quit = 1;
}

// Processing loop
void run(std::shared_ptr<PositionFilter> positionFilter) {

    while (!quit && !source_eof) {
        source_eof = positionFilter->process();
    }
}

int main(int argc, char *argv[]) {
    
    std::signal(SIGINT, sigHandler);

    // The image source to which the viewer will be attached
    std::string type;
    std::string source;
    std::string sink;
    std::string config_file;
    std::string config_key;
    bool config_used = false;
    po::options_description visible_options("OPTIONS");

    std::unordered_map<std::string, char> type_hash;
    type_hash["kalman"] = 'a';
    type_hash["homo"] = 'b';
    type_hash["region"] = 'c';

    try {
        
        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;

        po::options_description config("CONFIGURATION");
        config.add_options()
                ("config-file,c", po::value<std::string>(&config_file), "Configuration file.")
                ("config-key,k", po::value<std::string>(&config_key), "Configuration key.")
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), "Filter TYPE.")
                ("positionsource", po::value<std::string>(&source),
                "The name of the server that supplies object position information."
                "The server must be of type SMServer<Position>\n")
                ("sink", po::value<std::string>(&sink),
                "The name of the sink to which filtered positions will be published."
                "The server must be of type SMServer<Position>\n")
                ;

        po::positional_options_description positional_options;
        positional_options.add("type", 1);
        positional_options.add("positionsource", 1);
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
            std::cout << "Oat Position Filter version "
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
            std::cout <<  oat::Error("A TYPE must be specified.\n");
            return -1;
        }

        if (!variable_map.count("positionsource")) {
            printUsage(visible_options);
            std::cout <<  oat::Error("A position SOURCE must be specified.\n");
            return -1;
        }

        if (!variable_map.count("sink")) {
            printUsage(visible_options);
            std::cout <<  oat::Error("A position SINK must be specified.\n");
            return -1;
        }
        
        if (!variable_map.count("config-file") && type.compare("homo") == 0) {
            printUsage(visible_options);
            std::cerr << oat::Error("When TYPE=homo, a configuration file must be specified"
                         " to provide homography matrix.\n");
            return -1;
        }
        
        if ((variable_map.count("config-file") && !variable_map.count("config-key")) ||
                (!variable_map.count("config-file") && variable_map.count("config-key"))) {
            printUsage(visible_options);
            std::cerr << oat::Error("A configuration file must be supplied with a corresponding config-key.\n");
            return -1;
        } else if (variable_map.count("config-file")) {
            config_used = true;
        }


    } catch (std::exception& e) {
        std::cerr << oat::Error(e.what()) << "\n";
        return -1;
    } catch (...) {
        std::cerr << oat::Error("Exception of unknown type.\n");
        return -1;
    }


    // Create component
    std::shared_ptr<PositionFilter> filter;
    
    try {
        
        // Refine component type
        switch (type_hash[type]) {
            case 'a':
            {
                filter = std::make_shared<KalmanFilter2D>(source, sink);
                break;
            }
            case 'b':
            {
                filter = std::make_shared<HomographyTransform2D>(source, sink);
                break;
            }
            case 'c':
            {
                filter = std::make_shared<RegionFilter2D>(source, sink);
                break;
            }
            default:
            {
                printUsage(visible_options);
                std::cerr << oat::Error("Invalid TYPE specified.\n");
                return -1;
            }
        }

        if (config_used)
            filter->configure(config_file, config_key);

        // Tell user
        std::cout << oat::whoMessage(filter->get_name(),
                "Listening to source " + oat::sourceText(source) + ".\n")
                << oat::whoMessage(filter->get_name(),
                "Steaming to sink " + oat::sinkText(sink) + ".\n")
                << oat::whoMessage(filter->get_name(),
                "Press CTRL+C to exit.\n");

        // Infinite loop until ctrl-c or server end-of-stream signal
        run(filter);

        // Tell user
        std::cout << oat::whoMessage(filter->get_name(), "Exiting.\n");

        // Exit
        return 0;

    } catch (const cpptoml::parse_exception& ex) {
        std::cerr << oat::whoError(filter->get_name(), "Failed to parse configuration file " + config_file + "\n")
                  << oat::whoError(filter->get_name(), ex.what())
                  << "\n";
    } catch (const std::runtime_error& ex) {
        std::cerr << oat::whoError(filter->get_name(),ex.what())
                  << "\n";
    } catch (const cv::Exception& ex) {
        std::cerr << oat::whoError(filter->get_name(), ex.what())
                  << "\n";
    } catch (...) {
        std::cerr << oat::whoError(filter->get_name(), "Unknown exception.\n");
    }

    // Exit failure
    return -1; 
    
}
