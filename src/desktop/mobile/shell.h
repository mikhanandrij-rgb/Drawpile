// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): controller that turns the main window into the
// mobile, canvas-first interface while it is in small-screen mode.
#ifndef DESKTOP_MOBILE_SHELL_H
#define DESKTOP_MOBILE_SHELL_H
#include <QDockWidget>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

class MainWindow;
class QAbstractScrollArea;
class QAction;
class QSplitter;
class QStatusBar;
class QToolBar;
class QWidget;

namespace mobile {

class CommandBrowser;
class Hub;
class MorePanel;
class QuickSliders;
class Sheet;
class StatusPanel;
class ToolDrawer;
class ToolRail;
class TopBar;
class ValueBubble;

// Starts the scripted test driver if DRAWPILE_MOBILE_TEST_SCRIPT is set.
void startTestDriver(MainWindow *mw);

class Shell final : public QObject {
	Q_OBJECT
public:
	explicit Shell(MainWindow *mw);
	~Shell() override;

	static Shell *of(MainWindow *mw);

	bool isActive() const { return m_active; }

	// Shows the project hub. Returns false if the mobile interface is not
	// active, in which case the caller should fall back to the start dialog.
	bool showHub(int startDialogPage);
	void hideHub();

	// Opens a panel in the sheet.
	void openPanel(const QString &panelId, const QString &tabId = QString());
	void closePanel();

	void beforeInterfaceModeChange(bool smallScreenMode);

	// Writes the action inventory (Markdown and JSON) used for verification.
	void dumpInventory(const QString &path);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct HostedDock {
		QPointer<QDockWidget> dock;
		QString panelId;
		QString tabId;
		QDockWidget::DockWidgetFeatures features;
	};

	struct ParkedToolBar {
		QPointer<QToolBar> toolBar;
		Qt::ToolBarArea area;
		bool visible;
	};

	void addInterfaceToggle();
	void handleSmallScreenModeChanged(bool smallScreenMode);
	void activate();
	void deactivate();
	void createChrome();
	void destroyChrome();
	void adoptDocks();
	void releaseDocks();
	void adoptStatusAndChat();
	void releaseStatusAndChat();
	void parkToolBars();
	void unparkToolBars();
	void buildMorePanel();
	void bindDocument();
	void reapplyTheme();
	void updateLayout();
	void updateOverlays();
	void updateTitle();
	void updateConnection();
	void updateColors();
	void updateQuickSliderTargets();
	void updatePanelButtons();
	void setInterfaceHidden(bool hidden);
	void scheduleDockVisibilityCheck();
	void checkDockVisibility();
	void reclaimLater(QWidget *widget);
	void showMessage(const QString &message);

	QAction *action(const char *name) const;
	QVector<QAction *> toolActions() const;
	bool isLeftHanded() const;
	bool isLandscape() const;
	bool isWide() const;

	QPointer<MainWindow> m_mw;
	bool m_active = false;
	bool m_internalChange = false;
	bool m_interfaceHidden = false;
	bool m_dockCheckPending = false;
	bool m_updatingOverlays = false;
	bool m_themeRefreshPending = false;

	QPointer<QToolBar> m_topHolder;
	QPointer<QToolBar> m_railHolder;
	QPointer<TopBar> m_topBar;
	QPointer<ToolRail> m_rail;
	QPointer<Sheet> m_sheet;
	QPointer<QuickSliders> m_sliders;
	QPointer<ValueBubble> m_bubble;
	QPointer<QWidget> m_restoreButton;
	QPointer<QWidget> m_parking;
	QPointer<CommandBrowser> m_commands;
	QPointer<ToolDrawer> m_drawer;
	QPointer<MorePanel> m_more;
	QPointer<StatusPanel> m_statusPanel;
	QPointer<QWidget> m_chatPage;
	QPointer<Hub> m_hub;

	QVector<HostedDock> m_docks;
	QVector<ParkedToolBar> m_toolBars;
	QPointer<QStatusBar> m_statusBar;
	int m_statusBarIndex = -1;
	QPointer<QWidget> m_chatBox;
	QPointer<QSplitter> m_chatSplitter;
	int m_chatIndex = -1;
	QPointer<QAbstractScrollArea> m_canvasScrollArea;
	int m_hScrollPolicy = 0;
	int m_vScrollPolicy = 0;
	Qt::ToolBarArea m_railArea = Qt::BottomToolBarArea;
	QHash<QString, QString> m_lastTabs;
};

}

#endif
