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

#ifndef HIGH_SPEED_TOUCHSTONE_READER_H
#define HIGH_SPEED_TOUCHSTONE_READER_H

#include <kicommon.h>

#include <high_speed/nport_network.h>

#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <vector>


namespace KICAD::HIGH_SPEED
{

enum class TOUCHSTONE_DIAGNOSTIC_SEVERITY
{
    WARNING,
    ERROR
};


struct KICOMMON_API TOUCHSTONE_DIAGNOSTIC
{
    TOUCHSTONE_DIAGNOSTIC_SEVERITY severity;
    std::size_t                    line = 0;
    std::string                    message;
};


struct KICOMMON_API TOUCHSTONE_RESULT
{
    std::optional<NPORT_NETWORK>          network;
    std::vector<TOUCHSTONE_DIAGNOSTIC> diagnostics;

    bool Ok() const;
};


/**
 * Reader for conventional single-ended Touchstone 1.x, 2.0 and 2.1 network data.
 *
 * Full, lower-triangular and upper-triangular matrices are converted to canonical row-major
 * matrices.  Mixed-mode and noise blocks are recognized but intentionally not imported by this
 * first implementation; an explicit diagnostic is returned instead of silently misinterpreting
 * them.
 */
class KICOMMON_API TOUCHSTONE_READER
{
public:
    static TOUCHSTONE_RESULT Read( std::istream& aInput,
                                   std::optional<std::size_t> aPortCountHint = std::nullopt );

    static TOUCHSTONE_RESULT ReadFile( const std::filesystem::path& aPath );

    /** Infer the port count from a conventional .sNp filename extension. */
    static std::optional<std::size_t> InferPortCount( const std::filesystem::path& aPath );
};

} // namespace KICAD::HIGH_SPEED

#endif // HIGH_SPEED_TOUCHSTONE_READER_H
