/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <macros.h>             // arrayDim definition
#include <pcb_edit_frame.h>
#include <board.h>
#include <board_design_settings.h>
#include <dialogs/dialog_color_picker.h>
#include <widgets/paged_dialog.h>
#include <widgets/layer_presentation.h>
#include <widgets/wx_panel.h>
#include <wx/bmpcbox.h>
#include <wx/log.h>
#include <wx/rawbmp.h>
#include <wx/clipbrd.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/richmsgdlg.h>
#include <wx/statbox.h>
#include <wx/settings.h>
#include <math/util.h>      // for KiROUND
#include <transline_calculations/coupled_microstrip.h>
#include <transline_calculations/coupled_stripline.h>
#include <transline_calculations/microstrip.h>
#include <transline_calculations/stripline.h>

#include "panel_board_stackup.h"
#include "panel_board_finish.h"
#include <panel_setup_layers.h>
#include "board_stackup_reporter.h"
#include <bitmaps.h>
#include "dialog_dielectric_list_manager.h"
#include <wx/textdlg.h>

#include <locale_io.h>
#include <eda_list_dialog.h>
#include <richio.h>
#include <string_utils.h>               // for UIDouble2Str()

#include <algorithm>
#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <project/project_file.h>
#include <stdexcept>


// Some wx widget ID to know what widget has fired a event:
#define ID_INCREMENT 256    // space between 2 ID type. Bigger than the layer count max

// The actual widget IDs are the base id + the row index.
// they are used in events to know the row index of the control that fired the event
enum WIDGETS_IDS
{
    ID_ITEM_MATERIAL = 10000,       // Be sure it is higher than other IDs
                                    // used in the board setup dialog
    ID_ITEM_THICKNESS = ID_ITEM_MATERIAL + ID_INCREMENT,
    ID_ITEM_THICKNESS_LOCKED = ID_ITEM_THICKNESS + ID_INCREMENT,
    ID_ITEM_COLOR = ID_ITEM_THICKNESS_LOCKED + ID_INCREMENT,
};

// Default colors to draw icons:
static wxColor copperColor( 220, 180, 30 );
static wxColor dielectricColor( 75, 120, 75 );
static wxColor pasteColor( 200, 200, 200 );

static void drawBitmap( wxBitmap& aBitmap, wxColor aColor );

static const wxString KICAD_PRO_STACKUP_PROPERTY = wxT( "kicad_pro.stackup_control" );


