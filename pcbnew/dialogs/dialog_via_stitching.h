/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef DIALOG_VIA_STITCHING_H
#define DIALOG_VIA_STITCHING_H

#include <dialog_shim.h>
#include <layer_ids.h>

#include <vector>

class BOARD;
class wxCheckListBox;
class wxChoice;
class wxRadioBox;
class wxSpinCtrlDouble;
class wxStaticText;


class DIALOG_VIA_STITCHING : public DIALOG_SHIM
{
public:
    enum class SOURCE
    {
        SELECTED_POLYGONS,
        COPPER_LAYERS
    };

    DIALOG_VIA_STITCHING( wxWindow* aParent, BOARD* aBoard );

    SOURCE GetSource() const;
    std::vector<PCB_LAYER_ID> GetLayers() const;
    int GetNetCode() const;
    int GetSpacing() const;
    int GetViaDiameter() const;
    int GetViaDrill() const;

private:
    void updateCalculatedSpacing();
    void onModeChanged( wxCommandEvent& aEvent );

    BOARD*            m_board;
    wxRadioBox*       m_source;
    wxCheckListBox*   m_layers;
    wxChoice*         m_net;
    wxRadioBox*       m_spacingMode;
    wxSpinCtrlDouble* m_spacing;
    wxSpinCtrlDouble* m_frequency;
    wxSpinCtrlDouble* m_epsilonEffective;
    wxChoice*         m_wavelengthDivisor;
    wxStaticText*     m_calculatedSpacing;
    wxSpinCtrlDouble* m_viaDiameter;
    wxSpinCtrlDouble* m_viaDrill;
    std::vector<int>  m_netCodes;
    std::vector<PCB_LAYER_ID> m_layerIds;
};

#endif
