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

#include <high_speed/touchstone_reader.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>


namespace KICAD::HIGH_SPEED
{
namespace
{

enum class DATA_FORMAT
{
    MAGNITUDE_ANGLE,
    DB_ANGLE,
    REAL_IMAGINARY
};


enum class MATRIX_FORMAT
{
    FULL,
    LOWER,
    UPPER
};


enum class TWO_PORT_ORDER
{
    N11_N21_N12_N22,
    N11_N12_N21_N22
};


struct PARSE_STATE
{
    bool                       isVersion2 = false;
    bool                       sawVersion = false;
    bool                       sawKeyword = false;
    bool                       sawOptionLine = false;
    bool                       sawNumberOfPorts = false;
    bool                       sawNumberOfFrequencies = false;
    bool                       sawTwoPortDataOrder = false;
    bool                       sawNetworkData = false;
    bool                       sawEnd = false;
    bool                       inNetworkData = false;
    bool                       collectingReference = false;
    bool                       unsupportedMixedMode = false;
    std::optional<std::size_t> portCount;
    std::optional<std::size_t> declaredFrequencyCount;
    NETWORK_PARAMETER          parameter = NETWORK_PARAMETER::SCATTERING;
    DATA_FORMAT                dataFormat = DATA_FORMAT::MAGNITUDE_ANGLE;
    MATRIX_FORMAT              matrixFormat = MATRIX_FORMAT::FULL;
    TWO_PORT_ORDER             twoPortOrder = TWO_PORT_ORDER::N11_N21_N12_N22;
    double                     frequencyMultiplier = 1.0e9;
    std::vector<double>        optionReferences = { 50.0 };
    std::vector<double>        keywordReferences;
    std::vector<double>        networkTokens;
};


std::string trim( std::string aText )
{
    const auto first = aText.find_first_not_of( " \t\r\n" );

    if( first == std::string::npos )
        return {};

    const auto last = aText.find_last_not_of( " \t\r\n" );
    return aText.substr( first, last - first + 1 );
}


std::string upper( std::string aText )
{
    std::ranges::transform(
            aText, aText.begin(),
            []( unsigned char aChar ) { return static_cast<char>( std::toupper( aChar ) ); } );
    return aText;
}


std::vector<std::string> split( const std::string& aText )
{
    std::istringstream        stream( aText );
    std::vector<std::string> tokens;
    std::string              token;

    while( stream >> token )
        tokens.push_back( token );

    return tokens;
}


bool parseDouble( const std::string& aText, double& aValue )
{
    const char* begin = aText.data();
    const char* end = begin + aText.size();
    const auto  result = std::from_chars( begin, end, aValue, std::chars_format::general );
    return result.ec == std::errc() && result.ptr == end && std::isfinite( aValue );
}


bool parseSize( const std::string& aText, std::size_t& aValue )
{
    const char* begin = aText.data();
    const char* end = begin + aText.size();
    const auto  result = std::from_chars( begin, end, aValue );
    return result.ec == std::errc() && result.ptr == end;
}


void addDiagnostic( TOUCHSTONE_RESULT& aResult, TOUCHSTONE_DIAGNOSTIC_SEVERITY aSeverity,
                    std::size_t aLine, std::string aMessage )
{
    aResult.diagnostics.push_back( { aSeverity, aLine, std::move( aMessage ) } );
}


bool appendNumbers( const std::string& aText, std::vector<double>& aDestination,
                    TOUCHSTONE_RESULT& aResult, std::size_t aLine )
{
    for( const std::string& token : split( aText ) )
    {
        double value = 0.0;

        if( !parseDouble( token, value ) )
        {
            addDiagnostic( aResult, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, aLine,
                           "Expected a numeric value, found '" + token + "'" );
            return false;
        }

        aDestination.push_back( value );
    }

    return true;
}


void parseOptionLine( const std::string& aText, PARSE_STATE& aState, TOUCHSTONE_RESULT& aResult,
                      std::size_t aLine )
{
    if( aState.sawOptionLine )
    {
        addDiagnostic( aResult, TOUCHSTONE_DIAGNOSTIC_SEVERITY::WARNING, aLine,
                       "Additional Touchstone option line ignored" );
        return;
    }

    aState.sawOptionLine = true;
    const std::vector<std::string> tokens = split( aText.substr( 1 ) );

    for( std::size_t i = 0; i < tokens.size(); ++i )
    {
        const std::string token = upper( tokens[i] );

        if( token == "HZ" )
            aState.frequencyMultiplier = 1.0;
        else if( token == "KHZ" )
            aState.frequencyMultiplier = 1.0e3;
        else if( token == "MHZ" )
            aState.frequencyMultiplier = 1.0e6;
        else if( token == "GHZ" )
            aState.frequencyMultiplier = 1.0e9;
        else if( token == "S" )
            aState.parameter = NETWORK_PARAMETER::SCATTERING;
        else if( token == "Y" )
            aState.parameter = NETWORK_PARAMETER::ADMITTANCE;
        else if( token == "Z" )
            aState.parameter = NETWORK_PARAMETER::IMPEDANCE;
        else if( token == "H" )
            aState.parameter = NETWORK_PARAMETER::HYBRID_H;
        else if( token == "G" )
            aState.parameter = NETWORK_PARAMETER::HYBRID_G;
        else if( token == "MA" )
            aState.dataFormat = DATA_FORMAT::MAGNITUDE_ANGLE;
        else if( token == "DB" )
            aState.dataFormat = DATA_FORMAT::DB_ANGLE;
        else if( token == "RI" )
            aState.dataFormat = DATA_FORMAT::REAL_IMAGINARY;
        else if( token == "R" )
        {
            aState.optionReferences.clear();

            while( i + 1 < tokens.size() )
            {
                double reference = 0.0;

                if( !parseDouble( tokens[i + 1], reference ) )
                    break;

                aState.optionReferences.push_back( reference );
                ++i;
            }

            if( aState.optionReferences.empty() )
            {
                addDiagnostic( aResult, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, aLine,
                               "Option-line R must be followed by a positive resistance" );
            }
        }
        else
        {
            addDiagnostic( aResult, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, aLine,
                           "Unknown Touchstone option '" + tokens[i] + "'" );
        }
    }
}


std::pair<std::string, std::string> parseKeyword( const std::string& aLine )
{
    const std::size_t close = aLine.find( ']' );

    if( close == std::string::npos )
        return { {}, {} };

    return { upper( trim( aLine.substr( 1, close - 1 ) ) ), trim( aLine.substr( close + 1 ) ) };
}


NPORT_NETWORK::COMPLEX makeComplex( double aFirst, double aSecond, DATA_FORMAT aFormat )
{
    if( aFormat == DATA_FORMAT::REAL_IMAGINARY )
        return { aFirst, aSecond };

    const double magnitude = aFormat == DATA_FORMAT::DB_ANGLE ? std::pow( 10.0, aFirst / 20.0 )
                                                               : aFirst;
    return std::polar( magnitude, aSecond * std::numbers::pi / 180.0 );
}


std::vector<std::pair<std::size_t, std::size_t>> matrixPositions( std::size_t aPortCount,
                                                                  MATRIX_FORMAT aMatrixFormat,
                                                                  TWO_PORT_ORDER aTwoPortOrder )
{
    std::vector<std::pair<std::size_t, std::size_t>> positions;

    if( aMatrixFormat == MATRIX_FORMAT::LOWER )
    {
        for( std::size_t row = 0; row < aPortCount; ++row )
        {
            for( std::size_t column = 0; column <= row; ++column )
                positions.emplace_back( row, column );
        }
    }
    else if( aMatrixFormat == MATRIX_FORMAT::UPPER )
    {
        for( std::size_t row = 0; row < aPortCount; ++row )
        {
            for( std::size_t column = row; column < aPortCount; ++column )
                positions.emplace_back( row, column );
        }
    }
    else if( aPortCount == 2 && aTwoPortOrder == TWO_PORT_ORDER::N11_N21_N12_N22 )
    {
        positions = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    }
    else
    {
        for( std::size_t row = 0; row < aPortCount; ++row )
        {
            for( std::size_t column = 0; column < aPortCount; ++column )
                positions.emplace_back( row, column );
        }
    }

    return positions;
}


} // namespace


bool TOUCHSTONE_RESULT::Ok() const
{
    return network.has_value()
           && std::ranges::none_of( diagnostics, []( const TOUCHSTONE_DIAGNOSTIC& aDiagnostic )
                                    {
                                        return aDiagnostic.severity
                                               == TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR;
                                    } );
}


TOUCHSTONE_RESULT TOUCHSTONE_READER::Read( std::istream& aInput,
                                           std::optional<std::size_t> aPortCountHint )
{
    TOUCHSTONE_RESULT result;
    PARSE_STATE       state;
    state.portCount = aPortCountHint;

    std::string line;
    std::size_t lineNumber = 0;

    while( std::getline( aInput, line ) )
    {
        ++lineNumber;

        if( const std::size_t comment = line.find( '!' ); comment != std::string::npos )
            line.erase( comment );

        line = trim( line );

        if( line.empty() )
            continue;

        if( line.front() == '#' )
        {
            parseOptionLine( line, state, result, lineNumber );

            if( !state.isVersion2 )
                state.inNetworkData = true;

            continue;
        }

        if( line.front() == '[' )
        {
            state.sawKeyword = true;
            state.collectingReference = false;
            const auto [keyword, argument] = parseKeyword( line );

            if( keyword.empty() )
            {
                addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                               "Malformed Touchstone keyword" );
                continue;
            }

            if( keyword == "VERSION" )
            {
                state.sawVersion = true;
                state.isVersion2 = argument == "2.0" || argument == "2.1";

                if( !state.isVersion2 )
                {
                    addDiagnostic(
                            result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                            "Only Touchstone versions 2.0 and 2.1 are supported by [Version]" );
                }
            }
            else if( keyword == "NUMBER OF PORTS" )
            {
                state.sawNumberOfPorts = true;
                std::size_t count = 0;

                if( !parseSize( argument, count ) || count == 0 )
                {
                    addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                                   "[Number of Ports] requires a positive integer" );
                }
                else if( state.portCount && *state.portCount != count )
                {
                    addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                                   "Filename and [Number of Ports] disagree" );
                }
                else
                {
                    state.portCount = count;
                }
            }
            else if( keyword == "NUMBER OF FREQUENCIES" )
            {
                state.sawNumberOfFrequencies = true;
                std::size_t count = 0;

                if( !parseSize( argument, count ) || count == 0 )
                {
                    addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                                   "[Number of Frequencies] requires a positive integer" );
                }
                else
                {
                    state.declaredFrequencyCount = count;
                }
            }
            else if( keyword == "TWO-PORT DATA ORDER" )
            {
                state.sawTwoPortDataOrder = true;
                const std::string order = upper( argument );

                if( order == "21_12" )
                    state.twoPortOrder = TWO_PORT_ORDER::N11_N21_N12_N22;
                else if( order == "12_21" )
                    state.twoPortOrder = TWO_PORT_ORDER::N11_N12_N21_N22;
                else
                {
                    addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                                   "[Two-Port Data Order] must be 21_12 or 12_21" );
                }
            }
            else if( keyword == "REFERENCE" )
            {
                state.keywordReferences.clear();
                state.collectingReference = true;
                appendNumbers( argument, state.keywordReferences, result, lineNumber );
            }
            else if( keyword == "MATRIX FORMAT" )
            {
                const std::string format = upper( argument );

                if( format == "FULL" )
                    state.matrixFormat = MATRIX_FORMAT::FULL;
                else if( format == "LOWER" )
                    state.matrixFormat = MATRIX_FORMAT::LOWER;
                else if( format == "UPPER" )
                    state.matrixFormat = MATRIX_FORMAT::UPPER;
                else
                {
                    addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                                   "[Matrix Format] must be Full, Lower, or Upper" );
                }
            }
            else if( keyword == "NETWORK DATA" )
            {
                state.sawNetworkData = true;
                state.inNetworkData = true;
            }
            else if( keyword == "MIXED-MODE ORDER" )
            {
                state.unsupportedMixedMode = true;
                addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, lineNumber,
                               "Mixed-mode Touchstone import is not implemented yet" );
            }
            else if( keyword == "NOISE DATA" )
            {
                state.inNetworkData = false;
                addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::WARNING, lineNumber,
                               "Touchstone noise data is not imported" );
            }
            else if( keyword == "END" )
            {
                state.inNetworkData = false;
                state.sawEnd = true;
            }
            else if( keyword == "NUMBER OF NOISE FREQUENCIES" || keyword == "BEGIN INFORMATION"
                     || keyword == "END INFORMATION" )
            {
                // Recognized metadata that does not alter conventional network data.
            }
            else
            {
                addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::WARNING, lineNumber,
                               "Unsupported Touchstone keyword [" + keyword + "] ignored" );
            }

            continue;
        }

        if( state.collectingReference )
        {
            appendNumbers( line, state.keywordReferences, result, lineNumber );

            if( state.portCount && state.keywordReferences.size() >= *state.portCount )
                state.collectingReference = false;
        }
        else if( state.inNetworkData )
        {
            appendNumbers( line, state.networkTokens, result, lineNumber );
        }
    }

    if( !state.sawOptionLine )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Touchstone option line is missing" );

    if( state.sawKeyword && !state.sawVersion )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Touchstone keyword syntax requires a leading [Version] 2.0 or 2.1" );

    if( !state.portCount )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Port count is unavailable; use an .sNp filename or [Number of Ports]" );

    if( state.isVersion2 && !state.sawNetworkData )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Touchstone 2.x file is missing [Network Data]" );

    if( state.isVersion2 && !state.sawNumberOfPorts )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Touchstone 2.x file is missing [Number of Ports]" );

    if( state.isVersion2 && !state.sawNumberOfFrequencies )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Touchstone 2.x file is missing [Number of Frequencies]" );

    if( state.isVersion2 && state.portCount == 2 && !state.sawTwoPortDataOrder )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Two-port Touchstone 2.x file is missing [Two-Port Data Order]" );

    if( state.isVersion2 && !state.sawEnd )
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Touchstone 2.x file is missing [End]" );

    if( state.isVersion2 && state.optionReferences.size() != 1 )
        addDiagnostic(
                result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                "Touchstone 2.x option line accepts one common R value; use [Reference] for "
                "per-port values" );

    const bool hasErrors = std::ranges::any_of(
            result.diagnostics,
            []( const TOUCHSTONE_DIAGNOSTIC& aDiagnostic )
            {
                return aDiagnostic.severity == TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR;
            } );

    if( state.unsupportedMixedMode || !state.portCount || hasErrors )
    {
        return result;
    }

    const std::size_t portCount = *state.portCount;

    if( ( state.parameter == NETWORK_PARAMETER::HYBRID_H
          || state.parameter == NETWORK_PARAMETER::HYBRID_G )
        && portCount != 2 )
    {
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "H and G network parameters are only valid for two-port data" );
        return result;
    }

    std::vector<double> references = state.keywordReferences.empty() ? state.optionReferences
                                                                      : state.keywordReferences;

    if( references.size() == 1 )
    {
        const double commonReference = references.front();
        references.assign( portCount, commonReference );
    }

    if( references.size() != portCount
        || std::ranges::any_of( references, []( double aValue ) { return aValue <= 0.0; } ) )
    {
        addDiagnostic(
                result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                "Reference resistance count must match the port count and all values must be "
                "positive" );
        return result;
    }

    const auto positions = matrixPositions( portCount, state.matrixFormat, state.twoPortOrder );
    const std::size_t valuesPerFrequency = 1 + 2 * positions.size();

    if( state.networkTokens.empty() || state.networkTokens.size() % valuesPerFrequency != 0 )
    {
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Network-data value count does not match the port and matrix format" );
        return result;
    }

    const std::size_t frequencyCount = state.networkTokens.size() / valuesPerFrequency;

    if( state.declaredFrequencyCount && *state.declaredFrequencyCount != frequencyCount )
    {
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "[Number of Frequencies] does not match the network data" );
        return result;
    }

    try
    {
        NPORT_NETWORK network( portCount, state.parameter, std::move( references ) );
        std::size_t   offset = 0;

        for( std::size_t frequencyIndex = 0; frequencyIndex < frequencyCount; ++frequencyIndex )
        {
            const double frequencyHz = state.networkTokens[offset++] * state.frequencyMultiplier;
            std::vector<NPORT_NETWORK::COMPLEX> matrix( portCount * portCount );

            for( const auto& [row, column] : positions )
            {
                const auto value = makeComplex( state.networkTokens[offset],
                                                state.networkTokens[offset + 1], state.dataFormat );
                offset += 2;
                matrix[row * portCount + column] = value;

                if( state.matrixFormat != MATRIX_FORMAT::FULL && row != column )
                    matrix[column * portCount + row] = value;
            }

            network.AddSample( frequencyHz, std::move( matrix ) );
        }

        result.network = std::move( network );
    }
    catch( const std::exception& exception )
    {
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0, exception.what() );
    }

    return result;
}


TOUCHSTONE_RESULT TOUCHSTONE_READER::ReadFile( const std::filesystem::path& aPath )
{
    std::ifstream input( aPath );

    if( !input )
    {
        TOUCHSTONE_RESULT result;
        addDiagnostic( result, TOUCHSTONE_DIAGNOSTIC_SEVERITY::ERROR, 0,
                       "Unable to open Touchstone file '" + aPath.string() + "'" );
        return result;
    }

    return Read( input, InferPortCount( aPath ) );
}


std::optional<std::size_t> TOUCHSTONE_READER::InferPortCount( const std::filesystem::path& aPath )
{
    const std::string extension = upper( aPath.extension().string() );

    if( extension.size() < 4 || extension[0] != '.' || extension[1] != 'S'
        || extension.back() != 'P' )
    {
        return std::nullopt;
    }

    std::size_t count = 0;

    if( !parseSize( extension.substr( 2, extension.size() - 3 ), count ) || count == 0 )
        return std::nullopt;

    return count;
}

} // namespace KICAD::HIGH_SPEED