const std::vector<PANEL_SETUP_BOARD_STACKUP::STACKUP_PRESET>&
PANEL_SETUP_BOARD_STACKUP::getStackupPresets()
{
    static const std::vector<STACKUP_PRESET> presets = []
    {
        std::vector<STACKUP_PRESET> result;

        auto dielectric = []( const wxString& aMaterial, double aThickness, double aEpsilonR,
                              bool aCore = false )
        {
            return PRESET_DIELECTRIC{ aMaterial, aThickness, aEpsilonR, aCore };
        };

        auto jlcPrepreg = [&]( const wxString& aType, double aThickness )
        {
            double epsilonR = 4.1;

            if( aType == wxT( "7628" ) )
                epsilonR = 4.4;
            else if( aType == wxT( "1080" ) )
                epsilonR = 3.91;
            else if( aType == wxT( "2116" ) )
                epsilonR = 4.16;

            return dielectric( aType + wxT( "*1" ), aThickness, epsilonR );
        };

        auto core = [&]( double aThickness )
        {
            return dielectric( wxT( "Core" ), aThickness, 4.6, true );
        };

        auto reversed = []( std::vector<PRESET_DIELECTRIC> aLayers )
        {
            std::reverse( aLayers.begin(), aLayers.end() );
            return aLayers;
        };

        auto addJlc4 = [&]( const wxString& aName, std::vector<PRESET_DIELECTRIC> aOuter,
                            double aCoreThickness )
        {
            result.push_back( { wxT( "JLCPCB" ), aName, { 0.035, 0.0152, 0.0152, 0.035 },
                                { aOuter, { core( aCoreThickness ) }, reversed( aOuter ) } } );
        };

        addJlc4( wxT( "JLC04161H-7628" ), { jlcPrepreg( wxT( "7628" ), 0.2104 ) }, 1.065 );
        addJlc4( wxT( "JLC04161H-3313" ), { jlcPrepreg( wxT( "3313" ), 0.0994 ) }, 1.265 );
        addJlc4( wxT( "JLC04161H-1080" ), { jlcPrepreg( wxT( "1080" ), 0.0764 ) }, 1.265 );
        addJlc4( wxT( "JLC04161H-7628A" ),
                 { jlcPrepreg( wxT( "7628" ), 0.218 ), jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                 0.865 );
        addJlc4( wxT( "JLC04161H-3313A" ),
                 { jlcPrepreg( wxT( "3313" ), 0.107 ), jlcPrepreg( wxT( "3313" ), 0.0994 ) },
                 1.065 );
        addJlc4( wxT( "JLC04161H-1080A" ),
                 { jlcPrepreg( wxT( "1080" ), 0.084 ), jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                 1.065 );
        addJlc4( wxT( "JLC04161H-7628B" ),
                 { jlcPrepreg( wxT( "7628" ), 0.218 ), jlcPrepreg( wxT( "7628" ), 0.218 ),
                   jlcPrepreg( wxT( "2116" ), 0.1164 ) },
                 0.4 );
        addJlc4( wxT( "JLC04161H-2116A" ),
                 { jlcPrepreg( wxT( "2116" ), 0.124 ), jlcPrepreg( wxT( "7628" ), 0.218 ),
                   jlcPrepreg( wxT( "2116" ), 0.1164 ) },
                 0.6 );
        addJlc4( wxT( "JLC04161H-2116B" ),
                 { jlcPrepreg( wxT( "2116" ), 0.124 ), jlcPrepreg( wxT( "7628" ), 0.2104 ) },
                 0.865 );
        addJlc4( wxT( "JLC04161H-2116C" ),
                 { jlcPrepreg( wxT( "2116" ), 0.124 ), jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                 1.065 );
        addJlc4( wxT( "JLC04161H-2116" ), { jlcPrepreg( wxT( "2116" ), 0.1164 ) }, 1.265 );
        addJlc4( wxT( "JLC04161H-7628E" ),
                 { jlcPrepreg( wxT( "7628" ), 0.218 ), jlcPrepreg( wxT( "7628" ), 0.2104 ) },
                 0.6 );
        addJlc4( wxT( "JLC04161H-2116D" ),
                 { jlcPrepreg( wxT( "2116" ), 0.124 ), jlcPrepreg( wxT( "7628" ), 0.2104 ) },
                 0.7 );
        addJlc4( wxT( "JLC04161H-7628F" ),
                 { jlcPrepreg( wxT( "7628" ), 0.218 ), jlcPrepreg( wxT( "7628" ), 0.218 ),
                   jlcPrepreg( wxT( "7628" ), 0.2104 ) },
                 0.25 );
        addJlc4( wxT( "JLC04161H-2116E" ),
                 { jlcPrepreg( wxT( "2116" ), 0.124 ), jlcPrepreg( wxT( "2116" ), 0.1164 ) },
                 0.865 );
        addJlc4( wxT( "JLC04161H-7628D" ), { jlcPrepreg( wxT( "7628" ), 0.2104 ) }, 1.265 );
        addJlc4( wxT( "JLC04161H-7628C" ),
                 { jlcPrepreg( wxT( "7628" ), 0.218 ), jlcPrepreg( wxT( "7628" ), 0.218 ),
                   jlcPrepreg( wxT( "7628" ), 0.2104 ) },
                 0.15 );

        auto addJlc6 = [&]( const wxString& aName, std::vector<PRESET_DIELECTRIC> aTop,
                            double aTopCore, std::vector<PRESET_DIELECTRIC> aMiddle,
                            double aBottomCore, std::vector<PRESET_DIELECTRIC> aBottom )
        {
            result.push_back( { wxT( "JLCPCB" ), aName,
                                { 0.035, 0.0152, 0.0152, 0.0152, 0.0152, 0.035 },
                                { aTop, { core( aTopCore ) }, aMiddle,
                                  { core( aBottomCore ) }, aBottom } } );
        };

        auto symmetricJlc6 = [&]( const wxString& aName, std::vector<PRESET_DIELECTRIC> aOuter,
                                  double aCore, std::vector<PRESET_DIELECTRIC> aMiddle )
        {
            addJlc6( aName, aOuter, aCore, aMiddle, aCore, reversed( aOuter ) );
        };

        symmetricJlc6( wxT( "JLC06161H-3313" ), { jlcPrepreg( wxT( "3313" ), 0.0994 ) },
                       0.55, { jlcPrepreg( wxT( "2116" ), 0.1088 ) } );
        symmetricJlc6( wxT( "JLC06161H-7628" ), { jlcPrepreg( wxT( "7628" ), 0.2104 ) },
                       0.4, { jlcPrepreg( wxT( "7628" ), 0.2028 ) } );
        symmetricJlc6( wxT( "JLC06161H-1080" ), { jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                       0.55, { jlcPrepreg( wxT( "7628" ), 0.2104 ) } );
        symmetricJlc6( wxT( "JLC06161H-2116A" ), { jlcPrepreg( wxT( "2116" ), 0.1164 ) },
                       0.13, { jlcPrepreg( wxT( "2116" ), 0.1164 ), core( 0.7 ),
                               jlcPrepreg( wxT( "2116" ), 0.1164 ) } );
        symmetricJlc6( wxT( "JLC06161H-1080A" ), { jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                       0.6, { jlcPrepreg( wxT( "3313" ), 0.0994 ) } );
        addJlc6( wxT( "JLC06161H-3313C" ),
                 { jlcPrepreg( wxT( "3313" ), 0.107 ), jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                 0.4, { jlcPrepreg( wxT( "1080" ), 0.0764 ),
                        jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                 0.4, { jlcPrepreg( wxT( "1080" ), 0.0784 ),
                        jlcPrepreg( wxT( "3313" ), 0.107 ) } );
        symmetricJlc6( wxT( "JLC06161H-1080B" ), { jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                       0.1, { jlcPrepreg( wxT( "7628" ), 0.2104 ), core( 0.7 ),
                              jlcPrepreg( wxT( "7628" ), 0.2104 ) } );
        symmetricJlc6( wxT( "JLC06161H-3313E" ), { jlcPrepreg( wxT( "3313" ), 0.0994 ) },
                       0.1, { jlcPrepreg( wxT( "7628" ), 0.2104 ), core( 0.7 ),
                              jlcPrepreg( wxT( "7628" ), 0.2104 ) } );
        symmetricJlc6( wxT( "JLC06161H-2116B" ), { jlcPrepreg( wxT( "2116" ), 0.1164 ) },
                       0.5, { jlcPrepreg( wxT( "1080" ), 0.0764 ),
                              jlcPrepreg( wxT( "1080" ), 0.0764 ) } );
        symmetricJlc6( wxT( "JLC06161H-2116" ),
                       { jlcPrepreg( wxT( "2116" ), 0.127 ),
                         jlcPrepreg( wxT( "2313" ), 0.0964 ) },
                       0.3, { jlcPrepreg( wxT( "7628" ), 0.2084 ),
                              jlcPrepreg( wxT( "7628" ), 0.2084 ) } );
        symmetricJlc6( wxT( "JLC06161H-7628B" ),
                       { jlcPrepreg( wxT( "7628" ), 0.218 ),
                         jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                       0.35, { jlcPrepreg( wxT( "1080" ), 0.0764 ),
                               jlcPrepreg( wxT( "1080" ), 0.0764 ) } );
        symmetricJlc6( wxT( "JLC06161H-3313D" ),
                       { jlcPrepreg( wxT( "3313" ), 0.107 ),
                         jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                       0.25, { jlcPrepreg( wxT( "7628" ), 0.2104 ),
                               jlcPrepreg( wxT( "2116" ), 0.124 ),
                               jlcPrepreg( wxT( "7628" ), 0.2104 ) } );
        symmetricJlc6( wxT( "JLC06161H-7628A" ),
                       { jlcPrepreg( wxT( "7628" ), 0.218 ),
                         jlcPrepreg( wxT( "7628" ), 0.218 ),
                         jlcPrepreg( wxT( "1080" ), 0.0764 ) },
                       0.1, { jlcPrepreg( wxT( "2116" ), 0.1164 ),
                              jlcPrepreg( wxT( "2116" ), 0.1164 ) } );

        auto pcbwayPp = [&]( const wxString& aType, double aThickness, double aEpsilonR )
        {
            return dielectric( aType, aThickness, aEpsilonR );
        };

        auto addPcbway = [&]( int aLayers, double aFinishedThickness,
                              std::vector<PRESET_DIELECTRIC> aDielectrics )
        {
            std::vector<double> copper( aLayers, 0.035 );
            std::vector<std::vector<PRESET_DIELECTRIC>> groups;

            for( const PRESET_DIELECTRIC& layer : aDielectrics )
                groups.push_back( { layer } );

            result.push_back( { wxT( "PCBWay" ),
                                wxString::Format( wxT( "Regular %dL / %.1f mm / 1 oz / 70%%" ),
                                                  aLayers, aFinishedThickness ),
                                copper, groups } );
        };

        const PRESET_DIELECTRIC pp7628 = pcbwayPp( wxT( "7628 RC46%" ), 0.196, 4.74 );
        const PRESET_DIELECTRIC pp2116 = pcbwayPp( wxT( "2116 RC58%" ), 0.130, 4.45 );
        const PRESET_DIELECTRIC pp3313 = pcbwayPp( wxT( "3313 RC58%" ), 0.103, 4.45 );

        addPcbway( 4, 1.6, { pp7628, core( 1.030 ), pp7628 } );
        addPcbway( 6, 1.6, { pp2116, core( 0.430 ), pp7628, core( 0.430 ), pp2116 } );
        addPcbway( 8, 1.6, { pp2116, core( 0.230 ), pp7628, core( 0.230 ), pp7628,
                             core( 0.230 ), pp2116 } );
        addPcbway( 10, 1.6, { pp3313, core( 0.130 ), pp7628, core( 0.130 ), pp7628,
                              core( 0.130 ), pp7628, core( 0.130 ), pp3313 } );
        addPcbway( 12, 1.6, { pp3313, core( 0.130 ), pp3313, core( 0.130 ), pp3313,
                              core( 0.130 ), pp3313, core( 0.130 ), pp3313,
                              core( 0.130 ), pp3313 } );

        for( int layers : { 14, 16, 18 } )
        {
            std::vector<PRESET_DIELECTRIC> dielectrics;
            const double coreThickness = layers == 14 ? 0.170 : ( layers == 16 ? 0.130 : 0.081 );

            for( int ii = 0; ii < layers - 1; ++ii )
                dielectrics.push_back( ii % 2 == 0 ? pp2116 : core( coreThickness ) );

            addPcbway( layers, 2.4, dielectrics );
        }

        return result;
    }();

    return presets;
}


PANEL_SETUP_BOARD_STACKUP::PANEL_SETUP_BOARD_STACKUP( wxWindow* aParentWindow,
                                                      PCB_EDIT_FRAME* aFrame,
                                                      PANEL_SETUP_LAYERS* aPanelLayers,
                                                      PANEL_SETUP_BOARD_FINISH* aPanelFinish ):
        PANEL_SETUP_BOARD_STACKUP_BASE( aParentWindow ),
        m_delectricMatList( DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_DIELECTRIC ),
        m_solderMaskMatList( DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_SOLDERMASK ),
        m_silkscreenMatList( DIELECTRIC_SUBSTRATE_LIST::DL_MATERIAL_SILKSCREEN ),
        m_board( aFrame->GetBoard() ),
        m_frame( aFrame ),
        m_lastUnits( aFrame->GetUserUnits() )
{
    m_panelLayers = aPanelLayers;
    m_panelFinish = aPanelFinish;
    m_brdSettings = &m_board->GetDesignSettings();

    m_panel1->SetBorders( false, false, true, true );

    m_panelLayers->SetPhysicalStackupPanel( this );

    m_enabledLayers = m_board->GetEnabledLayers() & BOARD_STACKUP::StackupAllowedBrdLayers();

    // Use a good size for color swatches (icons) in this dialog
    m_colorSwatchesSize = wxSize( 14, 14 );
    m_colorIconsSize = wxSize( 24, 14 );

    // Calculates a good size for wxTextCtrl to enter Epsilon R and Loss tan
    // ("0.0000000" + margins)
    m_numericFieldsSize = GetTextExtent( wxT( "X.XXXXXXX" ) );
    m_numericFieldsSize.y = -1;     // Use default for the vertical size

    // Calculates a minimal size for wxTextCtrl to enter a dim with units
    // ("000.0000000 mils" + margins)
    m_numericTextCtrlSize = GetTextExtent( wxT( "XXX.XXXXXXX mils" ) );
    m_numericTextCtrlSize.y = -1;     // Use default for the vertical size

    // The grid column containing the lock checkbox is kept to a minimal
    // size. So we use a wxStaticBitmap: set the bitmap itself
    m_bitmapLockThickness->SetBitmap( KiBitmapBundle( BITMAPS::locked ) );

    // Gives a minimal size of wxTextCtrl showing dimensions+units
    m_tcCTValue->SetMinSize( m_numericTextCtrlSize );

    // Prepare dielectric layer type: layer type keyword is "core" or "prepreg"
    m_core_prepreg_choice.Add( _( "Core" ) );
    m_core_prepreg_choice.Add( _( "PrePreg" ) );

    buildLayerStackPanel( true );
    synchronizeWithBoard( true );
    computeBoardThickness();
    buildStackupPresetControls();
    buildImpedancePanel();
    loadProjectImpedanceSettings();

    m_impedanceUpdateTimer.Bind( wxEVT_TIMER,
                                 [this]( wxTimerEvent& ) { updateAllImpedanceRows(); } );

    m_frame->Bind( EDA_EVT_UNITS_CHANGED, &PANEL_SETUP_BOARD_STACKUP::onUnitsChanged, this );
}


PANEL_SETUP_BOARD_STACKUP::~PANEL_SETUP_BOARD_STACKUP()
{
    m_impedanceUpdateTimer.Stop();
    disconnectEvents();
}


void PANEL_SETUP_BOARD_STACKUP::onUnitsChanged( wxCommandEvent& event )
{
    EDA_UNITS    newUnits = m_frame->GetUserUnits();
    EDA_IU_SCALE scale = m_frame->GetIuScale();

    auto convert =
            [&]( wxTextCtrl* aTextCtrl )
            {
                wxString str = aTextCtrl->GetValue();
                long long int temp = EDA_UNIT_UTILS::UI::ValueFromString( scale, m_lastUnits, str );
                str = EDA_UNIT_UTILS::UI::StringFromValue( scale, newUnits, temp, true );

                // Don't use SetValue(); we don't want a bunch of event propagation as the actual
                // value hasn't changed, only its presentation.
                aTextCtrl->ChangeValue( str );
            };

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = ui_item.m_Item;

        if( item->IsThicknessEditable() && item->IsEnabled() )
            convert( static_cast<wxTextCtrl*>( ui_item.m_ThicknessCtrl ) );
    }

    convert( m_tcCTValue );

    for( IMPEDANCE_ROW& row : m_impedanceRows )
    {
        convert( row.m_gap );

        if( row.m_width->GetValue() != wxT( "—" ) )
        {
            const long long width = EDA_UNIT_UTILS::UI::ValueFromString(
                    scale, m_lastUnits, row.m_width->GetValue() );
            row.m_width->ChangeValue(
                    EDA_UNIT_UTILS::UI::StringFromValue( scale, newUnits, width, false ) );
        }
    }

    m_lastUnits = newUnits;

    if( m_impedanceWidthHeading )
    {
        const wxString units = EDA_UNIT_UTILS::GetText( newUnits ).Trim( false );
        m_impedanceWidthHeading->SetLabel( wxString::Format( _( "W (%s)" ), units ) );
    }

    event.Skip();
}


void PANEL_SETUP_BOARD_STACKUP::onCopperLayersSelCount( wxCommandEvent& event )
{
    int oldBoardWidth = static_cast<int>( m_frame->ValueFromString( m_tcCTValue->GetValue() ) );
    updateCopperLayerCount();
    showOnlyActiveLayers();
    updateIconColor();
    setDefaultLayerWidths( oldBoardWidth );
    computeBoardThickness();
    rebuildImpedanceRows();
    Layout();
}


void PANEL_SETUP_BOARD_STACKUP::onAdjustDielectricThickness( wxCommandEvent& event )
{
    // The list of items that can be modified:
    std::vector< BOARD_STACKUP_ROW_UI_ITEM* > items_candidate;

    // Some dielectric layers can have a locked thickness, so calculate the min
    // acceptable thickness
    int min_thickness = 0;

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = ui_item.m_Item;

        if( !item->IsThicknessEditable() || !ui_item.m_isEnabled )
            continue;

        // We are looking for locked thickness items only:
        wxCheckBox* cb_box = dynamic_cast<wxCheckBox*> ( ui_item.m_ThicknessLockCtrl );

        if( cb_box && !cb_box->GetValue() )
        {
            items_candidate.push_back( &ui_item );
            continue;
        }

        wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_ThicknessCtrl );

        int item_thickness = m_frame->ValueFromString( textCtrl->GetValue() );
        min_thickness += item_thickness;
    }

    wxString title;

    if( min_thickness == 0 )
    {
        title.Printf( _( "Enter board thickness in %s:" ),
                      EDA_UNIT_UTILS::GetText( m_frame->GetUserUnits() ).Trim( false ) );
    }
    else
    {
        title.Printf( _( "Enter expected board thickness (min value %s):" ),
                      m_frame->StringFromValue( min_thickness, true ) );
    }

    wxTextEntryDialog dlg( this, title, _( "Adjust Unlocked Dielectric Layers" ) );

    if( dlg.ShowModal() != wxID_OK )
        return;

    int iu_thickness = m_frame->ValueFromString( dlg.GetValue() );

    if( iu_thickness < min_thickness )
    {
        wxMessageBox( wxString::Format( _("Value too small (min value %s)." ),
                                        m_frame->StringFromValue( min_thickness, true ) ) );
        return;
    }

    // Now adjust not locked dielectric thickness layers:

    if( items_candidate.size() )
        setDefaultLayerWidths( iu_thickness );
    else
        wxMessageBox( _( "All dielectric  thickness layers are locked" ) );

    computeBoardThickness();
    rebuildImpedanceRows();
}


void PANEL_SETUP_BOARD_STACKUP::disconnectEvents()
{
	// Disconnect Events connected to items in m_controlItemsList
    for( wxControl* item: m_controlItemsList )
    {
        wxBitmapComboBox* cb = dynamic_cast<wxBitmapComboBox*>( item );

        if( cb )
        {
            cb->Disconnect( wxEVT_COMMAND_COMBOBOX_SELECTED,
                            wxCommandEventHandler( PANEL_SETUP_BOARD_STACKUP::onColorSelected ),
                            nullptr, this );
        }

        wxButton* matButt = dynamic_cast<wxButton*>( item );

        if( matButt )
        {
            matButt->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED,
                                 wxCommandEventHandler( PANEL_SETUP_BOARD_STACKUP::onMaterialChange ),
                                 nullptr, this );
        }

        wxTextCtrl* textCtrl = dynamic_cast<wxTextCtrl*>( item );

        if( textCtrl )
        {
            textCtrl->Disconnect( wxEVT_COMMAND_TEXT_UPDATED,
                                  wxCommandEventHandler( PANEL_SETUP_BOARD_STACKUP::onThicknessChange ),
                                  nullptr, this );
        }
    }
}


void PANEL_SETUP_BOARD_STACKUP::onAddDielectricLayer( wxCommandEvent& event )
{
    wxArrayString headers;
    headers.Add( _( "Layers" ) );

    // Build Dielectric layers list:
    std::vector<wxArrayString> d_list;
    std::vector<int>           rows;  // indexes of row values for each selectable item
    int                        row = -1;

    for( BOARD_STACKUP_ROW_UI_ITEM& item : m_rowUiItemsList )
    {
        row++;

        if( !item.m_isEnabled )
            continue;

        BOARD_STACKUP_ITEM* brd_stackup_item = item.m_Item;

        if( brd_stackup_item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            wxArrayString d_item;

            if( brd_stackup_item->GetSublayersCount() > 1 )
            {
                d_item.Add( wxString::Format( _( "Layer '%s' (sublayer %d/%d)" ),
                                              brd_stackup_item->FormatDielectricLayerName(),
                                              item.m_SubItem+1,
                                              brd_stackup_item->GetSublayersCount() ) );
            }
            else
            {
                d_item.Add( brd_stackup_item->FormatDielectricLayerName() );
            }

            d_list.emplace_back( d_item );
            rows.push_back( row );
        }
    }

    EDA_LIST_DIALOG dlg( PAGED_DIALOG::GetDialog( this ), _( "Add Dielectric Layer" ),
                         headers, d_list, wxEmptyString,
                         false /* do not sort the list: it is **expected** in stackup order */ );
    dlg.SetListLabel( _( "Select layer to add:" ) );
    dlg.HideFilter();

    if( dlg.ShowModal() == wxID_OK && dlg.GetSelection() >= 0 )
    {
        row = rows[ dlg.GetSelection() ];

        BOARD_STACKUP_ITEM* brd_stackup_item = m_rowUiItemsList[row].m_Item;
        int new_sublayer = m_rowUiItemsList[row].m_SubItem;

        // Insert a new item after the selected item
        brd_stackup_item->AddDielectricPrms( new_sublayer+1 );

        rebuildLayerStackPanel();
        computeBoardThickness();
        updateAllImpedanceRows();
    }
}


void PANEL_SETUP_BOARD_STACKUP::onRemoveDielectricLayer( wxCommandEvent& event )
{
    wxArrayString headers;
    headers.Add( _( "Layers" ) );

    // Build deletable Dielectric layers list.
    // A layer can be deleted if there are 2 (or more) dielectric sub-layers
    // between 2 copper layers
    std::vector<wxArrayString> d_list;
    std::vector<int>           rows;      // indexes of row values for each selectable item
    int                        row = 0;   // row index in m_rowUiItemsList of items in choice list

    // Build the list of dielectric layers:
    for( BOARD_STACKUP_ITEM* item : m_stackup.GetList() )
    {
        if( !item->IsEnabled() || item->GetType() != BS_ITEM_TYPE_DIELECTRIC ||
            item->GetSublayersCount() <= 1 )
        {
            row++;
            continue;
        }

        for( int ii = 0; ii < item->GetSublayersCount(); ii++ )
        {
            wxArrayString d_item;

            d_item.Add( wxString::Format( _( "Layer '%s' sublayer %d/%d" ),
                                          item->FormatDielectricLayerName(),
                                          ii+1,
                                          item->GetSublayersCount() ) );

            d_list.emplace_back( d_item );
            rows.push_back( row++ );
        }
    }

    EDA_LIST_DIALOG dlg( PAGED_DIALOG::GetDialog( this ), _( "Remove Dielectric Layer" ),
                         headers, d_list, wxEmptyString,
                         false /* do not sort the list: it is **expected** in stackup order */ );
    dlg.SetListLabel( _( "Select layer to remove:" ) );
    dlg.HideFilter();

    if( dlg.ShowModal() == wxID_OK && dlg.GetSelection() >= 0 )
    {
        row = rows[ dlg.GetSelection() ];
        BOARD_STACKUP_ITEM* brd_stackup_item = m_rowUiItemsList[ row ].m_Item;
        int                 sublayer = m_rowUiItemsList[ row ].m_SubItem;

        // Remove the selected sub item for the selected dielectric layer
        brd_stackup_item->RemoveDielectricPrms( sublayer );

        rebuildLayerStackPanel();
        computeBoardThickness();
        updateAllImpedanceRows();
    }
}


void PANEL_SETUP_BOARD_STACKUP::onRemoveDielUI( wxUpdateUIEvent& event )
{
    // The m_buttonRemoveDielectricLayer wxButton is enabled only if a dielectric
    // layer can be removed, i.e. if dielectric layers have sublayers
    for( BOARD_STACKUP_ITEM* item : m_stackup.GetList() )
    {
        if( !item->IsEnabled() || item->GetType() != BS_ITEM_TYPE_DIELECTRIC )
           continue;

        if( item->GetSublayersCount() > 1 )
        {
            event.Enable( true );
            return;
        }
    }

    event.Enable( false );
}


void PANEL_SETUP_BOARD_STACKUP::onExportToClipboard( wxCommandEvent& event )
{
    if( !transferDataFromUIToStackup() )
        return;

    m_panelFinish->TransferDataFromWindow( m_stackup );

    // Build a ASCII representation of stackup and copy it in the clipboard
    wxString report = BuildStackupReport( m_stackup, m_frame->GetUserUnits() );

    wxLogNull doNotLog; // disable logging of failed clipboard actions

    if( wxTheClipboard->Open() )
    {
        // This data objects are held by the clipboard,
        // so do not delete them in the app.
        wxTheClipboard->SetData( new wxTextDataObject( report ) );
        wxTheClipboard->Flush(); // Allow data to be available after closing KiCad
        wxTheClipboard->Close();
    }
}


wxColor PANEL_SETUP_BOARD_STACKUP::GetSelectedColor( int aRow ) const
{
    const BOARD_STACKUP_ROW_UI_ITEM& row = m_rowUiItemsList[aRow];
    const BOARD_STACKUP_ITEM*        item = row.m_Item;
    const wxBitmapComboBox*          choice = dynamic_cast<wxBitmapComboBox*>( row.m_ColorCtrl );
    int                              idx = choice ? choice->GetSelection() : 0;

    if( IsCustomColorIdx( item->GetType(), idx ) )
        return m_rowUiItemsList[aRow].m_UserColor.ToColour();
    else
        return GetStandardColor( item->GetType(), idx ).ToColour();
}


void PANEL_SETUP_BOARD_STACKUP::setDefaultLayerWidths( int targetThickness )
{
    // This function tries to set the PCB thickness to the parameter and uses a fixed prepreg thickness
    // of 0.1 mm. The core thickness is calculated accordingly as long as it also stays above 0.1mm.
    // If the core thickness would be smaller than the default pregreg thickness given here,
    // both are reduced towards zero to arrive at the correct PCB width
    const int prePregDefaultThickness = pcbIUScale.mmToIU( 0.1 );

    int copperLayerCount = GetCopperLayerCount();

    // This code is for a symmetrical PCB stackup with even copper layer count
    // If asymmetric stackups were to be implemented, the following layer count calculations
    // for dielectric/core layers might need adjustments.
    wxASSERT( copperLayerCount % 2 == 0 );

    int  dielectricLayerCount = copperLayerCount - 1;
    int  coreLayerCount = copperLayerCount / 2 - 1;

    wxASSERT( dielectricLayerCount > 0 );

    bool currentLayerIsCore = false;

    // start with prepreg layer on the outside, except when creating two-layer-board
    if( copperLayerCount == 2 )
    {
        coreLayerCount = 1;
        currentLayerIsCore = true;
    }

    wxASSERT( coreLayerCount > 0 );

    int prePregLayerCount = dielectricLayerCount - coreLayerCount;

    int totalWidthOfFixedItems = 0;

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = ui_item.m_Item;

        if( !item->IsThicknessEditable() || !ui_item.m_isEnabled )
            continue;

        wxCheckBox* cbLock = dynamic_cast<wxCheckBox*>( ui_item.m_ThicknessLockCtrl );
        wxChoice*   layerType = dynamic_cast<wxChoice*>( ui_item.m_LayerTypeCtrl );

        if( ( item->GetType() == BS_ITEM_TYPE_DIELECTRIC && !layerType )
            || item->GetType() == BS_ITEM_TYPE_SOLDERMASK
            || item->GetType() == BS_ITEM_TYPE_COPPER
            || ( cbLock && cbLock->GetValue() ) )
        {
            // secondary dielectric layers, mask and copper layers and locked layers will be
            // counted as fixed width
            wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_ThicknessCtrl );
            int         item_thickness = m_frame->ValueFromString( textCtrl->GetValue() );

            totalWidthOfFixedItems += item_thickness;
        }
    }

    // Width that hasn't been allocated by fixed items
    int remainingWidth = targetThickness
                            - totalWidthOfFixedItems
                            - ( prePregDefaultThickness * prePregLayerCount );

    int prePregThickness = prePregDefaultThickness;
    int coreThickness = remainingWidth / coreLayerCount;

    if( remainingWidth <= 0 )
    {
        // Locked manufacturer presets may already consume the requested board thickness.
        // When more copper layers are added, keep the new dielectrics physically usable and
        // allow the resulting board thickness to grow instead of creating 0-thickness layers.
        prePregThickness = coreThickness = prePregDefaultThickness;
    }
    else if( coreThickness < prePregThickness )
    {
        // There's not enough room for prepreg and core layers of at least 0.1 mm, so adjust both down
        remainingWidth = targetThickness - totalWidthOfFixedItems;
        prePregThickness = coreThickness = std::max( 0, remainingWidth / dielectricLayerCount );
    }

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = ui_item.m_Item;

        if( item->GetType() != BS_ITEM_TYPE_DIELECTRIC || !ui_item.m_isEnabled )
            continue;

        wxChoice* layerType = dynamic_cast<wxChoice*>( ui_item.m_LayerTypeCtrl );

        if( !layerType )
        {
            // ignore secondary dielectric layers
            continue;
        }

        wxCheckBox* cbLock = dynamic_cast<wxCheckBox*>( ui_item.m_ThicknessLockCtrl );

        if( cbLock && cbLock->GetValue() )
        {
            currentLayerIsCore = !currentLayerIsCore;

            // Don't override width of locked layer
            continue;
        }

        int layerThickness = currentLayerIsCore ? coreThickness : prePregThickness;

        wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_ThicknessCtrl );
        layerType->SetSelection( currentLayerIsCore ? 0 : 1 );
        textCtrl->ChangeValue( m_frame->StringFromValue( layerThickness ) );
        item->SetThickness( layerThickness, ui_item.m_SubItem );

        currentLayerIsCore = !currentLayerIsCore;
    }

    updateStackupRowColors();
}


int PANEL_SETUP_BOARD_STACKUP::computeBoardThickness()
{
    int thickness = 0;

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = ui_item.m_Item;

        if( !item->IsThicknessEditable() || !ui_item.m_isEnabled )
            continue;

        wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_ThicknessCtrl );
        int         item_thickness = m_frame->ValueFromString( textCtrl->GetValue() );

        thickness += item_thickness;
    }

    wxString thicknessStr = m_frame->StringFromValue( thickness, true );

    // The text in the event will translate to the value for the text control
    // and is only updated if it changed
    m_tcCTValue->ChangeValue( thicknessStr );
    return thickness;
}


