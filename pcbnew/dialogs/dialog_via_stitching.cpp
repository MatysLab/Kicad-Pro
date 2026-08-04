/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "dialog_via_stitching.h"

#include <board.h>
#include <board_design_settings.h>
#include <netinfo.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/radiobox.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <cmath>


DIALOG_VIA_STITCHING::DIALOG_VIA_STITCHING( wxWindow* aParent, BOARD* aBoard ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Via Stitching" ) ),
        m_board( aBoard )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    wxString sourceChoices[] = { _( "Selected copper zones or polygons" ),
                                 _( "All matching zones on checked copper layers" ) };
    m_source = new wxRadioBox( this, wxID_ANY, _( "Stitching Area" ), wxDefaultPosition,
                               wxDefaultSize, 2, sourceChoices, 1, wxRA_SPECIFY_ROWS );
    mainSizer->Add( m_source, 0, wxEXPAND | wxALL, 10 );

    wxStaticBoxSizer* connectionSizer = new wxStaticBoxSizer( wxVERTICAL, this,
                                                              _( "Connection" ) );
    wxFlexGridSizer* connectionGrid = new wxFlexGridSizer( 2, 8, 8 );
    connectionGrid->AddGrowableCol( 1 );
    connectionGrid->Add( new wxStaticText( this, wxID_ANY, _( "Net:" ) ), 0,
                         wxALIGN_CENTER_VERTICAL );
    m_net = new wxChoice( this, wxID_ANY );

    for( const auto& [name, net] : m_board->GetNetInfo().NetsByName() )
    {
        if( net->GetNetCode() <= 0 )
            continue;

        m_net->Append( name );
        m_netCodes.push_back( net->GetNetCode() );
    }

    int groundIndex = m_net->FindString( wxT( "GND" ) );

    if( !m_netCodes.empty() )
        m_net->SetSelection( groundIndex != wxNOT_FOUND ? groundIndex : 0 );

    connectionGrid->Add( m_net, 1, wxEXPAND );
    connectionGrid->Add( new wxStaticText( this, wxID_ANY, _( "Copper layers:" ) ), 0,
                         wxALIGN_TOP );
    m_layers = new wxCheckListBox( this, wxID_ANY, wxDefaultPosition, wxSize( -1, 95 ) );

    for( PCB_LAYER_ID layer : m_board->GetEnabledLayers().CuStack() )
    {
        m_layerIds.push_back( layer );
        m_layers->Append( m_board->GetLayerName( layer ) );
        m_layers->Check( m_layers->GetCount() - 1 );
    }

    connectionGrid->Add( m_layers, 1, wxEXPAND );
    connectionSizer->Add( connectionGrid, 1, wxEXPAND | wxALL, 8 );
    mainSizer->Add( connectionSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    wxString spacingChoices[] = { _( "Manual center-to-center spacing" ),
                                  _( "Calculate from RF frequency" ) };
    m_spacingMode = new wxRadioBox( this, wxID_ANY, _( "Spacing" ), wxDefaultPosition,
                                    wxDefaultSize, 2, spacingChoices, 1, wxRA_SPECIFY_ROWS );
    mainSizer->Add( m_spacingMode, 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    wxFlexGridSizer* spacingGrid = new wxFlexGridSizer( 4, 8, 8 );
    spacingGrid->AddGrowableCol( 1 );
    spacingGrid->Add( new wxStaticText( this, wxID_ANY, _( "Spacing:" ) ), 0,
                      wxALIGN_CENTER_VERTICAL );
    m_spacing = new wxSpinCtrlDouble( this, wxID_ANY, wxT( "10" ), wxDefaultPosition,
                                      wxDefaultSize, wxSP_ARROW_KEYS, 0.1, 100.0, 10.0, 0.1 );
    spacingGrid->Add( m_spacing, 1, wxEXPAND );
    spacingGrid->Add( new wxStaticText( this, wxID_ANY, _( "mm (via center to center)" ) ), 0,
                      wxALIGN_CENTER_VERTICAL );
    spacingGrid->AddSpacer( 1 );

    spacingGrid->Add( new wxStaticText( this, wxID_ANY, _( "Frequency:" ) ), 0,
                      wxALIGN_CENTER_VERTICAL );
    m_frequency = new wxSpinCtrlDouble( this, wxID_ANY, wxT( "2.4" ), wxDefaultPosition,
                                        wxDefaultSize, wxSP_ARROW_KEYS, 0.01, 100.0, 2.4, 0.1 );
    spacingGrid->Add( m_frequency, 1, wxEXPAND );
    spacingGrid->Add( new wxStaticText( this, wxID_ANY, _( "GHz" ) ), 0,
                      wxALIGN_CENTER_VERTICAL );
    spacingGrid->AddSpacer( 1 );

    spacingGrid->Add( new wxStaticText( this, wxID_ANY, _( "Effective dielectric constant:" ) ),
                      0, wxALIGN_CENTER_VERTICAL );
    m_epsilonEffective = new wxSpinCtrlDouble( this, wxID_ANY, wxT( "3.8" ),
                                               wxDefaultPosition, wxDefaultSize,
                                               wxSP_ARROW_KEYS, 1.0, 20.0, 3.8, 0.05 );
    spacingGrid->Add( m_epsilonEffective, 1, wxEXPAND );
    m_wavelengthDivisor = new wxChoice( this, wxID_ANY );
    m_wavelengthDivisor->Append( _( "λg / 10" ) );
    m_wavelengthDivisor->Append( _( "λg / 20 (tighter)" ) );
    m_wavelengthDivisor->SetSelection( 0 );
    spacingGrid->Add( m_wavelengthDivisor, 0, wxEXPAND );
    spacingGrid->AddSpacer( 1 );

    spacingGrid->Add( new wxStaticText( this, wxID_ANY, _( "Calculated spacing:" ) ), 0,
                      wxALIGN_CENTER_VERTICAL );
    m_calculatedSpacing = new wxStaticText( this, wxID_ANY, wxEmptyString );
    spacingGrid->Add( m_calculatedSpacing, 1, wxALIGN_CENTER_VERTICAL );
    spacingGrid->AddSpacer( 1 );
    spacingGrid->AddSpacer( 1 );
    mainSizer->Add( spacingGrid, 0, wxEXPAND | wxALL, 10 );

    wxStaticBoxSizer* viaSizer = new wxStaticBoxSizer( wxHORIZONTAL, this, _( "Via Size" ) );
    viaSizer->Add( new wxStaticText( this, wxID_ANY, _( "Diameter:" ) ), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_viaDiameter = new wxSpinCtrlDouble( this, wxID_ANY, wxT( "0.6" ), wxDefaultPosition,
                                          wxDefaultSize, wxSP_ARROW_KEYS, 0.1, 10.0, 0.6, 0.05 );
    viaSizer->Add( m_viaDiameter, 1, wxRIGHT, 5 );
    viaSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm   Drill:" ) ), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_viaDrill = new wxSpinCtrlDouble( this, wxID_ANY, wxT( "0.3" ), wxDefaultPosition,
                                       wxDefaultSize, wxSP_ARROW_KEYS, 0.05, 9.0, 0.3, 0.05 );
    viaSizer->Add( m_viaDrill, 1, wxRIGHT, 5 );
    viaSizer->Add( new wxStaticText( this, wxID_ANY, _( "mm" ) ), 0, wxALIGN_CENTER_VERTICAL );
    mainSizer->Add( viaSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    const BOARD_DESIGN_SETTINGS& settings = m_board->GetDesignSettings();
    m_viaDiameter->SetValue( pcbIUScale.IUTomm( settings.GetCurrentViaSize() ) );
    m_viaDrill->SetValue( pcbIUScale.IUTomm( settings.GetCurrentViaDrill() ) );

    wxSizer* buttons = CreateStdDialogButtonSizer( wxOK | wxCANCEL );
    mainSizer->Add( buttons, 0, wxEXPAND | wxALL, 10 );
    SetSizerAndFit( mainSizer );

    m_spacingMode->Bind( wxEVT_RADIOBOX, &DIALOG_VIA_STITCHING::onModeChanged, this );
    m_frequency->Bind( wxEVT_SPINCTRLDOUBLE,
                       [this]( wxCommandEvent& ) { updateCalculatedSpacing(); } );
    m_epsilonEffective->Bind( wxEVT_SPINCTRLDOUBLE,
                              [this]( wxCommandEvent& ) { updateCalculatedSpacing(); } );
    m_wavelengthDivisor->Bind( wxEVT_CHOICE,
                               [this]( wxCommandEvent& ) { updateCalculatedSpacing(); } );
    wxCommandEvent initialEvent;
    onModeChanged( initialEvent );
    finishDialogSettings();
}


DIALOG_VIA_STITCHING::SOURCE DIALOG_VIA_STITCHING::GetSource() const
{
    return static_cast<SOURCE>( m_source->GetSelection() );
}


std::vector<PCB_LAYER_ID> DIALOG_VIA_STITCHING::GetLayers() const
{
    std::vector<PCB_LAYER_ID> layers;

    for( unsigned ii = 0; ii < m_layerIds.size(); ++ii )
    {
        if( m_layers->IsChecked( ii ) )
            layers.push_back( m_layerIds[ii] );
    }

    return layers;
}


int DIALOG_VIA_STITCHING::GetNetCode() const
{
    int selection = m_net->GetSelection();
    return selection >= 0 ? m_netCodes[selection] : 0;
}


int DIALOG_VIA_STITCHING::GetSpacing() const
{
    double spacingMm = m_spacing->GetValue();

    if( m_spacingMode->GetSelection() == 1 )
    {
        constexpr double C_MM_PER_NS = 299.792458;
        const double divisor = m_wavelengthDivisor->GetSelection() == 0 ? 10.0 : 20.0;
        spacingMm = C_MM_PER_NS / ( m_frequency->GetValue()
                                    * std::sqrt( m_epsilonEffective->GetValue() ) * divisor );
    }

    return pcbIUScale.mmToIU( spacingMm );
}


int DIALOG_VIA_STITCHING::GetViaDiameter() const
{
    return pcbIUScale.mmToIU( m_viaDiameter->GetValue() );
}


int DIALOG_VIA_STITCHING::GetViaDrill() const
{
    return pcbIUScale.mmToIU( m_viaDrill->GetValue() );
}


void DIALOG_VIA_STITCHING::updateCalculatedSpacing()
{
    m_calculatedSpacing->SetLabel( wxString::Format( wxT( "%.2f mm" ),
                                                     pcbIUScale.IUTomm( GetSpacing() ) ) );
}


void DIALOG_VIA_STITCHING::onModeChanged( wxCommandEvent& aEvent )
{
    const bool frequencyMode = m_spacingMode->GetSelection() == 1;
    m_spacing->Enable( !frequencyMode );
    m_frequency->Enable( frequencyMode );
    m_epsilonEffective->Enable( frequencyMode );
    m_wavelengthDivisor->Enable( frequencyMode );
    m_calculatedSpacing->Show( frequencyMode );
    updateCalculatedSpacing();
    Layout();
    aEvent.Skip();
}
