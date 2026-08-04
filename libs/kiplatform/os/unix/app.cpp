/*
* This program source code file is part of KiCad, a free EDA CAD application.
*
* Copyright (C) 2020 Mark Roszko <mark.roszko@gmail.com>
* Copyright The KiCad Developers, see AUTHORS.txt for contributors.
*
* This program is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation, either version 3 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <kiplatform/app.h>

#include <glib.h>
#include <gtk/gtk.h>

#include <wx/string.h>
#include <wx/utils.h>


/*
 * Function to attach to the glib logger to eat the output it gives so we don't
 * get the message spam on the terminal from wxWidget's abuse of the GTK API.
 */
static GLogWriterOutput nullLogWriter( GLogLevelFlags log_level, const GLogField* fields,
                                       gsize n_fields, gpointer user_data )
{
    return G_LOG_WRITER_HANDLED;
}


static GtkCssProvider* s_darkThemeProvider;

static const char s_darkThemeCss[] = R"css(
* {
    color: #E6E6E6;
    border-color: #383838;
    caret-color: #E6E6E6;
}

window, dialog, .background, notebook, stack, paned {
    background-color: #141414;
}

headerbar, .titlebar, menubar {
    background-color: #121212;
}

toolbar {
    background-color: #151515;
}

menu, popover, entry, textview, treeview.view, list, combobox button {
    background-color: #1B1B1B;
}

button:hover, menuitem:hover, menubar > menuitem:hover {
    background-color: #292929;
}

button:active {
    background-color: #333333;
}

separator {
    background-color: #383838;
}

label:disabled, entry:disabled, button:disabled {
    color: #666666;
}

window:backdrop label, window:backdrop entry {
    color: #A8A8A8;
}

*:selected, row:selected {
    background-color: #1769C2;
    color: #E6E6E6;
}

*:selected:hover, row:selected:hover {
    background-color: #2484E4;
}
)css";


bool KIPLATFORM::APP::Init()
{
    // Set KICAD_SHOW_GTK_MESSAGES=1 env var to show GTK messages,
    // otherwise, we hide them to avoid message spam.

    wxString showMessages;
    wxGetEnv( wxT( "KICAD_SHOW_GTK_MESSAGES" ), &showMessages );

    if( showMessages.IsEmpty() || showMessages != "1" )
    {
        // Attach a logger that will consume the annoying GTK error messages
        g_log_set_writer_func( nullLogWriter, nullptr, nullptr );
    }

    return true;
}


void KIPLATFORM::APP::EnableDarkMode( bool aForce )
{
    GdkScreen* screen = gdk_screen_get_default();

    if( !screen )
        return;

    if( !s_darkThemeProvider )
        s_darkThemeProvider = gtk_css_provider_new();

    gtk_style_context_remove_provider_for_screen( screen,
                                                  GTK_STYLE_PROVIDER( s_darkThemeProvider ) );

    if( aForce )
    {
        gtk_css_provider_load_from_data( s_darkThemeProvider, s_darkThemeCss, -1, nullptr );
        gtk_style_context_add_provider_for_screen( screen, GTK_STYLE_PROVIDER( s_darkThemeProvider ),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION );
    }
}


bool KIPLATFORM::APP::AttachConsole( bool aTryAlloc )
{
    // Not implemented on this platform
    return true;
}


bool KIPLATFORM::APP::IsOperatingSystemUnsupported()
{
    // Not implemented on this platform
    return false;
}


bool KIPLATFORM::APP::RegisterApplicationRestart( const wxString& aCommandLine )
{
    // Not implemented on this platform
    return true;
}


bool KIPLATFORM::APP::UnregisterApplicationRestart()
{
    // Not implemented on this platform
    return true;
}


bool KIPLATFORM::APP::SupportsShutdownBlockReason()
{
    return false;
}


void KIPLATFORM::APP::RemoveShutdownBlockReason( wxWindow* aWindow )
{
}


void KIPLATFORM::APP::SetShutdownBlockReason( wxWindow* aWindow, const wxString& aReason )
{
}


void KIPLATFORM::APP::ForceTimerMessagesToBeCreatedIfNecessary()
{
}


void KIPLATFORM::APP::AddDynamicLibrarySearchPath( const wxString& aPath )
{
}