int PANEL_SETUP_BOARD_STACKUP::GetCopperLayerCount() const
{
    return ( m_choiceCopperLayers->GetSelection() + 1 ) * 2;
}


void PANEL_SETUP_BOARD_STACKUP::updateCopperLayerCount()
{
    const int copperCount = GetCopperLayerCount();

    wxASSERT( copperCount >= 2 );

    m_enabledLayers.ClearCopperLayers();
    m_enabledLayers |= LSET::AllCuMask( copperCount );
}


void PANEL_SETUP_BOARD_STACKUP::synchronizeWithBoard( bool aFullSync )
{
    const BOARD_STACKUP&   brd_stackup = m_brdSettings->GetStackupDescriptor();

    if( aFullSync )
    {
        m_choiceCopperLayers->SetSelection( ( m_board->GetCopperLayerCount() / 2 ) - 1 );
        m_impedanceControlled->SetValue( brd_stackup.m_HasDielectricConstrains );
    }

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_row_item : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = ui_row_item.m_Item;
        int sub_item = ui_row_item.m_SubItem;

        if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            wxChoice* choice = dynamic_cast<wxChoice*>( ui_row_item.m_LayerTypeCtrl );

            if( choice )
                choice->SetSelection( item->GetTypeName() == KEY_CORE ? 0 : 1 );
        }

        if( item->IsMaterialEditable() )
        {
            wxTextCtrl* matName = dynamic_cast<wxTextCtrl*>( ui_row_item.m_MaterialCtrl );

            if( matName )
            {
                if( IsPrmSpecified( item->GetMaterial( sub_item ) ) )
                    matName->ChangeValue( item->GetMaterial( sub_item ) );
                else
                    matName->ChangeValue( wxGetTranslation( NotSpecifiedPrm() ) );
            }
        }

        if( item->IsThicknessEditable() )
        {
            wxTextCtrl* textCtrl = dynamic_cast<wxTextCtrl*>( ui_row_item.m_ThicknessCtrl );

            if( textCtrl )
                textCtrl->ChangeValue( m_frame->StringFromValue( item->GetThickness( sub_item ), true ) );

            if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
            {
                wxCheckBox* cb_box = dynamic_cast<wxCheckBox*> ( ui_row_item.m_ThicknessLockCtrl );

                if( cb_box )
                    cb_box->SetValue( item->IsThicknessLocked( sub_item ) );
            }
        }

        if( item->IsColorEditable() )
        {
            auto bm_combo = dynamic_cast<wxBitmapComboBox*>( ui_row_item.m_ColorCtrl );
            int  selected = 0;  // The "not specified" item

            if( item->GetColor( sub_item ).StartsWith( wxT( "#" ) ) )  // User defined color
            {
                COLOR4D custom_color( item->GetColor( sub_item ) );

                ui_row_item.m_UserColor = custom_color;

                selected = GetColorUserDefinedListIdx( item->GetType() );

                if( bm_combo )      // Update user color shown in the wxBitmapComboBox
                {
                    bm_combo->SetString( selected, item->GetColor( sub_item ) );
                    wxBitmap layerbmp( m_colorSwatchesSize.x, m_colorSwatchesSize.y );
                    LAYER_PRESENTATION::DrawColorSwatch( layerbmp, COLOR4D(), custom_color );
                    bm_combo->SetItemBitmap( selected, layerbmp );
                }
            }
            else
            {
                if( bm_combo )
                {
                    // Note: don't use bm_combo->FindString() because the combo strings are
                    // translated.
                    for( size_t ii = 0; ii < GetStandardColors( item->GetType() ).size(); ii++ )
                    {
                        if( GetStandardColorName( item->GetType(), ii ) == item->GetColor( sub_item ) )
                        {
                            selected = ii;
                            break;
                        }
                    }
                }
            }

            if( bm_combo )
                bm_combo->SetSelection( selected );
        }

        if( item->HasEpsilonRValue() )
        {
            wxString txt = UIDouble2Str( item->GetEpsilonR( sub_item ) );
            wxTextCtrl* textCtrl = dynamic_cast<wxTextCtrl*>( ui_row_item.m_EpsilonCtrl );

            if( textCtrl )
                textCtrl->ChangeValue( txt );
        }

        if( item->HasLossTangentValue() )
        {
            wxString txt = UIDouble2Str( item->GetLossTangent( sub_item ) );
            wxTextCtrl* textCtrl = dynamic_cast<wxTextCtrl*>( ui_row_item.m_LossTgCtrl );

            if( textCtrl )
                textCtrl->ChangeValue( txt );
        }
    }

    // Now enable/disable stackup items, according to the m_enabledLayers config
    showOnlyActiveLayers();

    updateIconColor();
}


void PANEL_SETUP_BOARD_STACKUP::showOnlyActiveLayers()
{
    // Now enable/disable stackup items, according to the m_enabledLayers config
    // Calculate copper layer count from m_enabledLayers, and *do not use* brd_stackup
    // for that, because it is not necessary up to date
    // (for instance after modifying the layer count from the panel layers in dialog)
    LSET copperMask = m_enabledLayers & ( LSET::ExternalCuMask() | LSET::InternalCuMask() );
    int copperLayersCount = copperMask.count();
    int  pos = 0;

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_row_item: m_rowUiItemsList )
    {
        bool show_item;
        BOARD_STACKUP_ITEM* item = ui_row_item.m_Item;

        if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
            // the m_DielectricLayerId is not a copper layer id, it is a dielectric idx from 1
            show_item = item->GetDielectricLayerId() < copperLayersCount;
        else
            show_item = m_enabledLayers[item->GetBrdLayerId()];

        item->SetEnabled( show_item );

        ui_row_item.m_isEnabled = show_item;

        if( show_item )
        {
            // pre-increment (ie: before calling lazyBuildRowUI) to account for header row
            pos += 9;
        }

        if( show_item && !ui_row_item.m_Icon )
            lazyBuildRowUI( ui_row_item, pos );

        if( ui_row_item.m_Icon )
        {
            // Show or not items of this row:
            ui_row_item.m_Background->Show( show_item );
            ui_row_item.m_Icon->Show( show_item );
            ui_row_item.m_LayerName->Show( show_item );
            ui_row_item.m_LayerTypeCtrl->Show( show_item );
            ui_row_item.m_MaterialCtrl->Show( show_item );

            if( ui_row_item.m_MaterialButt )
                ui_row_item.m_MaterialButt->Show( show_item );

            ui_row_item.m_ThicknessCtrl->Show( show_item );
            ui_row_item.m_ThicknessLockCtrl->Show( show_item );
            ui_row_item.m_ColorCtrl->Show( show_item );
            ui_row_item.m_EpsilonCtrl->Show( show_item );
            ui_row_item.m_LossTgCtrl->Show( show_item );
        }
    }
}


wxControl* PANEL_SETUP_BOARD_STACKUP::addSpacer( int aPos )
{
    wxStaticText* emptyText = new wxStaticText( m_scGridWin, wxID_ANY, wxEmptyString );
    m_fgGridSizer->Insert( aPos, emptyText, 0, wxALIGN_CENTER_VERTICAL );
    return emptyText;
}


void PANEL_SETUP_BOARD_STACKUP::lazyBuildRowUI( BOARD_STACKUP_ROW_UI_ITEM& ui_row_item,
                                                int aPos )
{
    BOARD_STACKUP_ITEM* item = ui_row_item.m_Item;
    int                 sublayerIdx = ui_row_item.m_SubItem;
    int                 row = ui_row_item.m_Row;

    ui_row_item.m_Background = new wxPanel( m_scGridWin );
    ui_row_item.m_Background->SetName( _( "Stackup layer row" ) );
    ui_row_item.m_Background->SetToolTip(
            _( "Editable physical stackup layer. The row color identifies its material type." ) );
    ui_row_item.m_Background->Lower();

    // Add color swatch icon. The color will be updated later,
    // when all widgets are initialized
    wxStaticBitmap* bitmap = new wxStaticBitmap( m_scGridWin, wxID_ANY, wxNullBitmap );
    m_fgGridSizer->Insert( aPos++, bitmap, 0, wxRIGHT|wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT, 4 );
    ui_row_item.m_Icon = bitmap;

    if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
    {
        wxString lname = item->FormatDielectricLayerName();

        if( item->GetSublayersCount() > 1 )
        {
            lname <<  wxT( "  (" ) << sublayerIdx +1 << wxT( "/" )
                                   << item->GetSublayersCount() << wxT( ")" );
        }

        wxStaticText* st_text = new wxStaticText( m_scGridWin, wxID_ANY, lname );
        m_fgGridSizer->Insert( aPos++, st_text, 0, wxRIGHT|wxALIGN_CENTER_VERTICAL, 2 );
        ui_row_item.m_LayerName = st_text;

        // For a dielectric layer, the layer type choice is not for each sublayer,
        // only for the first (sublayerIdx = 0), and is common to all sublayers
        if( sublayerIdx == 0 )
        {
            wxChoice* choice = new wxChoice( m_scGridWin, wxID_ANY, wxDefaultPosition,
                                             wxDefaultSize, m_core_prepreg_choice );
            choice->SetSelection( item->GetTypeName() == KEY_CORE ? 0 : 1 );
            choice->Bind( wxEVT_CHOICE,
                          [this]( wxCommandEvent& ) { updateStackupRowColors(); } );
            m_fgGridSizer->Insert( aPos++, choice, 1, wxEXPAND|wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL, 2 );

            ui_row_item.m_LayerTypeCtrl = choice;
        }
        else
        {
            ui_row_item.m_LayerTypeCtrl = addSpacer( aPos++ );
        }
    }
    else
    {
        item->SetLayerName( m_board->GetLayerName( item->GetBrdLayerId() ) );
        wxStaticText* st_text =  new wxStaticText( m_scGridWin, wxID_ANY, item->GetLayerName() );
        m_fgGridSizer->Insert( aPos++, st_text, 0, wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL, 1 );
        st_text->Show( true );
        ui_row_item.m_LayerName = st_text;

        wxString lname;

        if( item->GetTypeName() == KEY_COPPER )
            lname = _( "Copper" );
        else
            lname = wxGetTranslation( item->GetTypeName() );

        st_text = new wxStaticText( m_scGridWin, wxID_ANY, lname );
        m_fgGridSizer->Insert( aPos++, st_text, 0, wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL, 2 );
        ui_row_item.m_LayerTypeCtrl = st_text;
    }

    if( item->IsMaterialEditable() )
    {
        wxString matName = item->GetMaterial( sublayerIdx );

        wxBoxSizer* bSizerMat = new wxBoxSizer( wxHORIZONTAL );
       	m_fgGridSizer->Insert( aPos++, bSizerMat, 1, wxRIGHT|wxEXPAND, 4 );
        wxTextCtrl* textCtrl = new wxTextCtrl( m_scGridWin, wxID_ANY );

        if( IsPrmSpecified( matName ) )
            textCtrl->ChangeValue( matName );
        else
            textCtrl->ChangeValue( wxGetTranslation( NotSpecifiedPrm() ) );

        textCtrl->SetMinSize( m_numericTextCtrlSize );
        textCtrl->Bind( wxEVT_TEXT,
                        [this]( wxCommandEvent& ) { updateStackupRowColors(); } );
       	bSizerMat->Add( textCtrl, 0, wxALIGN_CENTER_VERTICAL|wxLEFT, 5 );

       	wxButton* m_buttonMat = new wxButton( m_scGridWin, ID_ITEM_MATERIAL+row, _( "..." ),
                                              wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
       	bSizerMat->Add( m_buttonMat, 0, wxALIGN_CENTER_VERTICAL, 2 );

        m_buttonMat->Connect( wxEVT_COMMAND_BUTTON_CLICKED,
                              wxCommandEventHandler( PANEL_SETUP_BOARD_STACKUP::onMaterialChange ),
                              nullptr, this );
        m_controlItemsList.push_back( m_buttonMat );

        ui_row_item.m_MaterialCtrl = textCtrl;
        ui_row_item.m_MaterialButt = m_buttonMat;

    }
    else
    {
        ui_row_item.m_MaterialCtrl = addSpacer( aPos++ );
    }

    if( item->IsThicknessEditable() )
    {
        wxTextCtrl* textCtrl = new wxTextCtrl( m_scGridWin, ID_ITEM_THICKNESS+row );
        textCtrl->SetMinSize( m_numericTextCtrlSize );
        textCtrl->ChangeValue( m_frame->StringFromValue( item->GetThickness( sublayerIdx ), true ) );
        m_fgGridSizer->Insert( aPos++, textCtrl, 0, wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL, 2 );
        m_controlItemsList.push_back( textCtrl );
        textCtrl->Connect( wxEVT_COMMAND_TEXT_UPDATED,
                           wxCommandEventHandler( PANEL_SETUP_BOARD_STACKUP::onThicknessChange ),
                           nullptr, this );
        ui_row_item.m_ThicknessCtrl = textCtrl;

        if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            wxCheckBox* cb_box = new wxCheckBox( m_scGridWin, ID_ITEM_THICKNESS_LOCKED+row,
                                                 wxEmptyString );
            cb_box->SetValue( item->IsThicknessLocked( sublayerIdx ) );

            m_fgGridSizer->Insert( aPos++, cb_box, 0,
                                   wxALIGN_CENTER_VERTICAL | wxALIGN_CENTER_HORIZONTAL, 2 );

            ui_row_item.m_ThicknessLockCtrl = cb_box;
        }
        else
        {
            ui_row_item.m_ThicknessLockCtrl = addSpacer( aPos++);
        }
    }
    else
    {
        ui_row_item.m_ThicknessCtrl = addSpacer( aPos++ );
        ui_row_item.m_ThicknessLockCtrl = addSpacer( aPos++ );
    }

    if( item->IsColorEditable() )
    {
        if( item->GetColor( sublayerIdx ).StartsWith( wxT( "#" ) ) )  // User defined color
        {
            ui_row_item.m_UserColor = COLOR4D( item->GetColor( sublayerIdx ) ).ToColour();
        }
        else
            ui_row_item.m_UserColor = GetDefaultUserColor( item->GetType() );

        wxBitmapComboBox* bm_combo = createColorBox( item, row );
        int               selected = 0;     // The "not specified" item

        m_fgGridSizer->Insert( aPos++, bm_combo, 1, wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL|wxEXPAND, 2 );

        if( item->GetColor( sublayerIdx ).StartsWith( wxT( "#" ) ) )
        {
            selected = GetColorUserDefinedListIdx( item->GetType() );
            bm_combo->SetString( selected, item->GetColor( sublayerIdx ) );
        }
        else
        {
            // Note: don't use bm_combo->FindString() because the combo strings are translated.
            for( size_t ii = 0; ii < GetStandardColors( item->GetType() ).size(); ii++ )
            {
                if( GetStandardColorName( item->GetType(), ii ) == item->GetColor( sublayerIdx ) )
                {
                    selected = ii;
                    break;
                }
            }
        }

        bm_combo->SetSelection( selected );
        ui_row_item.m_ColorCtrl = bm_combo;
    }
    else
    {
        ui_row_item.m_ColorCtrl = addSpacer( aPos++ );
    }

    if( item->HasEpsilonRValue() )
    {
        wxString txt = UIDouble2Str( item->GetEpsilonR( sublayerIdx ) );
        wxTextCtrl* textCtrl = new wxTextCtrl( m_scGridWin, wxID_ANY, wxEmptyString,
                                               wxDefaultPosition, m_numericFieldsSize );
        textCtrl->ChangeValue( txt );
        textCtrl->Bind( wxEVT_TEXT,
                        &PANEL_SETUP_BOARD_STACKUP::onImpedanceParameterChanged, this );
        m_fgGridSizer->Insert( aPos++, textCtrl, 0, wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL, 2 );
        ui_row_item.m_EpsilonCtrl = textCtrl;
    }
    else
    {
        ui_row_item.m_EpsilonCtrl = addSpacer( aPos++ );
    }

    if( item->HasLossTangentValue() )
    {
        wxString txt = UIDouble2Str( item->GetLossTangent( sublayerIdx ) );;
        wxTextCtrl* textCtrl = new wxTextCtrl( m_scGridWin, wxID_ANY, wxEmptyString,
                                               wxDefaultPosition, m_numericFieldsSize );
        textCtrl->ChangeValue( txt );
        textCtrl->Bind( wxEVT_TEXT,
                        &PANEL_SETUP_BOARD_STACKUP::onImpedanceParameterChanged, this );
        m_fgGridSizer->Insert( aPos++, textCtrl, 0, wxLEFT|wxRIGHT|wxALIGN_CENTER_VERTICAL, 2 );
        ui_row_item.m_LossTgCtrl = textCtrl;
    }
    else
    {
        ui_row_item.m_LossTgCtrl = addSpacer( aPos++ );
    }
}


