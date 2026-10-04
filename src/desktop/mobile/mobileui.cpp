// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/mobile/mobileui.h"
#include "desktop/dialogs/startdialog.h"
#include "desktop/mainwindow.h"
#include "desktop/mobile/shell.h"

namespace mobile {

void attach(MainWindow *mw)
{
	if(mw && !Shell::of(mw)) {
		new Shell(mw);
	}
}

void beforeInterfaceModeChange(MainWindow *mw, bool smallScreenMode)
{
	if(Shell *shell = Shell::of(mw)) {
		shell->beforeInterfaceModeChange(smallScreenMode);
	}
}

bool interceptStartPage(MainWindow *mw, int page)
{
	switch(page) {
	case int(dialogs::StartDialog::Guess):
	case int(dialogs::StartDialog::Create):
#ifndef __EMSCRIPTEN__
	case int(dialogs::StartDialog::Recent):
#endif
		break;
	default:
		return false;
	}
	Shell *shell = Shell::of(mw);
	return shell && shell->showHub(page);
}

}
