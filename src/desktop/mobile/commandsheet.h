// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): touch-friendly browser for all main menu commands.
#ifndef DESKTOP_MOBILE_COMMANDSHEET_H
#define DESKTOP_MOBILE_COMMANDSHEET_H
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QAction;
class QLineEdit;
class QMenu;
class QMenuBar;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace mobile {

// Shows the main window's menu bar as a hierarchy of large rows, plus a
// search over every command. It is generated from the live QMenu objects
// every time it's opened, so nothing in the menus can become unreachable and
// new upstream menu entries show up automatically.
class CommandBrowser final : public QWidget {
	Q_OBJECT
public:
	CommandBrowser(QMenuBar *menuBar, QWidget *parent = nullptr);

	// Actions that should not be listed because the mobile layout replaces
	// them (e.g. toggles for the classic small-screen tool bars).
	void setHiddenActionNames(const QStringList &names);
	// Quick actions shown as cards on the root page.
	void setQuickActions(const QVector<QAction *> &actions);

	// Flattened list of every reachable action with its breadcrumb, used by
	// the action inventory dump.
	struct Entry {
		QAction *action;
		QString path;
	};
	QVector<Entry> allEntries() const;

public slots:
	void reset();
	void leave();

signals:
	void closeRequested();

protected:
	void showEvent(QShowEvent *event) override;

private:
	struct Page {
		QWidget *widget;
		QMenu *menu; // null for the root and search pages
		QString title;
	};

	void showRoot();
	void pushMenu(QMenu *menu, const QString &title);
	void popPage();
	void updateSearch(const QString &text);
	QWidget *makePage(
		const QString &title, bool withBack, QVBoxLayout **outContent);
	void addMenuRows(QVBoxLayout *layout, QMenu *menu);
	QWidget *makeRow(QAction *action, const QString &subtitle);
	QWidget *makeSubmenuRow(QMenu *menu, bool topLevel);
	void activate(QAction *action);
	bool isListed(QAction *action) const;
	void collectEntries(
		QMenu *menu, const QString &path, QVector<Entry> &out) const;
	void clearPages();
	void hideMenusUpTo(int depth);

	QPointer<QMenuBar> m_menuBar;
	QLineEdit *m_search;
	QStackedWidget *m_stack;
	QVector<Page> m_pages;
	QVector<QPointer<QMenu>> m_shownMenus;
	QStringList m_hiddenActionNames;
	QVector<QPointer<QAction>> m_quickActions;
};

}

#endif
