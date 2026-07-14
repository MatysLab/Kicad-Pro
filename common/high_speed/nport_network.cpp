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

#include <high_speed/nport_network.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>


namespace KICAD::HIGH_SPEED
{

NPORT_NETWORK::NPORT_NETWORK( std::size_t aPortCount, NETWORK_PARAMETER aParameter,
                              std::vector<double> aReferenceImpedancesOhm ) :
        m_portCount( aPortCount ),
        m_parameter( aParameter ),
        m_referenceImpedancesOhm( std::move( aReferenceImpedancesOhm ) )
{
    if( m_portCount == 0 )
        throw std::invalid_argument( "An N-port network must contain at least one port" );

    if( m_referenceImpedancesOhm.empty() )
        m_referenceImpedancesOhm.assign( m_portCount, 50.0 );

    if( m_referenceImpedancesOhm.size() != m_portCount )
        throw std::invalid_argument( "Reference-impedance count must match the port count" );

    if( std::ranges::any_of( m_referenceImpedancesOhm,
                            []( double aValue ) { return aValue <= 0.0; } ) )
    {
        throw std::invalid_argument( "Reference impedances must be positive" );
    }
}


void NPORT_NETWORK::AddSample( double aFrequencyHz, std::vector<COMPLEX> aRowMajorMatrix )
{
    if( aFrequencyHz < 0.0 )
        throw std::invalid_argument( "Network frequencies cannot be negative" );

    if( !m_frequenciesHz.empty() && aFrequencyHz <= m_frequenciesHz.back() )
        throw std::invalid_argument( "Network frequencies must be strictly increasing" );

    if( aRowMajorMatrix.size() != m_portCount * m_portCount )
        throw std::invalid_argument( "Network sample matrix has the wrong size" );

    m_frequenciesHz.push_back( aFrequencyHz );
    m_samples.insert( m_samples.end(), std::make_move_iterator( aRowMajorMatrix.begin() ),
                      std::make_move_iterator( aRowMajorMatrix.end() ) );
}


const NPORT_NETWORK::COMPLEX& NPORT_NETWORK::Value( std::size_t aFrequencyIndex, std::size_t aRow,
                                                    std::size_t aColumn ) const
{
    return m_samples.at( FlatIndex( aFrequencyIndex, aRow, aColumn ) );
}


NPORT_NETWORK::COMPLEX& NPORT_NETWORK::Value( std::size_t aFrequencyIndex, std::size_t aRow,
                                              std::size_t aColumn )
{
    return m_samples.at( FlatIndex( aFrequencyIndex, aRow, aColumn ) );
}


std::size_t NPORT_NETWORK::FlatIndex( std::size_t aFrequencyIndex, std::size_t aRow,
                                     std::size_t aColumn ) const
{
    if( aFrequencyIndex >= FrequencyCount() || aRow >= m_portCount || aColumn >= m_portCount )
        throw std::out_of_range( "N-port matrix index is out of range" );

    return aFrequencyIndex * m_portCount * m_portCount + aRow * m_portCount + aColumn;
}

} // namespace KICAD::HIGH_SPEED