void PANEL_SETUP_BOARD_STACKUP::rebuildLayerStackPanel( bool aRelinkItems )
{
    wxWindowUpdateLocker locker( m_scGridWin );
    m_scGridWin->Hide();

    // Rebuild the stackup for the dialog, after dielectric parameters list is modified
    // (added/removed):

    // First, delete all ui objects, because wxID values will be no longer valid for many widgets
    disconnectEvents();
    m_controlItemsList.clear();

    // Delete widgets (handled by the wxPanel parent)
    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item: m_rowUiItemsList )
    {
        // This remove and delete the current ui_item.m_MaterialCtrl sizer
        if( ui_item.m_MaterialCtrl )
            ui_item.m_MaterialCtrl->SetSizer( nullptr );

        // Delete other widgets
        delete ui_item.m_Icon;             // Color icon in first column (column 1)
        delete ui_item.m_LayerName;        // string shown in column 2
        delete ui_item.m_LayerTypeCtrl;    // control shown in column 3
        delete ui_item.m_MaterialCtrl;     // control shown in column 4, with m_MaterialButt
        delete ui_item.m_MaterialButt;     // control shown in column 4, with m_MaterialCtrl
        delete ui_item.m_ThicknessCtrl;    // control shown in column 5
        delete ui_item.m_ThicknessLockCtrl;// control shown in column 6
        delete ui_item.m_ColorCtrl;        // control shown in column 7
        delete ui_item.m_EpsilonCtrl;      // control shown in column 8
        delete ui_item.m_LossTgCtrl;       // control shown in column 9
        delete ui_item.m_Background;       // full-row semantic color
    }

    m_rowUiItemsList.clear();

    // In order to recreate a clean grid layer list, we have to delete and
    // recreate the sizer m_fgGridSizer (just deleting items in this size is not enough)
    // therefore we also have to add the "old" title items to the newly recreated m_fgGridSizer:
	m_scGridWin->SetSizer( nullptr );   // This remove and delete the current m_fgGridSizer

    m_fgGridSizer = new wxFlexGridSizer( 0, 9, 0, 2 );
	m_fgGridSizer->SetFlexibleDirection( wxHORIZONTAL );
	m_fgGridSizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
	m_fgGridSizer->SetHGap( 6 );
	m_scGridWin->SetSizer( m_fgGridSizer );

    // Re-add "old" title items:
    const int sizer_flags = wxALIGN_CENTER_VERTICAL | wxALL | wxALIGN_CENTER_HORIZONTAL;
	m_fgGridSizer->Add( m_staticTextLayer, 0, sizer_flags, 2 );
	m_fgGridSizer->Add( m_staticTextType, 0, sizer_flags, 2 );
	m_fgGridSizer->Add( m_staticTextLayerId, 0, sizer_flags, 5 );
	m_fgGridSizer->Add( m_staticTextMaterial, 0, sizer_flags, 2 );
	m_fgGridSizer->Add( m_staticTextThickness, 0, sizer_flags, 2 );
	m_fgGridSizer->Add( m_bitmapLockThickness, 0, sizer_flags, 1 );
	m_fgGridSizer->Add( m_staticTextColor, 0, sizer_flags, 2 );
	m_fgGridSizer->Add( m_staticTextEpsilonR, 0, sizer_flags, 2 );
	m_fgGridSizer->Add( m_staticTextLossTg, 0, sizer_flags, 2 );


    // Now, rebuild the widget list from the new m_stackup items:
    buildLayerStackPanel( false, aRelinkItems );

    // Now enable/disable stackup items, according to the m_enabledLayers config
    showOnlyActiveLayers();

    updateIconColor();
    updateStackupRowColors();

    m_scGridWin->Layout();
    m_scGridWin->Show();
}


void PANEL_SETUP_BOARD_STACKUP::buildLayerStackPanel( bool aCreateInitialStackup,
                                                      bool aRelinkStackup )
{
    // Build a full stackup for the dialog, with a active copper layer count
    // = current board layer count to calculate a reasonable default stackup:
    if( aCreateInitialStackup || aRelinkStackup )
    {
        if( aCreateInitialStackup )
        {
            // Creates a BOARD_STACKUP with 32 copper layers.
            // extra layers will be hidden later.
            // but if the number of layer is changed in the dialog, the corresponding
            // widgets will be available with their previous values.
            m_stackup.BuildDefaultStackupList( nullptr, m_brdSettings->GetCopperLayerCount() );
        }

        const BOARD_STACKUP& brd_stackup = m_brdSettings->GetStackupDescriptor();

        // Now initialize all stackup items to the board values, when exist
        for( BOARD_STACKUP_ITEM* item: m_stackup.GetList() )
        {
            // Search for board settings:
            for( BOARD_STACKUP_ITEM* board_item: brd_stackup.GetList() )
            {
                if( item->GetBrdLayerId() != UNDEFINED_LAYER )
                {
                    if( item->GetBrdLayerId() == board_item->GetBrdLayerId() )
                    {
                        *item = *board_item;
                        break;
                    }
                }
                else    // dielectric layer: see m_DielectricLayerId for identification
                {
                    // Compare dielectric layer with dielectric layer
                    if( board_item->GetBrdLayerId() != UNDEFINED_LAYER )
                        continue;

                    if( item->GetDielectricLayerId() == board_item->GetDielectricLayerId() )
                    {
                        *item = *board_item;
                        break;
                    }
                }
            }
        }
    }

    int row = 0;

    for( BOARD_STACKUP_ITEM* item : m_stackup.GetList() )
    {
        for( int sub_idx = 0; sub_idx < item->GetSublayersCount(); sub_idx++ )
        {
            m_rowUiItemsList.emplace_back( item, sub_idx, row );
            row++;
        }
    }
}


// Transfer current UI settings to m_stackup but not to the board
bool PANEL_SETUP_BOARD_STACKUP::transferDataFromUIToStackup()
{
    wxString error_msg;
    bool success = true;
    double value;

    for( BOARD_STACKUP_ROW_UI_ITEM& ui_item : m_rowUiItemsList )
    {
        // Skip stackup items useless for the current board
        if( !ui_item.m_isEnabled )
        {
            continue;
        }

        BOARD_STACKUP_ITEM* item = ui_item.m_Item;
        int sub_item = ui_item.m_SubItem;

        // Add sub layer if there is a new sub layer:
        while( item->GetSublayersCount() <= sub_item )
            item->AddDielectricPrms( item->GetSublayersCount() );

        if( sub_item == 0 )     // Name only main layer
            item->SetLayerName( ui_item.m_LayerName->GetLabel() );

        if( item->HasEpsilonRValue() )
        {
            wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_EpsilonCtrl );
            wxString    txt = textCtrl->GetValue();

            if( txt.ToDouble( &value ) && value >= 0.0 )
                item->SetEpsilonR( value, sub_item );
            else if( txt.ToCDouble( &value ) && value >= 0.0 )
                item->SetEpsilonR( value, sub_item );
            else
            {
                success = false;
                error_msg << _( "Incorrect value for Epsilon R (Epsilon R must be positive or "
                                "null if not used)" );
            }
        }

        if( item->HasLossTangentValue() )
        {
            wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_LossTgCtrl );
            wxString    txt = textCtrl->GetValue();

            if( txt.ToDouble( &value ) && value >= 0.0 )
                item->SetLossTangent( value, sub_item );
            else if( txt.ToCDouble( &value ) && value >= 0.0 )
                item->SetLossTangent( value, sub_item );
            else
            {
                success = false;

                if( !error_msg.IsEmpty() )
                    error_msg << wxT( "\n" );

                error_msg << _( "Incorrect value for Loss tg (Loss tg must be positive or null "
                                "if not used)" );
            }
        }

        if( item->IsMaterialEditable() )
        {
            wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_MaterialCtrl );
            item->SetMaterial( textCtrl->GetValue(), sub_item );

            // Ensure the not specified mat name is the keyword, not its translation
            // to avoid any issue is the language setting changes
            if( !IsPrmSpecified( item->GetMaterial( sub_item ) ) )
                item->SetMaterial( NotSpecifiedPrm(), sub_item );
        }

        if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            // Choice is Core or Prepreg. Sublayers have no choice:
            wxChoice* choice = dynamic_cast<wxChoice*>( ui_item.m_LayerTypeCtrl );

            if( choice )
            {
                int idx = choice->GetSelection();

                if( idx == 0 )
                    item->SetTypeName( KEY_CORE );
                else
                    item->SetTypeName( KEY_PREPREG );
            }
        }

        if( item->IsThicknessEditable() )
        {
            wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( ui_item.m_ThicknessCtrl );
            int         new_thickness = m_frame->ValueFromString( textCtrl->GetValue() );

            item->SetThickness( new_thickness, sub_item );

            if( new_thickness < 0 )
            {
                success = false;

                if( !error_msg.IsEmpty() )
                    error_msg << wxT( "\n" );

                error_msg << _( "A layer thickness is < 0. Fix it" );
            }

            if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
            {
                // Dielectric thickness layer can have a locked thickness:
                wxCheckBox* cb_box = static_cast<wxCheckBox*>( ui_item.m_ThicknessLockCtrl );
                item->SetThicknessLocked( cb_box && cb_box->GetValue(), sub_item );
            }
        }

        if( item->IsColorEditable() )
        {
            wxBitmapComboBox* choice = dynamic_cast<wxBitmapComboBox*>( ui_item.m_ColorCtrl );

            if( choice )
            {
                int idx = choice->GetSelection();

                if( IsCustomColorIdx( item->GetType(), idx ) )
                    item->SetColor( ui_item.m_UserColor.ToHexString(), sub_item );
                else
                    item->SetColor( GetStandardColorName( item->GetType(), idx ), sub_item );
            }
        }
    }

    if( !success )
    {
        wxMessageBox( error_msg, _( "Errors" ) );
        return false;
    }

    m_stackup.m_HasDielectricConstrains = m_impedanceControlled->GetValue();

    return true;
}


bool PANEL_SETUP_BOARD_STACKUP::TransferDataFromWindow()
{
    if( !transferDataFromUIToStackup() )
        return false;

    // NOTE: Copper layer count is transferred via PANEL_SETUP_LAYERS even though it is configured
    // on this page, because the logic for confirming deletion of board items on deleted layers is
    // on that panel and it doesn't make sense to split it up.

    BOARD_STACKUP& brd_stackup = m_brdSettings->GetStackupDescriptor();
    STRING_FORMATTER old_stackup;

    // FormatBoardStackup() (using FormatInternalUnits()) expects a "C" locale
    // to execute some tests. So switch to the suitable locale
    LOCALE_IO dummy;
    brd_stackup.FormatBoardStackup( &old_stackup, m_board );

    // copy enabled items to the new board stackup
    brd_stackup.RemoveAll();

    for( BOARD_STACKUP_ITEM* item : m_stackup.GetList() )
    {
        if( item->IsEnabled() )
            brd_stackup.Add( new BOARD_STACKUP_ITEM( *item ) );
    }

    STRING_FORMATTER new_stackup;
    brd_stackup.FormatBoardStackup( &new_stackup, m_board );

    bool modified = old_stackup.GetString() != new_stackup.GetString();
    int thickness = brd_stackup.BuildBoardThicknessFromStackup();

    if( m_brdSettings->GetBoardThickness() != thickness )
    {
        m_brdSettings->SetBoardThickness( thickness );
        modified = true;
    }

    if( brd_stackup.m_HasDielectricConstrains != m_impedanceControlled->GetValue() )
    {
        brd_stackup.m_HasDielectricConstrains = m_impedanceControlled->GetValue();
        modified = true;
    }

    if( !m_brdSettings->m_HasStackup )
    {
        m_brdSettings->m_HasStackup = true;
        modified = true;
    }

    const wxString projectSettings = serializeProjectImpedanceSettings();
    wxString& storedSettings = m_frame->Prj().GetProjectFile().m_BoardStackupControl;

    if( storedSettings != projectSettings )
    {
        storedSettings = projectSettings;
        modified = true;
    }

    if( modified )
        m_frame->OnModify();

    return true;
}


