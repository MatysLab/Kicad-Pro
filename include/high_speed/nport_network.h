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

#ifndef HIGH_SPEED_NPORT_NETWORK_H
#define HIGH_SPEED_NPORT_NETWORK_H

#include <kicommon.h>

#include <complex>
#include <cstddef>
#include <string>
#include <vector>


namespace KICAD::HIGH_SPEED
{

/** Electrical network-parameter representation stored by an NPORT_NETWORK. */
enum class NETWORK_PARAMETER
{
    SCATTERING,
    ADMITTANCE,
    IMPEDANCE,
    HYBRID_H,
    HYBRID_G
};


/**
 * Frequency-ordered complex N-port data.
 *
 * Matrix values use conventional row-major N_ij indexing: aRow is the response port and aColumn
 * is the driven port.  Importers are responsible for converting any file-specific ordering into
 * this canonical layout.
 */
class KICOMMON_API NPORT_NETWORK
{
public:
    using COMPLEX = std::complex<double>;

    NPORT_NETWORK( std::size_t aPortCount, NETWORK_PARAMETER aParameter,
                   std::vector<double> aReferenceImpedancesOhm = {} );

    std::size_t PortCount() const { return m_portCount; }
    std::size_t FrequencyCount() const { return m_frequenciesHz.size(); }

    NETWORK_PARAMETER Parameter() const { return m_parameter; }

    const std::vector<double>& FrequenciesHz() const { return m_frequenciesHz; }
    const std::vector<double>& ReferenceImpedancesOhm() const { return m_referenceImpedancesOhm; }

    /**
     * Add one complete N x N sample.
     *
     * @throws std::invalid_argument if the frequency is negative or not strictly increasing, or
     *         if the matrix does not contain exactly PortCount() squared values.
     */
    void AddSample( double aFrequencyHz, std::vector<COMPLEX> aRowMajorMatrix );

    const COMPLEX& Value( std::size_t aFrequencyIndex, std::size_t aRow,
                          std::size_t aColumn ) const;

    COMPLEX& Value( std::size_t aFrequencyIndex, std::size_t aRow, std::size_t aColumn );

    const std::vector<COMPLEX>& Samples() const { return m_samples; }

private:
    std::size_t FlatIndex( std::size_t aFrequencyIndex, std::size_t aRow,
                           std::size_t aColumn ) const;

private:
    std::size_t          m_portCount;
    NETWORK_PARAMETER    m_parameter;
    std::vector<double>  m_referenceImpedancesOhm;
    std::vector<double>  m_frequenciesHz;
    std::vector<COMPLEX> m_samples;
};

} // namespace KICAD::HIGH_SPEED

#endif // HIGH_SPEED_NPORT_NETWORK_H
