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

#include <unordered_map>
#include <signal.h>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>

#include "FrameFilter.h"
#include "BackgroundSubtractor.h"
#include "FrameMasker.h"

namespace po = boost::program_options;

volatile sig_atomic_t done = 0;

void run(FrameFilter* filter, std::string source, std::string sink) {

    std::cout << "Frame filter has begun listening to source \"" + source + "\".\n";
    std::cout << "Frame filter has begun steaming to sink \"" + sink + "\".\n";

    while (!done) {
        filter->filterAndServe();
    }

    std::cout << "Frame filter is exiting.\n";
}

void printUsage(po::options_description options){
    std::cout << "Usage: framefilt [OPTIONS]\n"
              << "   or: framefilt TYPE SOURCE SINK [CONFIG]\n"
              << "Perform background subtraction on images from SOURCE.\n"
              << "Publish background-subtracted images to SMServer<SharedCVMatHeader> SINK.\n\n"
              << "TYPE\n"
              << "  \'bsub\': Background subtraction\n"
              << "  \'mask\': Binary mask\n\n"
              << options << "\n";
}

int main(int argc, char *argv[]) {

    std::string type;
    std::string source;
    std::string sink;
    std::string config_file;
    std::string config_key;
    bool config_used = false;
    bool invert_mask = false;
    po::options_description visible_options("OPTIONS");
    
    std::unordered_map<std::string, char> type_hash;
    type_hash["bsub"] = 'a';
    type_hash["mask"] = 'b';

    try {

        po::options_description options("META");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;
        
        po::options_description config("CONFIGURATION");
        config.add_options()
                ("config-file,c", po::value<std::string>(&config_file), "Configuration file.")
                ("config-key,k", po::value<std::string>(&config_key), "Configuration key.")
                ("invert-mask,m", "If using TYPE=mask, invert the mask before applying")
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), 
                "Type of frame filter to use.\n")
                ("source", po::value<std::string>(&source),
                "The name of the SOURCE that supplies images on which to perform background subtraction."
                "The server must be of type SMServer<SharedCVMatHeader>\n")
                ("sink", po::value<std::string>(&sink),
                "The name of the SINK to which background subtracted images will be served.")
                ;

        po::positional_options_description positional_options;
         positional_options.add("type", 1);
        positional_options.add("source", 1);
        positional_options.add("sink", 1);
        
        visible_options.add(options).add(config);

        po::options_description all_options("All options");
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
            std::cout << "Simple-Tracker Background Subtractor version 1.0\n"; //TODO: Cmake managed versioning
            std::cout << "Written by Jonathan P. Newman in the MWL@MIT.\n";
            std::cout << "Licensed under the GPL3.0.\n";
            return 0;
        }

        if (!variable_map.count("source")) {
            printUsage(visible_options);
            std::cout << "Error: a SOURCE must be specified. Exiting.\n";
            return -1;
        }

        if (!variable_map.count("sink")) {
            printUsage(visible_options);
            std::cout << "Error: a SINK name must be specified. Exiting.\n";
            return -1;
        }
        
        
        if (variable_map.count("invert-mask")) {

            if (type_hash[type] != 'b') {
                std::cout << "Warning: invert-mask was requested, but this is the wrong filter TYPE for that option.\n "
                        << "invert-mask option was ignored.\n";
            } else {
                invert_mask = true;
            }
        }
        
        
        if ((variable_map.count("config-file") && !variable_map.count("config-key")) ||
                (!variable_map.count("config-file") && variable_map.count("config-key"))) {
            printUsage(visible_options);
            std::cout << "Error: config file must be supplied with a corresponding config-key. Exiting.\n";
            return -1;
        } else if (variable_map.count("config-file")) {
            config_used = true;
        }


    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Exception of unknown type! " << std::endl;
    }

    FrameFilter* filter; //(ant_source, pos_source, sink);
    switch (type_hash[type]) {
        case 'a':
        {
            filter = new BackgroundSubtractor(source, sink);
            break;
        }
        case 'b':
        {
            filter = new FrameMasker(source, sink, invert_mask);
            break;
        }
        default:
        {
            printUsage(visible_options);
            std::cout << "Error: invalid TYPE specified. Exiting.\n";
            return -1;
        }
    }

    if (config_used)
        filter->configure(config_file, config_key);

    // Two threads - one for user interaction, the other
    // for executing the processor
    boost::thread_group thread_group;
    thread_group.create_thread(boost::bind(&run, filter, source, sink));
    sleep(1);
    
    // Start the user interface
    while (!done) {

        char user_input;

        std::cout << "Framefilt has begun listening to source \"" + source + "\".\n";
        std::cout << "Framefilt has begun steaming to sink \"" + sink + "\".\n\n";
        std::cout << "COMMANDS:\n";
        std::cout << "  x: Exit.\n";

        std::cin >> user_input;

        switch (user_input) {
            case 'x':
            {
                done = true;
                break;
            }
            default:
                std::cout << "Invalid selection. Try again.\n";
                break;
        }
    }

    // Join processing and UI threads
    thread_group.join_all();
    
    // Free heap memory allocated to filter
    delete filter;

    // Exit
    return 0;
}