void PANEL_SETUP_BOARD_STACKUP::ImportSettingsFrom( BOARD* aBoard )
{
    BOARD* savedBrd = m_board;
    m_board = aBoard;

    BOARD_DESIGN_SETTINGS* savedSettings = m_brdSettings;
    m_brdSettings = &aBoard->GetDesignSettings();

    m_enabledLayers = m_board->GetEnabledLayers() & BOARD_STACKUP::StackupAllowedBrdLayers();

    rebuildLayerStackPanel( true );
    synchronizeWithBoard( true );
    computeBoardThickness();
    loadProjectImpedanceSettings();

    m_brdSettings = savedSettings;
    m_board = savedBrd;
}


void PANEL_SETUP_BOARD_STACKUP::OnLayersOptionsChanged( const LSET& aNewLayerSet )
{
    // Can be called spuriously from events before the layers page is even created
    if( !m_panelLayers->IsInitialized() )
        return;

    // First, verify the list of layers currently in stackup:
    // if it does not mach the list of layers set in PANEL_SETUP_LAYERS
    // rebuild the panel

    // the current enabled layers in PANEL_SETUP_LAYERS
    // Note: the number of layer can change, but not the layers properties
    LSET layersList = m_panelLayers->GetUILayerMask() & BOARD_STACKUP::StackupAllowedBrdLayers();

    if( m_enabledLayers != layersList )
    {
        m_enabledLayers = layersList;

        synchronizeWithBoard( false );
        rebuildImpedanceRows();

        Layout();
        Refresh();
    }
}


void PANEL_SETUP_BOARD_STACKUP::onColorSelected( wxCommandEvent& event )
{
    int                 idx = event.GetSelection();
    int                 item_id = event.GetId();
    int                 row = item_id - ID_ITEM_COLOR;
    BOARD_STACKUP_ITEM* item = m_rowUiItemsList[row].m_Item;

    if( IsCustomColorIdx( item->GetType(), idx ) )   // user color is the last option in list
    {
        DIALOG_COLOR_PICKER dlg( this, m_rowUiItemsList[row].m_UserColor, true, nullptr,
                                 GetDefaultUserColor( m_rowUiItemsList[row].m_Item->GetType() ) );

#ifdef __WXGTK__
        // Give a time-slice to close the menu before opening the dialog.
        // (Only matters on some versions of GTK.)
        wxSafeYield();
#endif

        if( dlg.ShowModal() == wxID_OK )
        {
            wxBitmapComboBox* combo = static_cast<wxBitmapComboBox*>( FindWindowById( item_id ) );
            COLOR4D           color = dlg.GetColor();

            m_rowUiItemsList[row].m_UserColor = color;

            combo->SetString( idx, color.ToHexString() );

            wxBitmap layerbmp( m_colorSwatchesSize.x, m_colorSwatchesSize.y );
            LAYER_PRESENTATION::DrawColorSwatch( layerbmp, COLOR4D( 0, 0, 0, 0 ), color );
            combo->SetItemBitmap( combo->GetCount() - 1, layerbmp );

            combo->SetSelection( idx );
        }
    }

    updateIconColor( row );
    updateStackupRowColors();
}


void PANEL_SETUP_BOARD_STACKUP::onMaterialChange( wxCommandEvent& event )
{
    // Ensure m_materialList contains all materials already in use in stackup list
    // and add it is missing
    if( !transferDataFromUIToStackup() )
        return;

    for( BOARD_STACKUP_ITEM* item : m_stackup.GetList() )
    {
        DIELECTRIC_SUBSTRATE_LIST* mat_list = nullptr;

        if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
            mat_list = &m_delectricMatList;
        else if( item->GetType() == BS_ITEM_TYPE_SOLDERMASK )
            mat_list = &m_solderMaskMatList;
        else if( item->GetType() == BS_ITEM_TYPE_SILKSCREEN )
            mat_list = &m_silkscreenMatList;

        else
            continue;

        for( int ii = 0; ii < item->GetSublayersCount(); ii++ )
        {
            int idx = mat_list->FindSubstrate( item->GetMaterial( ii ),
                                               item->GetEpsilonR( ii ),
                                               item->GetLossTangent( ii ) );

            if( idx < 0 && !item->GetMaterial().IsEmpty() )
            {
                // This material is not in list: add it
                DIELECTRIC_SUBSTRATE new_mat;
                new_mat.m_Name = item->GetMaterial( ii );
                new_mat.m_EpsilonR = item->GetEpsilonR( ii );
                new_mat.m_LossTangent = item->GetLossTangent( ii );
                mat_list->AppendSubstrate( new_mat );
            }
        }
    }

    int row  = event.GetId() - ID_ITEM_MATERIAL;
    BOARD_STACKUP_ITEM* item = m_rowUiItemsList[row].m_Item;
    int sub_item = m_rowUiItemsList[row].m_SubItem;
    DIELECTRIC_SUBSTRATE_LIST* item_mat_list = nullptr;

    switch( item->GetType() )
    {
    case BS_ITEM_TYPE_DIELECTRIC: item_mat_list = &m_delectricMatList;  break;
    case BS_ITEM_TYPE_SOLDERMASK: item_mat_list = &m_solderMaskMatList; break;
    case BS_ITEM_TYPE_SILKSCREEN: item_mat_list = &m_silkscreenMatList; break;
    default:                      item_mat_list = nullptr;              break;
    }

    wxCHECK( item_mat_list, /* void */ );

    DIALOG_DIELECTRIC_MATERIAL dlg( this, *item_mat_list );

    if( dlg.ShowModal() != wxID_OK )
        return;

    DIELECTRIC_SUBSTRATE substrate = dlg.GetSelectedSubstrate();

    if( substrate.m_Name.IsEmpty() )    // No substrate specified
        return;

    // Update Name, Epsilon R and Loss tg
    item->SetMaterial( substrate.m_Name, sub_item );
    item->SetEpsilonR( substrate.m_EpsilonR, sub_item );
    item->SetLossTangent( substrate.m_LossTangent, sub_item );

    wxTextCtrl* textCtrl = static_cast<wxTextCtrl*>( m_rowUiItemsList[row].m_MaterialCtrl );
    textCtrl->ChangeValue( item->GetMaterial( sub_item ) );

    if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC
            && !item->GetColor( sub_item ).StartsWith( "#" ) /* User defined color */ )
    {
        if( substrate.m_Name.IsSameAs( "PTFE" )
              || substrate.m_Name.IsSameAs( "Teflon" ) )
        {
            item->SetColor( "PTFE natural", sub_item );
        }
        else if( substrate.m_Name.IsSameAs( "Polyimide" )
              || substrate.m_Name.IsSameAs( "Kapton" ) )
        {
            item->SetColor( "Polyimide", sub_item );
        }
        else if( substrate.m_Name.IsSameAs( "Al" ) )
        {
            item->SetColor( "Aluminum", sub_item );
        }
        else
        {
            item->SetColor( "FR4 natural", sub_item );
        }
    }

    wxBitmapComboBox* picker = static_cast<wxBitmapComboBox*>( m_rowUiItemsList[row].m_ColorCtrl );

    for( size_t ii = 0; ii < GetStandardColors( item->GetType() ).size(); ii++ )
    {
        if( GetStandardColorName( item->GetType(), ii ) == item->GetColor( sub_item ) )
        {
            picker->SetSelection( ii );
            break;
        }
    }

    // some layers have a material choice but not EpsilonR ctrl
    if( item->HasEpsilonRValue() )
    {
        textCtrl = dynamic_cast<wxTextCtrl*>( m_rowUiItemsList[row].m_EpsilonCtrl );

        if( textCtrl )
            textCtrl->ChangeValue( item->FormatEpsilonR( sub_item ) );
    }

    // some layers have a material choice but not loss tg ctrl
    if( item->HasLossTangentValue() )
    {
        textCtrl = dynamic_cast<wxTextCtrl*>( m_rowUiItemsList[row].m_LossTgCtrl );

        if( textCtrl )
            textCtrl->ChangeValue( item->FormatLossTangent( sub_item ) );
    }

    updateAllImpedanceRows();
}


void PANEL_SETUP_BOARD_STACKUP::onThicknessChange( wxCommandEvent& event )
{
    int row  = event.GetId() - ID_ITEM_THICKNESS;
    wxString value = event.GetString();

    BOARD_STACKUP_ITEM* item = GetStackupItem( row );
    int idx = GetSublayerId( row );

    item->SetThickness( m_frame->ValueFromString( value ), idx );

    computeBoardThickness();
    scheduleImpedanceUpdate();
}


void PANEL_SETUP_BOARD_STACKUP::buildStackupPresetControls()
{
    wxStaticText* label = new wxStaticText( this, wxID_ANY, _( "Stackup preset:" ) );
    m_stackupPreset = new wxChoice( this, wxID_ANY, wxDefaultPosition,
                                    wxSize( FromDIP( 285 ), -1 ) );
    m_stackupPreset->SetToolTip(
            _( "Populate the layer count, copper, dielectric materials, thicknesses, and "
               "dielectric constants from a manufacturer stackup" ) );
    m_importStackupPreset = new wxButton( this, wxID_ANY, _( "Import..." ) );
    m_importStackupPreset->SetToolTip(
            _( "Import a KiCad Pro stackup preset from a JSON file" ) );

    // Place the preset with the other stackup-level actions, immediately after the
    // impedance-controlled checkbox and before the dielectric layer buttons.
    bTopSizer->Insert( 4, label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP( 10 ) );
    bTopSizer->Insert( 5, m_stackupPreset, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
                       FromDIP( 5 ) );
    bTopSizer->Insert( 6, m_importStackupPreset, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                       FromDIP( 5 ) );

    rebuildPresetChoices();
    m_stackupPreset->Bind( wxEVT_CHOICE, &PANEL_SETUP_BOARD_STACKUP::onApplyStackupPreset, this );
    m_importStackupPreset->Bind( wxEVT_BUTTON,
                                 &PANEL_SETUP_BOARD_STACKUP::onImportStackupPreset, this );
}


void PANEL_SETUP_BOARD_STACKUP::buildImpedancePanel()
{
    m_sizerStackup->SetOrientation( wxHORIZONTAL );

    m_impedancePanel = new wxPanel( this );
    m_impedancePanel->SetMinSize( wxSize( FromDIP( 350 ), -1 ) );

    wxStaticBoxSizer* panelSizer = new wxStaticBoxSizer( wxVERTICAL, m_impedancePanel,
                                                         _( "Controlled Impedance" ) );

    wxStaticText* help = new wxStaticText(
            panelSizer->GetStaticBox(), wxID_ANY,
            _( "Set the target for each signal layer. Widths use the copper and dielectric "
               "values currently shown in the stackup and update automatically. Manufacturer "
               "presets use 0.02 loss tangent where the source table does not specify one." ) );
    help->Wrap( FromDIP( 315 ) );
    panelSizer->Add( help, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 8 ) );

    m_impedanceGridWindow = new wxScrolledWindow( panelSizer->GetStaticBox(), wxID_ANY,
                                                   wxDefaultPosition, wxDefaultSize,
                                                   wxBORDER_NONE | wxHSCROLL | wxVSCROLL );
    m_impedanceGridWindow->SetScrollRate( FromDIP( 5 ), FromDIP( 5 ) );
    m_impedanceGrid = new wxFlexGridSizer( 0, 5, FromDIP( 4 ), FromDIP( 3 ) );
    m_impedanceGrid->AddGrowableCol( 1 );
    m_impedanceGridWindow->SetSizer( m_impedanceGrid );
    panelSizer->Add( m_impedanceGridWindow, 1, wxEXPAND | wxLEFT | wxRIGHT,
                     FromDIP( 6 ) );

    m_impedancePanel->SetSizer( panelSizer );
    m_sizerStackup->Add( m_impedancePanel, 0, wxEXPAND | wxLEFT, FromDIP( 10 ) );

    m_impedanceControlled->Bind( wxEVT_CHECKBOX,
                                 &PANEL_SETUP_BOARD_STACKUP::onImpedanceControlled, this );
    m_scGridWin->Bind( wxEVT_SIZE,
                       [this]( wxSizeEvent& aEvent )
                       {
                           aEvent.Skip();
                           CallAfter( &PANEL_SETUP_BOARD_STACKUP::layoutStackupRowBackgrounds );
                       } );
    rebuildImpedanceRows();
    updateImpedancePanelVisibility();
    updateStackupRowColors();
}


wxColor PANEL_SETUP_BOARD_STACKUP::getStackupRowColor(
        const BOARD_STACKUP_ROW_UI_ITEM& aRow ) const
{
    if( !aRow.m_Item )
        return wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );

    switch( aRow.m_Item->GetType() )
    {
    case BS_ITEM_TYPE_COPPER:
        return wxColour( 205, 126, 70 );

    case BS_ITEM_TYPE_DIELECTRIC:
    {
        bool isCore = aRow.m_Item->GetTypeName() == KEY_CORE;

        for( const BOARD_STACKUP_ROW_UI_ITEM& candidate : m_rowUiItemsList )
        {
            if( candidate.m_Item == aRow.m_Item )
            {
                if( const wxChoice* type = dynamic_cast<const wxChoice*>(
                            candidate.m_LayerTypeCtrl ) )
                {
                    isCore = type->GetSelection() == 0;
                    break;
                }
            }
        }

        return isCore ? wxColour( 112, 101, 73 ) : wxColour( 111, 128, 94 );
    }

    case BS_ITEM_TYPE_SOLDERMASK:
        return wxColour( 20, 126, 60 );

    case BS_ITEM_TYPE_SILKSCREEN:
        return wxColour( 225, 229, 234 );

    case BS_ITEM_TYPE_SOLDERPASTE:
        return wxColour( 150, 157, 166 );

    default:
        return wxColour( 100, 107, 116 );
    }
}


void PANEL_SETUP_BOARD_STACKUP::updateStackupRowColors()
{
    for( BOARD_STACKUP_ROW_UI_ITEM& row : m_rowUiItemsList )
    {
        if( !row.m_Background )
            continue;

        const wxColour background = getStackupRowColor( row );
        const double luminance = 0.2126 * background.Red() + 0.7152 * background.Green()
                                 + 0.0722 * background.Blue();
        const wxColour foreground = luminance > 145.0 ? wxColour( 24, 29, 36 ) : *wxWHITE;

        row.m_Background->SetBackgroundColour( background );

        for( wxControl* control : { static_cast<wxControl*>( row.m_Icon ),
                                    static_cast<wxControl*>( row.m_LayerName ),
                                    row.m_LayerTypeCtrl, row.m_MaterialCtrl,
                                    row.m_ThicknessCtrl, row.m_ThicknessLockCtrl,
                                    row.m_ColorCtrl, row.m_EpsilonCtrl, row.m_LossTgCtrl } )
        {
            if( wxStaticText* text = dynamic_cast<wxStaticText*>( control ) )
            {
                text->SetBackgroundColour( background );
                text->SetForegroundColour( control == row.m_LayerTypeCtrl ? *wxWHITE
                                                                         : foreground );
            }
            else if( wxStaticBitmap* bitmap = dynamic_cast<wxStaticBitmap*>( control ) )
            {
                bitmap->SetBackgroundColour( background );
            }
        }

        // Keep the layer classification visually consistent across every material band.
        // Dielectric rows use a wxChoice here while the other rows use static text.
        if( row.m_LayerTypeCtrl )
            row.m_LayerTypeCtrl->SetForegroundColour( *wxWHITE );

        row.m_Background->Refresh();
    }

    CallAfter( &PANEL_SETUP_BOARD_STACKUP::layoutStackupRowBackgrounds );
}


