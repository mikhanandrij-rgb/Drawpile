// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): writes an inventory of every action in the main
// window together with where it can be reached, in the original interface
// and in the mobile one. Enabled with DRAWPILE_MOBILE_INVENTORY=<path>.
#include "desktop/docks/dockbase.h"
#include "desktop/mainwindow.h"
#include "desktop/mobile/commandsheet.h"
#include "desktop/mobile/shell.h"
#include <QAction>
#include <QActionGroup>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QWidgetAction>

namespace mobile {

namespace {

QString clean(QString text)
{
	text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
	text.remove(QLatin1Char('&'));
	text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
	text.replace(QLatin1Char('|'), QStringLiteral("\\|"));
	return text;
}

void collectMenu(
	QMenu *menu, const QString &path, QHash<QAction *, QStringList> &out,
	QSet<QMenu *> &seen)
{
	if(seen.contains(menu)) {
		return;
	}
	seen.insert(menu);
	for(QAction *action : menu->actions()) {
		if(action->isSeparator()) {
			continue;
		}
		if(QMenu *sub = action->menu()) {
			collectMenu(
				sub, QStringLiteral("%1 › %2").arg(path, clean(sub->title())),
				out, seen);
		} else {
			out[action].append(path);
		}
	}
}

docks::DockBase *dockAncestor(QObject *object)
{
	for(QObject *o = object; o; o = o->parent()) {
		if(docks::DockBase *dock = qobject_cast<docks::DockBase *>(o)) {
			return dock;
		}
	}
	return nullptr;
}

bool isMobileChrome(QObject *object)
{
	for(QObject *o = object; o; o = o->parent()) {
		if(qobject_cast<docks::DockBase *>(o)) {
			return false;
		}
		if(o->property("mobileChrome").toBool()) {
			return true;
		}
	}
	return false;
}

}

void Shell::dumpInventory(const QString &path)
{
	if(!m_mw) {
		return;
	}
	MainWindow *mw = m_mw;

	// Where actions live in the main menu bar.
	QHash<QAction *, QStringList> menuPaths;
	QSet<QMenu *> seenMenus;
	for(QAction *top : mw->menuBar()->actions()) {
		if(QMenu *menu = top->menu()) {
			collectMenu(menu, clean(menu->title()), menuPaths, seenMenus);
		}
	}

	// Upstream tool bars, parked or not.
	QHash<QAction *, QStringList> toolBarNames;
	QList<QToolBar *> toolBars = mw->findChildren<QToolBar *>();
	for(QToolBar *toolBar : toolBars) {
		if(isMobileChrome(toolBar) || toolBar == m_topHolder ||
		   toolBar == m_railHolder) {
			continue;
		}
		for(QAction *action : toolBar->actions()) {
			if(!action->isSeparator() &&
			   !qobject_cast<QWidgetAction *>(action)) {
				toolBarNames[action].append(toolBar->windowTitle());
			}
		}
	}

	// Actions exposed directly by the mobile chrome.
	QHash<QString, QStringList> mobileDirect;
	auto addDirect = [&mobileDirect](const QString &name, const QString &where) {
		mobileDirect[name].append(where);
	};
	addDirect(QStringLiteral("undo"), tr("Top bar"));
	addDirect(QStringLiteral("redo"), tr("Top bar"));
	addDirect(QStringLiteral("toolpicker"), tr("Quick sliders (eyedropper)"));
	addDirect(QStringLiteral("swapcolors"), tr("Color button (long press)"));
	for(QAction *tool : toolActions()) {
		addDirect(tool->objectName(), tr("Tool bar / tool drawer"));
	}
	const char *moreActions[] = {
		"zoomfit",		"zoomone",	  "rotatezero", "viewflip",
		"viewmirror",	"fullscreen", "smallscreenleftymode",
		"interfacescale", "preferences",
	};
	for(const char *name : moreActions) {
		addDirect(QString::fromLatin1(name), tr("More panel"));
	}
	const char *sessionActions[] = {
		"invitesession", "sessionsettings", "leavesession", "viewserverlog",
		"togglechat"};
	for(const char *name : sessionActions) {
		addDirect(QString::fromLatin1(name), tr("Session panel"));
	}
	const char *quickActions[] = {
		"newdocument",	  "opendocument", "savedocument",  "savedocumentas",
		"exportdocument", "exportanim",	  "maketimelapse", "preferences"};
	for(const char *name : quickActions) {
		addDirect(QString::fromLatin1(name), tr("Menu › quick actions"));
	}
	addDirect(QStringLiteral("openplayback"), tr("Projects hub › More"));
	addDirect(QStringLiteral("exitprogram"), tr("Projects hub › More"));

	QStringList hidden = {
		QStringLiteral("smallscreensidetoolbar"),
		QStringLiteral("smallscreenbottomtoolbar")};

	QJsonArray rows;
	QString md;
	QTextStream ts(&md);
	ts << "| # | objectName | Action | Original location | Mobile location | "
		  "Reachable |\n";
	ts << "|---|---|---|---|---|---|\n";
	int index = 0;
	int unreachable = 0;
	QList<QAction *> actions = mw->findChildren<QAction *>();
	std::sort(actions.begin(), actions.end(), [](QAction *a, QAction *b) {
		return a->objectName() < b->objectName();
	});
	for(QAction *action : actions) {
		if(action->isSeparator() || action->menu() ||
		   qobject_cast<QWidgetAction *>(action) || isMobileChrome(action)) {
			continue;
		}
		QString name = action->objectName();
		QString text = clean(action->text());
		if(name.isEmpty() && text.isEmpty()) {
			continue;
		}

		QStringList original;
		for(const QString &p : menuPaths.value(action)) {
			original.append(tr("Menu: %1").arg(p));
		}
		for(const QString &t : toolBarNames.value(action)) {
			original.append(tr("Tool bar: %1").arg(t));
		}
		docks::DockBase *dock = dockAncestor(action);
		if(dock) {
			original.append(tr("Dock: %1").arg(dock->windowTitle()));
		}
		bool shortcut = !action->shortcuts().isEmpty();
		if(original.isEmpty()) {
			original.append(
				shortcut ? tr("Keyboard shortcut only (%1)")
							   .arg(action->shortcut().toString(
								   QKeySequence::NativeText))
						 : tr("Internal / shortcut-configurable"));
		}

		QStringList mobile;
		bool listedInMenu = action->isVisible() && !hidden.contains(name);
		if(listedInMenu) {
			for(const QString &p : menuPaths.value(action)) {
				mobile.append(tr("Menu sheet › %1").arg(p));
			}
		}
		mobile.append(mobileDirect.value(name));
		if(dock) {
			for(const HostedDock &hd : m_docks) {
				if(hd.dock == dock) {
					mobile.append(tr("%1 panel (%2)").arg(
						hd.panelId, dock->windowTitle()));
				}
			}
		}
		if(mobile.isEmpty() && original.size() == 1 &&
		   original.first().startsWith(tr("Keyboard shortcut only").left(8))) {
			mobile.append(tr("Keyboard shortcut (unchanged)"));
		}
		if(mobile.isEmpty() && !shortcut && menuPaths.value(action).isEmpty() &&
		   toolBarNames.value(action).isEmpty() && !dock) {
			mobile.append(tr("Internal / shortcut-configurable (unchanged)"));
		}
		if(hidden.contains(name)) {
			mobile.append(tr("Not applicable: classic layout option"));
		}

		// Shown in the original small-screen interface? Desktop-only
		// actions are hidden there by upstream as well.
		bool reachable = !mobile.isEmpty();
		bool desktopOnly = !action->isVisible() && !menuPaths.value(action).isEmpty();
		if(desktopOnly && mobile.isEmpty()) {
			mobile.append(tr("Desktop mode only (hidden in upstream mobile "
							 "mode as well)"));
			reachable = true;
		}
		if(!reachable) {
			++unreachable;
		}

		++index;
		ts << "| " << index << " | `" << name << "` | " << text << " | "
		   << original.join(QStringLiteral("<br>")) << " | "
		   << mobile.join(QStringLiteral("<br>")) << " | "
		   << (reachable ? QStringLiteral("yes") : QStringLiteral("**NO**"))
		   << " |\n";

		QJsonObject row;
		row.insert(QStringLiteral("name"), name);
		row.insert(QStringLiteral("text"), text);
		row.insert(
			QStringLiteral("shortcut"),
			action->shortcut().toString(QKeySequence::PortableText));
		row.insert(QStringLiteral("checkable"), action->isCheckable());
		row.insert(QStringLiteral("visible"), action->isVisible());
		row.insert(QStringLiteral("original"), QJsonArray::fromStringList(original));
		row.insert(QStringLiteral("mobile"), QJsonArray::fromStringList(mobile));
		row.insert(QStringLiteral("reachable"), reachable);
		rows.append(row);
	}
	ts << "\nTotal: " << index << ", unreachable: " << unreachable << "\n";

	QFile mdFile(path + QStringLiteral(".md"));
	if(mdFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		mdFile.write(md.toUtf8());
	}
	QFile jsonFile(path + QStringLiteral(".json"));
	if(jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QJsonObject root;
		root.insert(QStringLiteral("active"), m_active);
		root.insert(QStringLiteral("actions"), rows);
		root.insert(QStringLiteral("unreachable"), unreachable);
		jsonFile.write(QJsonDocument(root).toJson());
	}
	qInfo("Mobile UI inventory: %d actions, %d unreachable, written to %s",
		  index, unreachable, qUtf8Printable(path));
}

}
