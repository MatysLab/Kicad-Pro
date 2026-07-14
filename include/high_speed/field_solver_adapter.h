/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef HIGH_SPEED_FIELD_SOLVER_ADAPTER_H
#define HIGH_SPEED_FIELD_SOLVER_ADAPTER_H

#include <kicommon.h>

#include <high_speed/nport_network.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>


namespace KICAD::HIGH_SPEED
{

/** Solver-independent port definition at a geometry reference plane. */
struct KICOMMON_API FIELD_SOLVER_PORT
{
    std::string name;
    std::string signalConductor;
    std::string referenceConductor;
    double      referenceImpedanceOhm = 50.0;
};


/**
 * Stable process-boundary job passed to a 2D or 3D field-solver adapter.
 *
 * The geometry payload is deliberately opaque to the common analysis library.  An exporter and
 * adapter negotiate a format such as a solver-neutral KiCad geometry manifest or CSXCAD XML.
 */
struct KICOMMON_API FIELD_SOLVER_JOB
{
    unsigned int          schemaVersion = 1;
    std::string           jobId;
    std::string           geometryFormat;
    std::filesystem::path geometryPath;
    std::filesystem::path workingDirectory;
    std::string           geometryHash;
    double                startFrequencyHz = 0.0;
    double                stopFrequencyHz = 0.0;
    std::size_t           frequencyPointCount = 0;
    std::vector<FIELD_SOLVER_PORT> ports;

    /** Return user-facing validation errors without inspecting solver-specific geometry. */
    std::vector<std::string> Validate() const;
};


struct KICOMMON_API FIELD_SOLVER_CAPABILITIES
{
    bool        available = false;
    std::string solverName;
    std::string solverVersion;
    std::string adapterVersion;
    bool        supports2D = false;
    bool        supports3D = false;
    bool        supportsFieldData = false;
    bool        supportsDispersiveMaterials = false;
};


enum class FIELD_SOLVER_STATUS
{
    COMPLETED,
    CANCELLED,
    INVALID_JOB,
    UNAVAILABLE,
    FAILED
};


struct KICOMMON_API FIELD_SOLVER_RESULT
{
    FIELD_SOLVER_STATUS          status = FIELD_SOLVER_STATUS::FAILED;
    std::optional<NPORT_NETWORK> network;
    std::vector<std::string>     messages;
    std::filesystem::path        fieldDataPath;
    std::string                  rawResultHash;
};


struct KICOMMON_API FIELD_SOLVER_PROGRESS
{
    double      fraction = 0.0;
    std::string stage;
    std::string message;
};


using FIELD_SOLVER_PROGRESS_CALLBACK = std::function<void( const FIELD_SOLVER_PROGRESS& )>;


/** Interface implemented by optional external solvers such as openEMS. */
class KICOMMON_API FIELD_SOLVER_ADAPTER
{
public:
    virtual ~FIELD_SOLVER_ADAPTER() = default;

    virtual std::string Id() const = 0;
    virtual FIELD_SOLVER_CAPABILITIES Probe() const = 0;

    /**
     * Run without access to live PCB objects.  Cancellation must terminate any child process and
     * leave the project untouched.
     */
    virtual FIELD_SOLVER_RESULT Run( const FIELD_SOLVER_JOB& aJob, std::stop_token aStopToken,
                                     FIELD_SOLVER_PROGRESS_CALLBACK aProgress ) = 0;
};

} // namespace KICAD::HIGH_SPEED

#endif // HIGH_SPEED_FIELD_SOLVER_ADAPTER_H
