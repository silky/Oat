//******************************************************************************
//* File:   PositionGenerator.h
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
//*****************************************************************************

#ifndef OAT_POSITIONGENERATOR_H
#define	OAT_POSITIONGENERATOR_H

#include <chrono>
#include <string>
#include <random>
#include <opencv2/core/mat.hpp>

#include "../../lib/datatypes/Position2D.h"
#include "../../lib/shmemdf/Sink.h"

namespace oat {

/**
 * Abstract test position server.
 * All concrete test position server types implement this ABC.
 */
template <class T>
class PositionGenerator  {

public:

    /**
     * Abstract test position server.
     * All concrete test position server types implement this ABC and can be used
     * to server test positions with different motion characteristics to test
     * subsequent processing steps.
     * @param position_sink_name Position SINK to publish test positions
     * @param samples_per_second Sample rate in Hz
     */
    PositionGenerator(const std::string& position_sink_address,
                      const double samples_per_second = 30);

    virtual ~PositionGenerator() { }

    /**
     * PositionDetectors must be able to connect to a Source and Sink
     * Nodes in shared memory
     */
    virtual void connectToNode(void);

    /**
     * Generate test position. Publish test position to SINK.
     * @return End-of-stream signal. If true, this component should exit.
     */
    bool process(void);

    /**
     * Configure test position server parameters.
     * @param config_file configuration file path
     * @param config_key configuration key
     */
    virtual void configure(const std::string &file_name,
                           const std::string &key) = 0;

    /**
     * Get test position server name.
     * @return name
     */
    std::string name(void) const { return name_; }

protected:

    /**
     * Generate test position.
     * @param position Generated position.
     */
    virtual void generatePosition(T &position) = 0;

    // Test position sample clock
    std::chrono::high_resolution_clock clock;
    std::chrono::duration<double> sample_period_in_sec_;
    std::chrono::high_resolution_clock::time_point tick;

    /**
     * Configure the sample period
     * @param samples_per_second Sample period in seconds.
     */
    virtual void generateSamplePeriod(const double samples_per_second);

private:

    // Test position name
    std::string name_;

    // Internally generated position
    T internal_position_ {"internal"};

    // Shared position
    T * shared_position_;

    // The test position SINK
    std::string position_sink_address_;
    oat::Sink<T> position_sink_;

};

// Explicit declaration
//template class PositionGenerator<oat::Position2D>;

}      /* namespace oat */
#endif /* OAT_POSITIONGENERATOR_H */

