//******************************************************************************
//* File:   oat posisock main.cpp
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
//******************************************************************************

#include "OatConfig.h" // Generated by CMake

#include <csignal>
#include <unordered_map>
#include <string>
#include <vector>
#include <boost/program_options.hpp>

#include "../../lib/utility/IOFormat.h"

#include "PositionSocket.h"
#include "PositionPublisher.h"
#include "PositionReplier.h"
#include "UDPPositionClient.h"
//#include "UDPPositionServer.h"

namespace po = boost::program_options;

volatile sig_atomic_t quit = 0;
volatile sig_atomic_t source_eof = 0;

void printUsage(po::options_description options) {
    std::cout << "Usage: posisock [INFO]\n"
              << "   or: posisock TYPE SOURCE ENDPOINT\n"
              << "Send positions from SOURCE to a remote endpoint.\n\n"
              << "TYPE:\n"
              << "  pub: Asynchronous position publisher over ZMQ socket.\n"
              << "       Publishes positions without request to potentially many\n"
              << "       subscribers.\n"
              << "  rep: Synchronous position replier over ZMQ socket. \n"
              << "       Sends positions in response to requests from a single\n"
              << "       endpoint.Several transport/protocol options. The most\n"
              << "       useful are tcp and interprocess (ipc).\n"
              << "  udp: Asynchronous, client-side, unicast user datagram protocol\n"
              << "       over a traditional BSD-style socket.\n\n"
              << "ENDPOINT:\n"
              << "Device to send positions to.\n"
              << "  When TYPE is pos or rep, this is specified using a ZMQ-style\n"
              << "  endpoint: '<transport>://<host>:<port>'. For instance, \n" 
              << "  'tcp://*:5555' or 'ipc://*:5556' specify TCP and interprocess\n"
              << "  communication on ports 5555 or 5556, respectively\n"
              << "  When TYPE is udp, this is specified as '<host> <port>'\n"
              << "  For instance, '10.0.0.1 5555'.\n\n"
              << options << "\n";
}

// Signal handler to ensure shared resources are cleaned on exit due to ctrl-c
void sigHandler(int) {
    quit = 1;
}

void run(std::shared_ptr<oat::PositionSocket> socket) {
    try {

        socket->connectToNode();

        while (!quit && !source_eof) {
            source_eof = socket->process();
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

    // The image source to which the viewer will be attached
    std::string type;
    std::string source;
    std::vector<std::string> endpoint;
//    bool server_side = false;
    po::options_description visible_options("OPTIONS");

    std::unordered_map<std::string, char> type_hash;
    type_hash["pub"] = 'a';
    type_hash["rep"] = 'b';
    type_hash["udp"] = 'c';

    try {

        po::options_description options("INFO");
        options.add_options()
                ("help", "Produce help message.")
                ("version,v", "Print version information.")
                ;

        //po::options_description config("CONFIGURATION");
        //config.add_options()
//                ("server", "Server-side socket sychronization. "
//                           "Position data packets are sent whenever requested "
//                           "by a remote client. TODO: explain request protocol...")
                //TODO: Serialization protocol (JSON, binary, etc)
                ;

        po::options_description hidden("HIDDEN OPTIONS");
        hidden.add_options()
                ("type", po::value<std::string>(&type), "Filter TYPE.")
                ("positionsource", po::value<std::string>(&source),
                "The name of the server that supplies object position information."
                "The server must be of type SMServer<Position>\n")
                ("endpoint,e", po::value<std::vector<std::string> >()->multitoken(),
                 "Endpoint to send positions to.\n")
                ;

        po::positional_options_description positional_options;
        positional_options.add("type", 1);
        positional_options.add("positionsource", 1);
        positional_options.add("endpoint", -1);

        visible_options.add(options);

        po::options_description all_options("ALL OPTIONS");
        all_options.add(options).add(hidden);

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
            std::cout << "Oat Position Server version "
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
            std::cerr << oat::Error("A position SOURCE must be specified.\n");
            return -1;
        }

        if (!variable_map["endpoint"].empty()) {

            endpoint = variable_map["endpoint"].as<std::vector<std::string> >();

            if (endpoint.size() > 2 || endpoint.size() < 1) {
                printUsage(visible_options);
                std::cerr << oat::Error("Endpoint was incorrectly formatted.\n");
                return -1;
            } else if (endpoint.size() != 2 && type == "udp") {
                printUsage(visible_options);
                std::cerr << oat::Error("udp endpoint must be specified as <host> <port>.\n");
                return -1;
            }
        } else {
            printUsage(visible_options);
            std::cerr << oat::Error("An endpoint must be specified.\n");
        }
//        if (variable_map.count("server")) {
//             server_side = true;
//        }
//
//        if (variable_map.count("endpoint") && server_side ) {
//            std::cerr << oat::Warn("Posisock role is server, but host address was specified. ")
//                      << oat::Warn("Host address " + endpio + " will be ignored.\n");
//        }

    } catch (std::exception& e) {
        std::cerr << oat::Error(e.what()) << "\n";
        return -1;
    } catch (...) {
        std::cerr << oat::Error("Exception of unknown type.\n");
        return -1;
    }

    // Create component
    std::shared_ptr<oat::PositionSocket> socket;

    try {

        // Refine component type
        switch (type_hash[type]) {

            case 'a':
            {
                socket = std::make_shared<oat::PositionPublisher>(source, endpoint[0]);
                break;
            }
            case 'b':
            {
                socket = std::make_shared<oat::PositionReplier>(source, endpoint[0]);
                break;
            }
            case 'c':
            {
//                if (server_side)
//                    socket = std::make_shared<oat::UDPPositionServer>(source, port);
//                else
                socket = std::make_shared<oat::UDPPositionClient>(source, endpoint[0], endpoint[1]);
                break;
            }
            default:
            {
                printUsage(visible_options);
                std::cerr << oat::Error("Invalid TYPE specified.\n");
                return -1;
            }
        }

        // Tell user
        std::cout << oat::whoMessage(socket->name(),
                "Listening to source " + oat::sourceText(source) + ".\n")
                << oat::whoMessage(socket->name(),
                "Press CTRL+C to exit.\n");


        // Infinite loop until ctrl-c or server end-of-stream signal
        run(socket);

        // Tell user
        std::cout << oat::whoMessage(socket->name(), "Exiting.\n");

        // Exit
        return 0;

    // TODO: for some reason, socket->name() is uninitialize when we reach
    // these catch statements, so this will segfault if we try to do
    // oat::whoError(socket->name(), ...)
    } catch (const std::runtime_error &ex) {
        std::cerr << oat::Error(ex.what()) << "\n";
    } catch (const zmq::error_t &ex) {
        std::cerr << oat::whoError("zeromq: ", ex.what()) << "\n";
    } catch (const cv::Exception &ex) {
        std::cerr << oat::Error(ex.what()) << "\n";
    } catch (const boost::interprocess::interprocess_exception &ex) {
        std::cerr << oat::Error(ex.what()) << "\n";
    } catch (...) {
        std::cerr << oat::Error("Unknown exception.\n");
    }

    // Exit failure
    return -1;
}