void PANEL_SETUP_BOARD_STACKUP::layoutStackupRowBackgrounds()
{
    if( !m_scGridWin || !m_fgGridSizer )
        return;

    m_scGridWin->Layout();
    const int contentWidth = std::max( m_scGridWin->GetClientSize().x,
                                       m_fgGridSizer->GetPosition().x
                                               + m_fgGridSizer->GetSize().x );

    for( BOARD_STACKUP_ROW_UI_ITEM& row : m_rowUiItemsList )
    {
        if( !row.m_Background || !row.m_isEnabled || !row.m_Icon )
            continue;

        int top = row.m_Icon->GetPosition().y;
        int bottom = top + row.m_Icon->GetSize().y;

        for( wxControl* control : { static_cast<wxControl*>( row.m_LayerName ),
                                    row.m_LayerTypeCtrl, row.m_MaterialCtrl,
                                    row.m_ThicknessCtrl, row.m_ThicknessLockCtrl,
                                    row.m_ColorCtrl, row.m_EpsilonCtrl, row.m_LossTgCtrl } )
        {
            if( control && control->IsShown() )
            {
                top = std::min( top, control->GetPosition().y );
                bottom = std::max( bottom, control->GetPosition().y + control->GetSize().y );
            }
        }

        const int padding = FromDIP( 2 );
        row.m_Background->SetSize( 0, top - padding, contentWidth,
                                   bottom - top + 2 * padding );
        row.m_Background->Lower();
    }

    m_scGridWin->Refresh();
}


void PANEL_SETUP_BOARD_STACKUP::rebuildPresetChoices()
{
    if( !m_stackupPreset )
        return;

    m_visibleStackupPresets.clear();
    m_stackupPreset->Clear();
    m_visibleStackupPresets.push_back( nullptr );
    m_stackupPreset->Append( _( "Current / custom stackup" ) );

    for( const STACKUP_PRESET& preset : getStackupPresets() )
    {
        m_visibleStackupPresets.push_back( &preset );
        m_stackupPreset->Append( wxString::Format( wxT( "%s — %s (%zu layers)" ),
                                                   preset.m_manufacturer, preset.m_name,
                                                   preset.m_copperThicknessMm.size() ) );
    }

    for( const STACKUP_PRESET& preset : m_importedStackupPresets )
    {
        m_visibleStackupPresets.push_back( &preset );
        m_stackupPreset->Append( wxString::Format( wxT( "%s — %s (%zu layers)" ),
                                                   preset.m_manufacturer, preset.m_name,
                                                   preset.m_copperThicknessMm.size() ) );
    }

    m_stackupPreset->SetSelection( 0 );
}


void PANEL_SETUP_BOARD_STACKUP::onApplyStackupPreset( wxCommandEvent& aEvent )
{
    const int selection = m_stackupPreset ? m_stackupPreset->GetSelection() : wxNOT_FOUND;

    if( selection > 0 && selection < static_cast<int>( m_visibleStackupPresets.size() ) )
    {
        m_activeStackupPreset = *m_visibleStackupPresets[selection];
        applyStackupPreset( *m_activeStackupPreset );
    }
    else
    {
        m_activeStackupPreset.reset();
    }

    aEvent.Skip();
}


void PANEL_SETUP_BOARD_STACKUP::applyStackupPreset( const STACKUP_PRESET& aPreset )
{
    const int copperCount = static_cast<int>( aPreset.m_copperThicknessMm.size() );

    wxCHECK( copperCount >= 2 && copperCount <= 32 && copperCount % 2 == 0, /* void */ );
    wxCHECK( aPreset.m_dielectrics.size() == static_cast<size_t>( copperCount - 1 ), /* void */ );

    m_choiceCopperLayers->SetSelection( copperCount / 2 - 1 );
    updateCopperLayerCount();
    m_panelLayers->SyncCopperLayers( copperCount );
    showOnlyActiveLayers();

    size_t copperIndex = 0;

    for( BOARD_STACKUP_ITEM* item : m_stackup.GetList() )
    {
        if( item->GetType() == BS_ITEM_TYPE_COPPER && item->IsEnabled() )
        {
            item->SetThickness( pcbIUScale.mmToIU( aPreset.m_copperThicknessMm[copperIndex++] ) );
            item->SetMaterial( aPreset.m_manufacturer + wxT( " copper" ) );
        }
        else if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC && item->IsEnabled() )
        {
            const size_t dielectricIndex = static_cast<size_t>( item->GetDielectricLayerId() - 1 );
            const std::vector<PRESET_DIELECTRIC>& layers = aPreset.m_dielectrics[dielectricIndex];

            while( item->GetSublayersCount() > static_cast<int>( layers.size() ) )
                item->RemoveDielectricPrms( item->GetSublayersCount() - 1 );

            while( item->GetSublayersCount() < static_cast<int>( layers.size() ) )
                item->AddDielectricPrms( item->GetSublayersCount() );

            const bool containsCore = std::any_of(
                    layers.begin(), layers.end(),
                    []( const PRESET_DIELECTRIC& aLayer ) { return aLayer.m_core; } );
            item->SetTypeName( containsCore ? KEY_CORE : KEY_PREPREG );

            for( size_t ii = 0; ii < layers.size(); ++ii )
            {
                const PRESET_DIELECTRIC& layer = layers[ii];
                item->SetMaterial( aPreset.m_manufacturer + wxT( " " ) + layer.m_material, ii );
                item->SetThickness( pcbIUScale.mmToIU( layer.m_thicknessMm ), ii );
                item->SetEpsilonR( layer.m_epsilonR, ii );
                item->SetLossTangent( layer.m_lossTangent, ii );
                item->SetThicknessLocked( true, ii );
            }
        }
    }

    m_impedanceControlled->SetValue( true );
    rebuildLayerStackPanel();
    computeBoardThickness();
    rebuildImpedanceRows();
    updateImpedancePanelVisibility();
    Layout();
}


std::string PANEL_SETUP_BOARD_STACKUP::serializeStackupPresetJson(
        const STACKUP_PRESET& aPreset ) const
{
    nlohmann::json root = {
        { "format", "kicad-pro-stackup" },
        { "version", 1 },
        { "manufacturer", std::string( aPreset.m_manufacturer.utf8_str() ) },
        { "name", std::string( aPreset.m_name.utf8_str() ) },
        { "copper_thickness_mm", aPreset.m_copperThicknessMm },
        { "dielectrics", nlohmann::json::array() }
    };

    for( const std::vector<PRESET_DIELECTRIC>& dielectric : aPreset.m_dielectrics )
    {
        nlohmann::json layers = nlohmann::json::array();

        for( const PRESET_DIELECTRIC& layer : dielectric )
        {
            layers.push_back( {
                { "type", layer.m_core ? "core" : "prepreg" },
                { "material", std::string( layer.m_material.utf8_str() ) },
                { "thickness_mm", layer.m_thicknessMm },
                { "epsilon_r", layer.m_epsilonR },
                { "loss_tangent", layer.m_lossTangent }
            } );
        }

        root["dielectrics"].push_back( std::move( layers ) );
    }

    return root.dump( 2 );
}


std::optional<PANEL_SETUP_BOARD_STACKUP::STACKUP_PRESET>
PANEL_SETUP_BOARD_STACKUP::parseStackupPresetJson( const std::string& aJson,
                                                    wxString& aError ) const
{
    try
    {
        const nlohmann::json root = nlohmann::json::parse( aJson );

        if( root.value( "format", "" ) != "kicad-pro-stackup" || root.value( "version", 0 ) != 1 )
        {
            aError = _( "The file is not a KiCad Pro stackup preset version 1." );
            return std::nullopt;
        }

        STACKUP_PRESET preset;
        preset.m_manufacturer = wxString::FromUTF8( root.at( "manufacturer" ).get<std::string>() );
        preset.m_name = wxString::FromUTF8( root.at( "name" ).get<std::string>() );
        preset.m_copperThicknessMm = root.at( "copper_thickness_mm" ).get<std::vector<double>>();

        if( preset.m_manufacturer.IsEmpty() || preset.m_name.IsEmpty() )
            throw std::runtime_error( "manufacturer and name must not be empty" );

        const size_t copperCount = preset.m_copperThicknessMm.size();

        if( copperCount < 2 || copperCount > 32 || copperCount % 2 != 0 )
            throw std::runtime_error( "copper_thickness_mm must contain 2 to 32 even-numbered layers" );

        if( std::any_of( preset.m_copperThicknessMm.begin(), preset.m_copperThicknessMm.end(),
                         []( double aThickness ) { return aThickness <= 0.0; } ) )
        {
            throw std::runtime_error( "copper thicknesses must be greater than zero" );
        }

        const nlohmann::json& dielectrics = root.at( "dielectrics" );

        if( !dielectrics.is_array() || dielectrics.size() != copperCount - 1 )
            throw std::runtime_error( "dielectrics must contain one entry between each copper layer" );

        for( const nlohmann::json& dielectric : dielectrics )
        {
            if( !dielectric.is_array() || dielectric.empty() )
                throw std::runtime_error( "each dielectric entry must contain at least one sublayer" );

            std::vector<PRESET_DIELECTRIC> sublayers;

            for( const nlohmann::json& source : dielectric )
            {
                const std::string type = source.at( "type" ).get<std::string>();

                if( type != "core" && type != "prepreg" )
                    throw std::runtime_error( "dielectric type must be core or prepreg" );

                PRESET_DIELECTRIC layer;
                layer.m_core = type == "core";
                layer.m_material = wxString::FromUTF8( source.at( "material" ).get<std::string>() );
                layer.m_thicknessMm = source.at( "thickness_mm" ).get<double>();
                layer.m_epsilonR = source.at( "epsilon_r" ).get<double>();
                layer.m_lossTangent = source.value( "loss_tangent", 0.02 );

                if( layer.m_material.IsEmpty() || layer.m_thicknessMm <= 0.0
                    || layer.m_epsilonR <= 0.0 || layer.m_lossTangent < 0.0 )
                {
                    throw std::runtime_error( "dielectric values must be positive" );
                }

                sublayers.push_back( std::move( layer ) );
            }

            preset.m_dielectrics.push_back( std::move( sublayers ) );
        }

        return preset;
    }
    catch( const std::exception& exception )
    {
        aError = wxString::Format( _( "Invalid stackup preset: %s" ),
                                   wxString::FromUTF8( exception.what() ) );
        return std::nullopt;
    }
}


std::optional<PANEL_SETUP_BOARD_STACKUP::STACKUP_PRESET>
PANEL_SETUP_BOARD_STACKUP::readStackupPresetFile( const wxString& aPath, wxString& aError ) const
{
    wxFile file( aPath );
    wxString contents;

    if( !file.IsOpened() || !file.ReadAll( &contents ) )
    {
        aError = _( "The stackup preset file could not be read." );
        return std::nullopt;
    }

    return parseStackupPresetJson( std::string( contents.utf8_str() ), aError );
}


