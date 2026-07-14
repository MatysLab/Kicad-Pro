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

#include <high_speed/field_solver_adapter.h>

#include <set>


namespace KICAD::HIGH_SPEED
{

std::vector<std::string> FIELD_SOLVER_JOB::Validate() const
{
    std::vector<std::string> errors;

    if( schemaVersion != 1 )
        errors.emplace_back( "Unsupported field-solver job schema version" );

    if( jobId.empty() )
        errors.emplace_back( "Field-solver job ID is empty" );

    if( geometryFormat.empty() )
        errors.emplace_back( "Field-solver geometry format is empty" );

    if( geometryPath.empty() )
        errors.emplace_back( "Field-solver geometry path is empty" );

    if( workingDirectory.empty() )
        errors.emplace_back( "Field-solver working directory is empty" );

    if( geometryHash.empty() )
        errors.emplace_back( "Field-solver geometry hash is empty" );

    if( startFrequencyHz < 0.0 || stopFrequencyHz <= startFrequencyHz )
        errors.emplace_back( "Field-solver frequency range is invalid" );

    if( frequencyPointCount < 2 )
        errors.emplace_back( "Field-solver jobs require at least two frequency points" );

    if( ports.empty() )
        errors.emplace_back( "Field-solver job has no ports" );

    std::set<std::string> portNames;

    for( const FIELD_SOLVER_PORT& port : ports )
    {
        if( port.name.empty() )
            errors.emplace_back( "Field-solver port name is empty" );
        else if( !portNames.insert( port.name ).second )
            errors.emplace_back( "Field-solver port names must be unique" );

        if( port.signalConductor.empty() )
            errors.emplace_back( "Field-solver port signal conductor is empty" );

        if( port.referenceConductor.empty() )
            errors.emplace_back( "Field-solver port reference conductor is empty" );

        if( port.referenceImpedanceOhm <= 0.0 )
            errors.emplace_back( "Field-solver port reference impedance must be positive" );
    }

    return errors;
}

} // namespace KICAD::HIGH_SPEED
