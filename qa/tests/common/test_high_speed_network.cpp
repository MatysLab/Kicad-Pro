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

#include <qa_utils/wx_utils/unit_test_utils.h>

#include <high_speed/field_solver_adapter.h>
#include <high_speed/nport_network.h>
#include <high_speed/touchstone_reader.h>

#include <cmath>
#include <sstream>
#include <stdexcept>


using namespace KICAD::HIGH_SPEED;


BOOST_AUTO_TEST_SUITE( HighSpeedNetwork )


namespace
{

void checkComplex( const NPORT_NETWORK::COMPLEX& aActual,
                   const NPORT_NETWORK::COMPLEX& aExpected, double aTolerance = 1.0e-12 )
{
    BOOST_CHECK_SMALL( std::abs( aActual - aExpected ), aTolerance );
}

} // namespace


BOOST_AUTO_TEST_CASE( NPortEnforcesFrequencyAndMatrixInvariants )
{
    NPORT_NETWORK network( 2, NETWORK_PARAMETER::SCATTERING, { 50.0, 75.0 } );
    network.AddSample( 1.0e9, { { 0.1, 0.0 }, { 0.2, 0.0 }, { 0.3, 0.0 }, { 0.4, 0.0 } } );

    BOOST_CHECK_EQUAL( network.PortCount(), 2 );
    BOOST_CHECK_EQUAL( network.FrequencyCount(), 1 );
    BOOST_CHECK_EQUAL( network.ReferenceImpedancesOhm()[1], 75.0 );
    checkComplex( network.Value( 0, 1, 0 ), { 0.3, 0.0 } );

    BOOST_CHECK_THROW( network.AddSample( 1.0e9, std::vector<NPORT_NETWORK::COMPLEX>( 4 ) ),
                       std::invalid_argument );
    BOOST_CHECK_THROW( network.AddSample( 2.0e9, std::vector<NPORT_NETWORK::COMPLEX>( 3 ) ),
                       std::invalid_argument );
}


BOOST_AUTO_TEST_CASE( ReadsVersion1TwoPortLegacyOrder )
{
    std::istringstream input( R"(
        ! N11, N21, N12, N22
        # GHz S RI R 50
        1.0  0.1 0.01  0.2 0.02  0.3 0.03  0.4 0.04
        2.0  0.5 0.05  0.6 0.06  0.7 0.07  0.8 0.08
    )" );

    const TOUCHSTONE_RESULT result = TOUCHSTONE_READER::Read( input, 2 );
    BOOST_REQUIRE( result.Ok() );
    BOOST_REQUIRE( result.network );

    const NPORT_NETWORK& network = *result.network;
    BOOST_CHECK_EQUAL( network.FrequenciesHz()[0], 1.0e9 );
    checkComplex( network.Value( 0, 0, 0 ), { 0.1, 0.01 } );
    checkComplex( network.Value( 0, 1, 0 ), { 0.2, 0.02 } );
    checkComplex( network.Value( 0, 0, 1 ), { 0.3, 0.03 } );
    checkComplex( network.Value( 0, 1, 1 ), { 0.4, 0.04 } );
}


BOOST_AUTO_TEST_CASE( ReadsVersion2NaturalTwoPortOrderAndPerPortReferences )
{
    std::istringstream input( R"(
        [Version] 2.1
        # MHz S MA R 50
        [Number of Ports] 2
        [Two-Port Data Order] 12_21
        [Number of Frequencies] 1
        [Reference] 40 60
        [Matrix Format] Full
        [Network Data]
        100  1 0  2 90  3 180  4 -90
        [End]
    )" );

    const TOUCHSTONE_RESULT result = TOUCHSTONE_READER::Read( input );
    BOOST_REQUIRE( result.Ok() );
    BOOST_REQUIRE( result.network );

    const NPORT_NETWORK& network = *result.network;
    BOOST_CHECK_EQUAL( network.ReferenceImpedancesOhm()[0], 40.0 );
    BOOST_CHECK_EQUAL( network.ReferenceImpedancesOhm()[1], 60.0 );
    checkComplex( network.Value( 0, 0, 0 ), { 1.0, 0.0 } );
    checkComplex( network.Value( 0, 0, 1 ), { 0.0, 2.0 } );
    checkComplex( network.Value( 0, 1, 0 ), { -3.0, 0.0 } );
    checkComplex( network.Value( 0, 1, 1 ), { 0.0, -4.0 } );
}


BOOST_AUTO_TEST_CASE( ExpandsVersion2LowerTriangularMatrix )
{
    std::istringstream input( R"(
        [Version] 2.0
        # Hz Z RI
        [Number of Ports] 3
        [Number of Frequencies] 1
        [Matrix Format] Lower
        [Network Data]
        10  11 0  21 0  22 0  31 0  32 0  33 0
        [End]
    )" );

    const TOUCHSTONE_RESULT result = TOUCHSTONE_READER::Read( input );
    BOOST_REQUIRE( result.Ok() );
    BOOST_REQUIRE( result.network );

    const NPORT_NETWORK& network = *result.network;
    checkComplex( network.Value( 0, 0, 1 ), { 21.0, 0.0 } );
    checkComplex( network.Value( 0, 1, 0 ), { 21.0, 0.0 } );
    checkComplex( network.Value( 0, 0, 2 ), { 31.0, 0.0 } );
    checkComplex( network.Value( 0, 2, 1 ), { 32.0, 0.0 } );
}


BOOST_AUTO_TEST_CASE( RejectsMixedModeUntilPortTransformIsImplemented )
{
    std::istringstream input( R"(
        [Version] 2.1
        # GHz S RI
        [Number of Ports] 2
        [Two-Port Data Order] 21_12
        [Number of Frequencies] 1
        [Mixed-Mode Order] D1,2 C1,2
        [Network Data]
        1  0 0  1 0  1 0  0 0
        [End]
    )" );

    const TOUCHSTONE_RESULT result = TOUCHSTONE_READER::Read( input );
    BOOST_CHECK( !result.Ok() );
    BOOST_CHECK( !result.network );
}


BOOST_AUTO_TEST_CASE( InfersConventionalPortCount )
{
    BOOST_CHECK_EQUAL( *TOUCHSTONE_READER::InferPortCount( "channel.S16P" ), 16 );
    BOOST_CHECK( !TOUCHSTONE_READER::InferPortCount( "channel.ts" ) );
    BOOST_CHECK( !TOUCHSTONE_READER::InferPortCount( "channel.s0p" ) );
}


BOOST_AUTO_TEST_CASE( ValidatesFieldSolverProcessBoundary )
{
    FIELD_SOLVER_JOB job;
    BOOST_CHECK( !job.Validate().empty() );

    job.jobId = "via-transition";
    job.geometryFormat = "kicad-high-speed-geometry-v1";
    job.geometryPath = "geometry.json";
    job.workingDirectory = "solver-cache/job";
    job.geometryHash = "sha256:test";
    job.startFrequencyHz = 1.0e6;
    job.stopFrequencyHz = 20.0e9;
    job.frequencyPointCount = 401;
    job.ports = { { "P1", "signal-in", "gnd", 50.0 },
                  { "P2", "signal-out", "gnd", 50.0 } };

    BOOST_CHECK( job.Validate().empty() );
}


BOOST_AUTO_TEST_SUITE_END()