void PANEL_SETUP_BOARD_STACKUP::onImportStackupPreset( wxCommandEvent& aEvent )
{
    wxFileDialog dialog( this, _( "Import Stackup Preset" ), wxEmptyString, wxEmptyString,
                         _( "KiCad Pro stackup preset (*.json)|*.json|All files (*.*)|*.*" ),
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    if( dialog.ShowModal() != wxID_OK )
        return;

    wxString error;
    std::optional<STACKUP_PRESET> preset = readStackupPresetFile( dialog.GetPath(), error );

    if( !preset )
    {
        wxMessageBox( error, _( "Import Stackup Preset" ), wxOK | wxICON_ERROR, this );
        return;
    }

    auto existing = std::find_if(
            m_importedStackupPresets.begin(), m_importedStackupPresets.end(),
            [&]( const STACKUP_PRESET& aCandidate )
            {
                return aCandidate.m_manufacturer == preset->m_manufacturer
                       && aCandidate.m_name == preset->m_name;
            } );

    if( existing == m_importedStackupPresets.end() )
        m_importedStackupPresets.push_back( *preset );
    else
        *existing = *preset;

    m_activeStackupPreset = *preset;
    rebuildPresetChoices();

    for( size_t ii = 1; ii < m_visibleStackupPresets.size(); ++ii )
    {
        const STACKUP_PRESET* candidate = m_visibleStackupPresets[ii];

        if( candidate && candidate->m_manufacturer == preset->m_manufacturer
            && candidate->m_name == preset->m_name )
        {
            m_stackupPreset->SetSelection( static_cast<int>( ii ) );
        }
    }

    applyStackupPreset( *preset );
    aEvent.Skip();
}


wxString PANEL_SETUP_BOARD_STACKUP::serializeProjectImpedanceSettings()
{
    saveImpedanceRowState();

    nlohmann::json root = {
        { "format", "kicad-pro-project-stackup" },
        { "version", 1 },
        { "enabled", m_impedanceControlled->GetValue() },
        { "layers", nlohmann::json::array() }
    };

    if( m_activeStackupPreset )
        root["preset"] = serializeStackupPresetJson( *m_activeStackupPreset );

    for( const auto& [layer, state] : m_impedanceState )
    {
        const int structure = std::clamp( static_cast<int>( state.m_structure ), 0, 5 );
        const double spacingMm = pcbIUScale.IUTomm(
                m_frame->ValueFromString( state.m_gap ) );

        nlohmann::json layerSettings = {
            { "layer", static_cast<int>( layer ) },
            { "structure", structure },
            { "target_ohms", std::string( state.m_target.utf8_str() ) },
            { "spacing_mm", spacingMm }
        };

        const auto row = std::find_if(
                m_impedanceRows.begin(), m_impedanceRows.end(),
                [layer]( const IMPEDANCE_ROW& aRow ) { return aRow.m_layer == layer; } );

        if( row != m_impedanceRows.end() )
        {
            wxString error;
            std::optional<double> width = calculateTraceWidth( *row, error );

            if( width )
                layerSettings["width_mm"] = *width * 1000.0;
        }

        root["layers"].push_back( std::move( layerSettings ) );
    }

    return wxString::FromUTF8( root.dump() );
}


void PANEL_SETUP_BOARD_STACKUP::loadProjectImpedanceSettings()
{
    wxString projectSettings = m_frame->Prj().GetProjectFile().m_BoardStackupControl;

    // Read the original board-property location once for projects created before the
    // dedicated project setting was introduced. Board properties are synchronized from
    // text variables and cannot reliably retain private PCB Editor state.
    if( projectSettings.IsEmpty() )
    {
        const auto property = m_board->GetProperties().find( KICAD_PRO_STACKUP_PROPERTY );

        if( property == m_board->GetProperties().end() )
            return;

        projectSettings = property->second;
    }

    try
    {
        const nlohmann::json root = nlohmann::json::parse(
                std::string( projectSettings.utf8_str() ) );

        if( root.value( "format", "" ) != "kicad-pro-project-stackup"
            || root.value( "version", 0 ) != 1 )
        {
            return;
        }

        m_impedanceControlled->SetValue( root.value( "enabled", false ) );

        if( root.contains( "preset" ) && root["preset"].is_string() )
        {
            wxString error;
            std::optional<STACKUP_PRESET> preset = parseStackupPresetJson(
                    root["preset"].get<std::string>(), error );

            if( preset )
            {
                m_activeStackupPreset = *preset;
                bool builtIn = false;

                for( const STACKUP_PRESET& candidate : getStackupPresets() )
                {
                    if( candidate.m_manufacturer == preset->m_manufacturer
                        && candidate.m_name == preset->m_name )
                    {
                        builtIn = true;
                        break;
                    }
                }

                if( !builtIn )
                    m_importedStackupPresets.push_back( *preset );

                rebuildPresetChoices();

                for( size_t ii = 1; ii < m_visibleStackupPresets.size(); ++ii )
                {
                    const STACKUP_PRESET* candidate = m_visibleStackupPresets[ii];

                    if( candidate && candidate->m_manufacturer == preset->m_manufacturer
                        && candidate->m_name == preset->m_name )
                    {
                        m_stackupPreset->SetSelection( static_cast<int>( ii ) );
                        break;
                    }
                }
            }
        }

        m_impedanceState.clear();

        for( const nlohmann::json& source : root.value( "layers", nlohmann::json::array() ) )
        {
            const int layerNumber = source.value( "layer", -1 );

            if( layerNumber < 0 || layerNumber >= PCB_LAYER_ID_COUNT )
                continue;

            IMPEDANCE_STATE state;
            state.m_structure = static_cast<IMPEDANCE_STRUCTURE>(
                    std::clamp( source.value( "structure", 0 ), 0, 5 ) );
            state.m_target = wxString::FromUTF8( source.value( "target_ohms", "50" ) );
            state.m_gap = m_frame->StringFromValue(
                    pcbIUScale.mmToIU( source.value( "spacing_mm", 0.2 ) ), true );
            m_impedanceState[static_cast<PCB_LAYER_ID>( layerNumber )] = std::move( state );
        }

        rebuildImpedanceRows();
        updateImpedancePanelVisibility();
    }
    catch( const std::exception& exception )
    {
        wxLogWarning( "Could not restore KiCad Pro stackup settings: %s",
                      wxString::FromUTF8( exception.what() ) );
    }
}


void PANEL_SETUP_BOARD_STACKUP::saveImpedanceRowState()
{
    for( const IMPEDANCE_ROW& row : m_impedanceRows )
    {
        IMPEDANCE_STATE& state = m_impedanceState[row.m_layer];
        state.m_structure = static_cast<IMPEDANCE_STRUCTURE>( row.m_structure->GetSelection() );
        state.m_target = row.m_target->GetValue();
        state.m_gap = row.m_gap->GetValue();
    }
}


void PANEL_SETUP_BOARD_STACKUP::rebuildImpedanceRows()
{
    if( !m_impedanceGrid )
        return;

    m_impedanceUpdateTimer.Stop();
    saveImpedanceRowState();
    m_impedanceRows.clear();
    m_impedanceWidthHeading = nullptr;
    m_impedanceGrid->Clear( true );

    const wxString units = EDA_UNIT_UTILS::GetText( m_frame->GetUserUnits() ).Trim( false );
    const wxString headings[] = { _( "Layer" ), _( "Structure" ), _( "Target (Ω)" ),
                                  _( "Spacing" ), wxString::Format( _( "W (%s)" ), units ) };

    for( size_t i = 0; i < std::size( headings ); ++i )
    {
        wxStaticText* label = new wxStaticText( m_impedanceGridWindow, wxID_ANY, headings[i] );
        label->SetFont( label->GetFont().Bold() );
        m_impedanceGrid->Add( label, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP( 2 ) );

        if( i == std::size( headings ) - 1 )
            m_impedanceWidthHeading = label;
    }

    wxArrayString structures;
    structures.Add( _( "Microstrip" ) );
    structures.Add( _( "GCPW" ) );
    structures.Add( _( "CPW" ) );
    structures.Add( _( "Stripline" ) );
    structures.Add( _( "Diff. micro" ) );
    structures.Add( _( "Diff. strip" ) );

    for( BOARD_STACKUP_ROW_UI_ITEM& stackRow : m_rowUiItemsList )
    {
        BOARD_STACKUP_ITEM* item = stackRow.m_Item;

        if( !stackRow.m_isEnabled || item->GetType() != BS_ITEM_TYPE_COPPER )
            continue;

        const PCB_LAYER_ID layer = item->GetBrdLayerId();
        auto [stateIt, inserted] = m_impedanceState.try_emplace( layer );
        IMPEDANCE_STATE& state = stateIt->second;

        if( inserted )
        {
            const bool outer = layer == F_Cu || layer == B_Cu;
            state.m_structure = outer ? IMPEDANCE_STRUCTURE::MICROSTRIP
                                      : IMPEDANCE_STRUCTURE::STRIPLINE;
            state.m_gap = m_frame->StringFromValue( pcbIUScale.mmToIU( 0.2 ), true );
        }

        wxStaticText* layerName = new wxStaticText( m_impedanceGridWindow, wxID_ANY,
                                                     m_board->GetLayerName( layer ) );
        wxChoice* structure = new wxChoice( m_impedanceGridWindow, wxID_ANY,
                                             wxDefaultPosition, wxDefaultSize, structures );
        const wxSize compactStructureSize( FromDIP( 82 ), -1 );
        structure->SetMinSize( compactStructureSize );
        structure->SetMaxSize( compactStructureSize );
        structure->SetToolTip(
                _( "Trace geometry. GCPW: grounded coplanar waveguide; CPW: coplanar "
                   "waveguide; Diff.: differential pair." ) );
        structure->SetSelection( static_cast<int>( state.m_structure ) );

        wxTextCtrl* target = new wxTextCtrl( m_impedanceGridWindow, wxID_ANY, state.m_target,
                                             wxDefaultPosition, wxSize( FromDIP( 40 ), -1 ) );
        target->SetToolTip( _( "Target characteristic impedance in ohms" ) );

        wxTextCtrl* gap = new wxTextCtrl( m_impedanceGridWindow, wxID_ANY, state.m_gap,
                                          wxDefaultPosition, wxSize( FromDIP( 62 ), -1 ) );
        gap->SetToolTip( _( "Copper-to-copper spacing for coplanar and differential traces" ) );

        wxTextCtrl* width = new wxTextCtrl( m_impedanceGridWindow, wxID_ANY, wxEmptyString,
                                            wxDefaultPosition, wxSize( FromDIP( 112 ), -1 ),
                                            wxTE_READONLY | wxTE_RIGHT );
        width->SetToolTip( _( "Calculated trace width" ) );

        m_impedanceGrid->Add( layerName, 0, wxALIGN_CENTER_VERTICAL );
        m_impedanceGrid->Add( structure, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL );
        m_impedanceGrid->Add( target, 0, wxALIGN_CENTER_VERTICAL );
        m_impedanceGrid->Add( gap, 0, wxALIGN_CENTER_VERTICAL );
        m_impedanceGrid->Add( width, 0, wxALIGN_CENTER_VERTICAL );

        m_impedanceRows.push_back( { layer, structure, target, nullptr, gap, width } );

        structure->Bind( wxEVT_CHOICE,
                         [this, layer]( wxCommandEvent& ) { updateImpedanceRow( layer ); } );
        target->Bind( wxEVT_TEXT,
                      [this, layer]( wxCommandEvent& ) { updateImpedanceRow( layer ); } );
        gap->Bind( wxEVT_TEXT,
                   [this, layer]( wxCommandEvent& ) { updateImpedanceRow( layer ); } );
    }

    m_impedanceGridWindow->FitInside();
    m_impedanceGridWindow->Layout();
    updateAllImpedanceRows();
}


void PANEL_SETUP_BOARD_STACKUP::updateImpedancePanelVisibility()
{
    if( !m_impedancePanel )
        return;

    m_impedancePanel->Show( m_impedanceControlled->GetValue() );
    m_sizerStackup->Layout();
    Layout();
}


void PANEL_SETUP_BOARD_STACKUP::onImpedanceControlled( wxCommandEvent& aEvent )
{
    updateImpedancePanelVisibility();
    aEvent.Skip();
}


void PANEL_SETUP_BOARD_STACKUP::onImpedanceParameterChanged( wxCommandEvent& aEvent )
{
    scheduleImpedanceUpdate();
    aEvent.Skip();
}


void PANEL_SETUP_BOARD_STACKUP::scheduleImpedanceUpdate()
{
    // Text fields emit an event for every keystroke and bulk stackup operations can update
    // several controls together. Coalesce them into one calculation pass.
    m_impedanceUpdateTimer.StartOnce( 75 );
}


std::optional<PANEL_SETUP_BOARD_STACKUP::TRACE_GEOMETRY>
PANEL_SETUP_BOARD_STACKUP::getTraceGeometry( PCB_LAYER_ID aLayer ) const
{
    int copperRow = -1;

    for( size_t ii = 0; ii < m_rowUiItemsList.size(); ++ii )
    {
        const BOARD_STACKUP_ROW_UI_ITEM& row = m_rowUiItemsList[ii];

        if( row.m_isEnabled && row.m_Item->GetType() == BS_ITEM_TYPE_COPPER
            && row.m_Item->GetBrdLayerId() == aLayer )
        {
            copperRow = static_cast<int>( ii );
            break;
        }
    }

    if( copperRow < 0 )
        return std::nullopt;

    auto dielectricInDirection =
            [&]( int aDirection )
            {
                DIELECTRIC_GEOMETRY result;
                double epsilonSum = 0.0;
                double lossSum = 0.0;
                bool foundReference = false;

                for( int ii = copperRow + aDirection;
                     ii >= 0 && ii < static_cast<int>( m_rowUiItemsList.size() );
                     ii += aDirection )
                {
                    const BOARD_STACKUP_ROW_UI_ITEM& row = m_rowUiItemsList[ii];

                    if( !row.m_isEnabled )
                        continue;

                    if( row.m_Item->GetType() == BS_ITEM_TYPE_COPPER )
                    {
                        foundReference = true;
                        break;
                    }

                    if( row.m_Item->GetType() != BS_ITEM_TYPE_DIELECTRIC )
                        continue;

                    const wxTextCtrl* thicknessCtrl =
                            dynamic_cast<const wxTextCtrl*>( row.m_ThicknessCtrl );
                    const wxTextCtrl* epsilonCtrl =
                            dynamic_cast<const wxTextCtrl*>( row.m_EpsilonCtrl );
                    const wxTextCtrl* lossCtrl =
                            dynamic_cast<const wxTextCtrl*>( row.m_LossTgCtrl );
                    double epsilon = 0.0;
                    double loss = 0.0;

                    if( !thicknessCtrl || !epsilonCtrl
                        || ( !epsilonCtrl->GetValue().ToDouble( &epsilon )
                             && !epsilonCtrl->GetValue().ToCDouble( &epsilon ) ) )
                    {
                        continue;
                    }

                    if( lossCtrl )
                    {
                        if( !lossCtrl->GetValue().ToDouble( &loss ) )
                            lossCtrl->GetValue().ToCDouble( &loss );
                    }

                    const double height = pcbIUScale.IUTomm(
                            m_frame->ValueFromString( thicknessCtrl->GetValue() ) ) / 1000.0;

                    if( height <= 0.0 || epsilon <= 0.0 )
                        continue;

                    result.m_height += height;
                    epsilonSum += epsilon * height;
                    lossSum += loss * height;
                }

                result.m_valid = foundReference && result.m_height > 0.0;

                if( result.m_height > 0.0 )
                {
                    result.m_epsilonR = epsilonSum / result.m_height;
                    result.m_lossTangent = lossSum / result.m_height;
                }

                return result;
            };

    TRACE_GEOMETRY geometry;
    geometry.m_above = dielectricInDirection( -1 );
    geometry.m_below = dielectricInDirection( 1 );

    const wxTextCtrl* copperThickness = dynamic_cast<const wxTextCtrl*>(
            m_rowUiItemsList[copperRow].m_ThicknessCtrl );

    if( copperThickness )
    {
        geometry.m_copperThickness = pcbIUScale.IUTomm(
                m_frame->ValueFromString( copperThickness->GetValue() ) ) / 1000.0;
    }

    return geometry;
}


static double ellipticK( double aModulus )
{
    aModulus = std::clamp( aModulus, 1e-12, 1.0 - 1e-12 );
    double a = 1.0;
    double b = std::sqrt( 1.0 - aModulus * aModulus );

    for( int ii = 0; ii < 16 && std::abs( a - b ) > 1e-14; ++ii )
    {
        const double nextA = ( a + b ) / 2.0;
        b = std::sqrt( a * b );
        a = nextA;
    }

    return M_PI / ( 2.0 * a );
}


static double coplanarImpedance( double aWidth, double aGap, double aHeight,
                                 double aEpsilonR, bool aGrounded )
{
    constexpr double Z_FREE_SPACE = 376.730313668;
    const double k1 = aWidth / ( aWidth + 2.0 * aGap );
    const double q1 = ellipticK( k1 ) / ellipticK( std::sqrt( 1.0 - k1 * k1 ) );
    double epsilonEffective;
    double factor;

    if( aGrounded )
    {
        const double k3 = std::tanh( M_PI * aWidth / ( 4.0 * aHeight ) )
                          / std::tanh( M_PI * ( aWidth + 2.0 * aGap )
                                       / ( 4.0 * aHeight ) );
        const double q3 = ellipticK( k3 ) / ellipticK( std::sqrt( 1.0 - k3 * k3 ) );
        const double q = 1.0 / ( q1 + q3 );
        epsilonEffective = 1.0 + q3 * q * ( aEpsilonR - 1.0 );
        factor = Z_FREE_SPACE * q / 2.0;
    }
    else
    {
        const double k2 = std::sinh( M_PI * aWidth / ( 4.0 * aHeight ) )
                          / std::sinh( M_PI * ( aWidth + 2.0 * aGap )
                                       / ( 4.0 * aHeight ) );
        const double q2 = ellipticK( k2 ) / ellipticK( std::sqrt( 1.0 - k2 * k2 ) );
        epsilonEffective = 1.0 + ( aEpsilonR - 1.0 ) * q2 / ( 2.0 * q1 );
        factor = Z_FREE_SPACE / ( 4.0 * q1 );
    }

    return factor / std::sqrt( epsilonEffective );
}


static double ipc2141MicrostripWidth( double aImpedance, double aCopperThickness,
                                      double aHeight, double aEpsilonR )
{
    // Algebraic synthesis form of the IPC-2141 microstrip approximation.  Keeping the
    // original 5.98 / 0.8 coefficients avoids the rounding introduced by displaying
    // the equivalent expression as 7.48h - 1.25t.
    return ( 5.98 * aHeight
             / std::exp( aImpedance * std::sqrt( aEpsilonR + 1.41 ) / 87.0 )
             - aCopperThickness ) / 0.8;
}


std::optional<double> PANEL_SETUP_BOARD_STACKUP::calculateTraceWidth(
        const IMPEDANCE_ROW& aRow, wxString& aError ) const
{
    double target = 0.0;

    if( ( !aRow.m_target->GetValue().ToDouble( &target )
          && !aRow.m_target->GetValue().ToCDouble( &target ) ) || target <= 0.0 )
    {
        aError = _( "Target impedance must be greater than 0" );
        return std::nullopt;
    }

    std::optional<TRACE_GEOMETRY> geometry = getTraceGeometry( aRow.m_layer );

    if( !geometry )
    {
        aError = _( "Layer geometry is unavailable" );
        return std::nullopt;
    }

    const IMPEDANCE_STRUCTURE structure = static_cast<IMPEDANCE_STRUCTURE>(
            aRow.m_structure->GetSelection() );
    const bool microstrip = structure == IMPEDANCE_STRUCTURE::MICROSTRIP
                            || structure == IMPEDANCE_STRUCTURE::GROUNDED_COPLANAR
                            || structure == IMPEDANCE_STRUCTURE::COPLANAR
                            || structure == IMPEDANCE_STRUCTURE::DIFF_MICROSTRIP;
    const DIELECTRIC_GEOMETRY* substrate = nullptr;

    if( microstrip )
    {
        if( aRow.m_layer == F_Cu )
            substrate = &geometry->m_below;
        else if( aRow.m_layer == B_Cu )
            substrate = &geometry->m_above;
        else if( geometry->m_above.m_valid && geometry->m_below.m_valid )
            substrate = geometry->m_above.m_height <= geometry->m_below.m_height
                                ? &geometry->m_above : &geometry->m_below;

        if( !substrate || !substrate->m_valid )
        {
            aError = _( "A positive dielectric thickness and reference copper layer are required" );
            return std::nullopt;
        }
    }
    else if( !geometry->m_above.m_valid || !geometry->m_below.m_valid )
    {
        aError = _( "Stripline requires positive dielectric thickness above and below" );
        return std::nullopt;
    }

    double gap = 0.0;
    const bool usesGap = structure == IMPEDANCE_STRUCTURE::GROUNDED_COPLANAR
                         || structure == IMPEDANCE_STRUCTURE::COPLANAR
                         || structure == IMPEDANCE_STRUCTURE::DIFF_MICROSTRIP
                         || structure == IMPEDANCE_STRUCTURE::DIFF_STRIPLINE;

    if( usesGap )
    {
        gap = pcbIUScale.IUTomm( m_frame->ValueFromString( aRow.m_gap->GetValue() ) ) / 1000.0;

        if( gap <= 0.0 )
        {
            aError = _( "Spacing must be greater than 0" );
            return std::nullopt;
        }
    }

    if( structure == IMPEDANCE_STRUCTURE::MICROSTRIP )
    {
        const double width = ipc2141MicrostripWidth( target, geometry->m_copperThickness,
                                                      substrate->m_height,
                                                      substrate->m_epsilonR );

        if( !std::isfinite( width ) || width <= 0.0 )
        {
            aError = _( "No practical width was found for this geometry" );
            return std::nullopt;
        }

        return width;
    }

    if( structure == IMPEDANCE_STRUCTURE::GROUNDED_COPLANAR
        || structure == IMPEDANCE_STRUCTURE::COPLANAR )
    {
        double low = 1e-7;
        double high = std::max( 0.02, substrate->m_height * 100.0 );
        const bool grounded = structure == IMPEDANCE_STRUCTURE::GROUNDED_COPLANAR;

        if( coplanarImpedance( low, gap, substrate->m_height,
                               substrate->m_epsilonR, grounded ) < target
            || coplanarImpedance( high, gap, substrate->m_height,
                                  substrate->m_epsilonR, grounded ) > target )
        {
            aError = _( "No practical width was found for this geometry" );
            return std::nullopt;
        }

        for( int ii = 0; ii < 80; ++ii )
        {
            const double mid = ( low + high ) / 2.0;

            if( coplanarImpedance( mid, gap, substrate->m_height,
                                   substrate->m_epsilonR, grounded ) > target )
                low = mid;
            else
                high = mid;
        }

        return ( low + high ) / 2.0;
    }

    constexpr double FREQUENCY = 1.0e9;
    constexpr double COPPER_RESISTIVITY = 1.72e-8;
    constexpr double MU_0 = 1.25663706212e-6;
    const double sigma = 1.0 / COPPER_RESISTIVITY;
    const double skinDepth = std::sqrt( COPPER_RESISTIVITY / ( M_PI * FREQUENCY * MU_0 ) );
    const bool differential = structure == IMPEDANCE_STRUCTURE::DIFF_MICROSTRIP
                              || structure == IMPEDANCE_STRUCTURE::DIFF_STRIPLINE;
    std::unique_ptr<TRANSLINE_CALCULATION_BASE> calculator;

    if( structure == IMPEDANCE_STRUCTURE::MICROSTRIP )
        calculator = std::make_unique<MICROSTRIP>();
    else if( structure == IMPEDANCE_STRUCTURE::STRIPLINE )
        calculator = std::make_unique<STRIPLINE>();
    else if( structure == IMPEDANCE_STRUCTURE::DIFF_MICROSTRIP )
        calculator = std::make_unique<COUPLED_MICROSTRIP>();
    else
        calculator = std::make_unique<COUPLED_STRIPLINE>();

    const double epsilon = microstrip
                                   ? substrate->m_epsilonR
                                   : ( geometry->m_above.m_epsilonR * geometry->m_above.m_height
                                       + geometry->m_below.m_epsilonR * geometry->m_below.m_height )
                                             / ( geometry->m_above.m_height
                                                 + geometry->m_below.m_height );
    const double loss = microstrip
                                ? substrate->m_lossTangent
                                : ( geometry->m_above.m_lossTangent * geometry->m_above.m_height
                                    + geometry->m_below.m_lossTangent * geometry->m_below.m_height )
                                          / ( geometry->m_above.m_height
                                              + geometry->m_below.m_height );

    calculator->SetParameter( TRANSLINE_PARAMETERS::EPSILONR, epsilon );
    calculator->SetParameter( TRANSLINE_PARAMETERS::T, geometry->m_copperThickness );
    calculator->SetParameter( TRANSLINE_PARAMETERS::PHYS_WIDTH, 1e-4 );
    calculator->SetParameter( TRANSLINE_PARAMETERS::PHYS_LEN, 0.01 );
    calculator->SetParameter( TRANSLINE_PARAMETERS::FREQUENCY, FREQUENCY );
    calculator->SetParameter( TRANSLINE_PARAMETERS::SIGMA, sigma );
    calculator->SetParameter( TRANSLINE_PARAMETERS::SKIN_DEPTH, skinDepth );
    calculator->SetParameter( TRANSLINE_PARAMETERS::ANG_L, 1.0 );
    calculator->SetParameter( TRANSLINE_PARAMETERS::MURC, 1.0 );

    if( structure == IMPEDANCE_STRUCTURE::MICROSTRIP
        || structure == IMPEDANCE_STRUCTURE::DIFF_MICROSTRIP )
    {
        calculator->SetParameter( TRANSLINE_PARAMETERS::H, substrate->m_height );
        calculator->SetParameter( TRANSLINE_PARAMETERS::H_T, 1e20 );
        calculator->SetParameter( TRANSLINE_PARAMETERS::ROUGH, 0.0 );
        calculator->SetParameter( TRANSLINE_PARAMETERS::TAND, loss );

        if( structure == IMPEDANCE_STRUCTURE::MICROSTRIP )
            calculator->SetParameter( TRANSLINE_PARAMETERS::MUR, 1.0 );
    }
    else if( structure == IMPEDANCE_STRUCTURE::STRIPLINE )
    {
        calculator->SetParameter( TRANSLINE_PARAMETERS::STRIPLINE_A,
                                  geometry->m_above.m_height );
        calculator->SetParameter( TRANSLINE_PARAMETERS::H,
                                  geometry->m_above.m_height + geometry->m_copperThickness
                                          + geometry->m_below.m_height );
        calculator->SetParameter( TRANSLINE_PARAMETERS::TAND, loss );
    }
    else
    {
        calculator->SetParameter( TRANSLINE_PARAMETERS::H,
                                  geometry->m_above.m_height + geometry->m_copperThickness
                                          + geometry->m_below.m_height );
    }

    if( differential )
    {
        calculator->SetParameter( TRANSLINE_PARAMETERS::Z0_E, target / 2.0 );
        calculator->SetParameter( TRANSLINE_PARAMETERS::Z0_O, target / 2.0 );
        calculator->SetParameter( TRANSLINE_PARAMETERS::Z_DIFF, target );
        calculator->SetParameter( TRANSLINE_PARAMETERS::PHYS_S, gap );
    }
    else
    {
        calculator->SetParameter( TRANSLINE_PARAMETERS::Z0, target );
    }

    const SYNTHESIZE_OPTS opts = differential ? SYNTHESIZE_OPTS::FIX_SPACING
                                                : SYNTHESIZE_OPTS::DEFAULT;

    if( !calculator->Synthesize( opts ) )
    {
        aError = _( "Width calculation did not converge" );
        return std::nullopt;
    }

    auto& results = calculator->GetSynthesisResults();
    auto widthResult = results.find( TRANSLINE_PARAMETERS::PHYS_WIDTH );

    if( widthResult == results.end() || widthResult->second.second != TRANSLINE_STATUS::OK
        || !std::isfinite( widthResult->second.first ) || widthResult->second.first <= 0.0 )
    {
        aError = _( "No practical width was found for this geometry" );
        return std::nullopt;
    }

    return widthResult->second.first;
}


void PANEL_SETUP_BOARD_STACKUP::updateImpedanceRow( PCB_LAYER_ID aLayer )
{
    auto rowIt = std::find_if( m_impedanceRows.begin(), m_impedanceRows.end(),
                               [aLayer]( const IMPEDANCE_ROW& aRow )
                               {
                                   return aRow.m_layer == aLayer;
                               } );

    if( rowIt == m_impedanceRows.end() )
        return;

    const IMPEDANCE_STRUCTURE structure = static_cast<IMPEDANCE_STRUCTURE>(
            rowIt->m_structure->GetSelection() );
    const bool usesGap = structure == IMPEDANCE_STRUCTURE::GROUNDED_COPLANAR
                         || structure == IMPEDANCE_STRUCTURE::COPLANAR
                         || structure == IMPEDANCE_STRUCTURE::DIFF_MICROSTRIP
                         || structure == IMPEDANCE_STRUCTURE::DIFF_STRIPLINE;
    rowIt->m_gap->Enable( usesGap );

    wxString error;
    std::optional<double> width = calculateTraceWidth( *rowIt, error );

    if( width )
    {
        const int widthIU = pcbIUScale.mmToIU( *width * 1000.0 );
        rowIt->m_width->ChangeValue( m_frame->StringFromValue( widthIU, false ) );
        rowIt->m_width->SetToolTip( _( "Calculated trace width" ) );
    }
    else
    {
        rowIt->m_width->ChangeValue( wxT( "—" ) );
        rowIt->m_width->SetToolTip( error );
    }
}


void PANEL_SETUP_BOARD_STACKUP::updateAllImpedanceRows()
{
    for( const IMPEDANCE_ROW& row : m_impedanceRows )
        updateImpedanceRow( row.m_layer );
}


BOARD_STACKUP_ITEM* PANEL_SETUP_BOARD_STACKUP::GetStackupItem( int aRow )
{
    return m_rowUiItemsList[aRow].m_Item;
}


int PANEL_SETUP_BOARD_STACKUP::GetSublayerId( int aRow )
{
    return m_rowUiItemsList[aRow].m_SubItem;
}


wxColor PANEL_SETUP_BOARD_STACKUP::getColorIconItem( int aRow )
{
    BOARD_STACKUP_ITEM* st_item = dynamic_cast<BOARD_STACKUP_ITEM*>( GetStackupItem( aRow ) );

    wxASSERT( st_item );
    wxColor color;

    if( ! st_item )
        return color;

    switch( st_item->GetType() )
    {
    case BS_ITEM_TYPE_COPPER:      color = copperColor;              break;
    case BS_ITEM_TYPE_DIELECTRIC:  color = dielectricColor;          break;
    case BS_ITEM_TYPE_SOLDERMASK:  color = GetSelectedColor( aRow ); break;
    case BS_ITEM_TYPE_SILKSCREEN:  color = GetSelectedColor( aRow ); break;
    case BS_ITEM_TYPE_SOLDERPASTE: color = pasteColor;               break;

    default:
    case BS_ITEM_TYPE_UNDEFINED:
        wxFAIL_MSG( wxT( "PANEL_SETUP_BOARD_STACKUP::getColorIconItem: unrecognized item type" ) );
        break;
    }

    wxASSERT_MSG( color.IsOk(), wxT( "Invalid color in PCB stackup" ) );

    return color;
}


void PANEL_SETUP_BOARD_STACKUP::updateIconColor( int aRow )
{
    // explicit depth important under MSW. We use R,V,B 24 bits/pixel bitmap
    const int bitmap_depth = 24;

    if( aRow >= 0 )
    {
        wxStaticBitmap* st_bitmap = m_rowUiItemsList[aRow].m_Icon;

        wxBitmap bmp( m_colorIconsSize.x, m_colorIconsSize.y / 2, bitmap_depth );
        drawBitmap( bmp, getColorIconItem( aRow ) );
        st_bitmap->SetBitmap( bmp );
        return;
    }

    for( unsigned row = 0; row < m_rowUiItemsList.size(); row++ )
    {
        if( m_rowUiItemsList[row].m_Icon )
        {
            wxBitmap bmp( m_colorIconsSize.x, m_colorIconsSize.y / 2, bitmap_depth );
            drawBitmap( bmp, getColorIconItem( row ) );
            m_rowUiItemsList[row].m_Icon->SetBitmap( bmp );
        }
    }
}


wxBitmapComboBox* PANEL_SETUP_BOARD_STACKUP::createColorBox( BOARD_STACKUP_ITEM* aStackupItem,
                                                             int aRow )
{
    wxBitmapComboBox* combo = new wxBitmapComboBox( m_scGridWin, ID_ITEM_COLOR + aRow,
                                                    wxEmptyString, wxDefaultPosition,
                                                    wxDefaultSize, 0, nullptr, wxCB_READONLY );

    // Fills the combo box with choice list + bitmaps
    BOARD_STACKUP_ITEM_TYPE itemType = aStackupItem ? aStackupItem->GetType()
                                                    : BS_ITEM_TYPE_SILKSCREEN;

    for( size_t ii = 0; ii < GetStandardColors( itemType ).size(); ii++ )
    {
        wxString label;
        COLOR4D  curr_color;

        // Defined colors have a name, the user color uses HTML notation ( i.e. #FF000080)
        if( IsCustomColorIdx( itemType, ii )
                && aStackupItem && aStackupItem->GetColor().StartsWith( wxT( "#" ) ) )
        {
            label = aStackupItem->GetColor();
            curr_color = COLOR4D( label );
        }
        else
        {
            label = wxGetTranslation( GetStandardColorName( itemType, ii ) );
            curr_color = GetStandardColor( itemType, ii );
        }

        wxBitmap layerbmp( m_colorSwatchesSize.x, m_colorSwatchesSize.y );
        LAYER_PRESENTATION::DrawColorSwatch( layerbmp, COLOR4D( 0, 0, 0, 0 ), curr_color );

        combo->Append( label, layerbmp );
    }

    // Ensure the size of the widget is enough to show the text and the icon
    // We have to have a selected item when doing this, because otherwise GTK
    // will just choose a random size that might not fit the actual data
    // (such as in cases where the font size is very large). So we select
    // the longest item (which should be the last item), and size it that way.
    int sel = combo->GetSelection();
    combo->SetSelection( combo->GetCount() - 1 );

    combo->SetMinSize( wxSize( -1, -1 ) );
    wxSize bestSize = combo->GetBestSize();

    bestSize.x = bestSize.x + m_colorSwatchesSize.x;
    combo->SetMinSize( bestSize );
    combo->SetSelection( sel );

    // add the wxBitmapComboBox to wxControl list, to be able to disconnect the event
    // on exit
    m_controlItemsList.push_back( combo );

    combo->Connect( wxEVT_COMMAND_COMBOBOX_SELECTED,
                    wxCommandEventHandler( PANEL_SETUP_BOARD_STACKUP::onColorSelected ),
                    nullptr, this );

    combo->Bind( wxEVT_COMBOBOX_DROPDOWN,
            [combo]( wxCommandEvent& aEvent )
            {
                combo->SetString( combo->GetCount() - 1, _( "Custom..." ) );
            } );

    return combo;
}


void drawBitmap( wxBitmap& aBitmap, wxColor aColor )
{
    wxNativePixelData data( aBitmap );
    wxNativePixelData::Iterator p( data );

    for( int yy = 0; yy < data.GetHeight(); yy++ )
    {
        wxNativePixelData::Iterator rowStart = p;

        for( int xx = 0; xx < data.GetWidth(); xx++ )
        {
            p.Red() = aColor.Red();
            p.Green() = aColor.Green();
            p.Blue() = aColor.Blue();
            ++p;
        }

        p = rowStart;
        p.OffsetY( data, 1 );
    }
}
