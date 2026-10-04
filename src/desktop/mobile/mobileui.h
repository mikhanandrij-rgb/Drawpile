// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): entry points called from upstream code. These are
// the only functions upstream files call, keeping the fork's footprint in
// existing files to a handful of lines.
#ifndef DESKTOP_MOBILE_MOBILEUI_H
#define DESKTOP_MOBILE_MOBILEUI_H

class MainWindow;

namespace mobile {

// Attaches the mobile interface controller to a main window. It activates
// itself whenever the window is in small-screen ("Mobile") mode.
void attach(MainWindow *mw);

// Must be called before the main window switches its interface mode, so that
// the mobile interface can hand docks and tool bars back to the main window.
void beforeInterfaceModeChange(MainWindow *mw, bool smallScreenMode);

// Called when a start dialog page is about to be shown. Returns true if the
// mobile project hub took over instead.
bool interceptStartPage(MainWindow *mw, int page);

}

#endif
