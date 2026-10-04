// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/mobile/shell.h"
#include "desktop/chat/chatbox.h"
#include "desktop/docks/dockbase.h"
#include "desktop/docks/toolsettingsdock.h"
#include "desktop/mainwindow.h"
#include "desktop/mobile/chrome.h"
#include "desktop/mobile/commandsheet.h"
#include "desktop/mobile/hub.h"
#include "desktop/mobile/panels.h"
#include "desktop/mobile/sheet.h"
#include "desktop/mobile/theme.h"
#include "desktop/mobile/widgets.h"
#include "desktop/view/canvaswrapper.h"
#include "desktop/utils/widgetutils.h"
#include "desktop/widgets/kis_slider_spin_box.h"
#include "desktop/widgets/viewstatusbar.h"
#include "libclient/document.h"
#include "libclient/net/client.h"
#include "libclient/tools/tool.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace mobile {

namespace {

struct DockPlacement {
	const char *objectName;
	const char *panelId;
	const char *tabId;
	const char *iconName;
	bool scrollable;
};

// Where each upstream dock goes. Docks that aren't listed here (e.g. new ones
// added upstream in the future) end up in the "panels" section of the more
// panel automatically, so they never become unreachable.
constexpr DockPlacement DOCK_PLACEMENTS[] = {
	{"ToolSettings", "brush", "tool", "sliders", true},
	{"BrushPalette", "brush", "brushes", "brush", false},
	{"colorspinnerdock", "color", "wheel", "theme:drawpile_colorwheel", false},
	{"colorsliderdock", "color", "sliders", "theme:drawpile_colorsliders",
	 true},
	{"colorpalettedock", "color", "palette", "theme:drawpile_colorpalette",
	 false},
	{"colorcircledock", "color", "circle", "theme:drawpile_colorcircle", false},
	{"LayerList", "layers", "layers", "layers", false},
	{"Timeline", "animation", "timeline", "timeline", false},
	{"onionskins", "animation", "onion", "theme:onion-on", true},
	{"navigatordock", "view", "navigator", "navigator", false},
	{"referencedock", "view", "reference", "reference", false},
};

// Which panel wins when upstream code shows several docks at once.
int panelPriority(const QString &panelId)
{
	static const QStringList order = {
		QStringLiteral("layers"),	 QStringLiteral("brush"),
		QStringLiteral("animation"), QStringLiteral("chat"),
		QStringLiteral("color"),	 QStringLiteral("view"),
	};
	int i = order.indexOf(panelId);
	return i == -1 ? order.size() : i;
}

bool isBrushTool(int tool)
{
	switch(tool) {
	case tools::Tool::FREEHAND:
	case tools::Tool::ERASER:
	case tools::Tool::LINE:
	case tools::Tool::RECTANGLE:
	case tools::Tool::ELLIPSE:
	case tools::Tool::BEZIER:
		return true;
	default:
		return false;
	}
}

QString stripMnemonic(QString text)
{
	text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
	text.remove(QLatin1Char('&'));
	text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
	return text;
}

}

Shell::Shell(MainWindow *mw)
	: QObject(mw)
	, m_mw(mw)
{
	setObjectName(QStringLiteral("mobileShell"));
	connect(
		mw, &MainWindow::smallScreenModeChanged, this,
		&Shell::handleSmallScreenModeChanged);
	if(mw->isSmallScreenMode()) {
		handleSmallScreenModeChanged(true);
	}
	addInterfaceToggle();
	startTestDriver(mw);
	QByteArray inventoryPath = qgetenv("DRAWPILE_MOBILE_INVENTORY");
	if(!inventoryPath.isEmpty()) {
		QTimer::singleShot(3000, this, [this, inventoryPath] {
			dumpInventory(QString::fromLocal8Bit(inventoryPath));
		});
	}
}

Shell::~Shell()
{
	if(m_active) {
		qApp->removeEventFilter(this);
	}
}

Shell *Shell::of(MainWindow *mw)
{
	return mw ? mw->findChild<Shell *>(
					QStringLiteral("mobileShell"), Qt::FindDirectChildrenOnly)
			  : nullptr;
}

bool Shell::showHub(int startDialogPage)
{
	if(!m_active || !m_mw) {
		return false;
	}
	if(!m_hub) {
		m_hub = new Hub(m_mw, m_mw);
		m_hub->setStyleSheet(chromeStyleSheet());
		connect(m_hub, &Hub::closeRequested, this, &Shell::hideHub);
	}
	if(m_sheet) {
		m_sheet->close();
	}
	m_hub->setGeometry(m_mw->rect());
	m_hub->showForStartPage(startDialogPage);
	m_hub->raise();
	m_hub->show();
	return true;
}

void Shell::hideHub()
{
	if(m_hub) {
		m_hub->hide();
	}
}

void Shell::openPanel(const QString &panelId, const QString &tabId)
{
	if(m_sheet) {
		m_sheet->open(panelId, tabId);
	}
}

void Shell::closePanel()
{
	if(m_sheet) {
		m_sheet->close();
	}
}

void Shell::beforeInterfaceModeChange(bool smallScreenMode)
{
	if(!smallScreenMode && m_active) {
		deactivate();
	}
}

bool Shell::eventFilter(QObject *watched, QEvent *event)
{
	QEvent::Type type = event->type();

	// Android back button: close the sheet, otherwise go back to the hub.
	if(type == QEvent::KeyPress || type == QEvent::KeyRelease) {
		QKeyEvent *ke = static_cast<QKeyEvent *>(event);
		if(ke->key() == Qt::Key_Back && m_active && m_mw &&
		   !QApplication::activeModalWidget() &&
		   !QApplication::activePopupWidget() &&
		   (QApplication::activeWindow() == m_mw ||
			!QApplication::activeWindow())) {
			bool hubVisible = m_hub && m_hub->isVisible();
			if(hubVisible) {
				if(m_hub->handleBack(type == QEvent::KeyRelease)) {
					return true;
				}
				return false;
			}
			if(type == QEvent::KeyRelease) {
				if(m_sheet && m_sheet->isOpen()) {
					m_sheet->close();
				} else if(m_interfaceHidden) {
					setInterfaceHidden(false);
				} else {
					showHub(-1);
				}
			}
			return true;
		}
		return false;
	}

	if(!m_active || !m_mw) {
		return QObject::eventFilter(watched, event);
	}

	if(watched == m_mw) {
		switch(type) {
		case QEvent::Resize:
			updateLayout();
			if(m_hub && m_hub->isVisible()) {
				m_hub->setGeometry(m_mw->rect());
			}
			break;
		case QEvent::WindowTitleChange:
		case QEvent::ModifiedChange:
			updateTitle();
			break;
		case QEvent::PaletteChange:
		case QEvent::StyleChange:
			if(!m_themeRefreshPending) {
				m_themeRefreshPending = true;
				QTimer::singleShot(0, this, &Shell::reapplyTheme);
			}
			break;
		default:
			break;
		}
		return false;
	}

	if(watched == m_mw->centralWidget()) {
		if(type == QEvent::Resize || type == QEvent::Move) {
			updateOverlays();
		}
		return false;
	}

	if(watched == m_mw->menuBar()) {
		if(type == QEvent::Show && !m_internalChange) {
			QTimer::singleShot(0, this, [this] {
				if(m_active && m_mw) {
					m_mw->menuBar()->hide();
				}
			});
		}
		return false;
	}

	// Something in upstream moved one of our hosted widgets away (e.g. the
	// left-handed mode resetting the dock layout). Take it back.
	if(type == QEvent::ParentChange && !m_internalChange) {
		if(QWidget *widget = qobject_cast<QWidget *>(watched)) {
			reclaimLater(widget);
		}
		return false;
	}

	if((type == QEvent::ShowToParent || type == QEvent::HideToParent) &&
	   !m_internalChange) {
		QWidget *widget = qobject_cast<QWidget *>(watched);
		if(widget) {
			bool isHosted = widget == m_chatBox;
			for(const HostedDock &hd : m_docks) {
				if(hd.dock == widget) {
					isHosted = true;
					break;
				}
			}
			if(isHosted) {
				widget->setProperty(
					"mobileVisibilityRequest", type == QEvent::ShowToParent
												   ? QStringLiteral("show")
												   : QStringLiteral("hide"));
				scheduleDockVisibilityCheck();
			} else if(widget->property("mobileChatButton").toBool()) {
				// Upstream shows this button when there are unread messages.
				if(m_topBar) {
					m_topBar->setChatBadge(
						type == QEvent::ShowToParent ? QStringLiteral("!")
													 : QString());
				}
			}
		}
	}
	return QObject::eventFilter(watched, event);
}

void Shell::addInterfaceToggle()
{
	// A switch in the View menu, next to upstream's small-screen options, so
	// the classic layout can be turned back on (and off again) by the user.
	QAction *lefty = action("smallscreenleftymode");
	if(!lefty || !m_mw) {
		return;
	}
	QMenu *viewMenu = nullptr;
	std::function<QMenu *(QMenu *)> find = [&](QMenu *menu) -> QMenu * {
		for(QAction *a : menu->actions()) {
			if(a == lefty) {
				return menu;
			} else if(a->menu()) {
				if(QMenu *found = find(a->menu())) {
					return found;
				}
			}
		}
		return nullptr;
	};
	for(QAction *top : m_mw->menuBar()->actions()) {
		if(top->menu() && (viewMenu = find(top->menu()))) {
			break;
		}
	}
	if(!viewMenu) {
		return;
	}
	QAction *toggle = new QAction(tr("Drawpile Mobile interface"), m_mw);
	toggle->setObjectName(QStringLiteral("mobileuitoggle"));
	toggle->setCheckable(true);
	toggle->setChecked(
		!QSettings().value(QStringLiteral("mobileui/classic"), false).toBool());
	toggle->setStatusTip(
		tr("Use the touch-friendly interface of this fork in small-screen "
		   "mode. Takes effect after restarting Drawpile."));
	QList<QAction *> actions = viewMenu->actions();
	int index = actions.indexOf(lefty);
	QAction *before = actions.value(index + 1, nullptr);
	viewMenu->insertAction(before, toggle);
	auto sync = [toggle, lefty] {
		toggle->setVisible(lefty->isVisible());
		toggle->setEnabled(lefty->isEnabled());
	};
	sync();
	connect(lefty, &QAction::changed, toggle, sync);
	connect(toggle, &QAction::triggered, this, [this](bool checked) {
		QSettings().setValue(QStringLiteral("mobileui/classic"), !checked);
		showMessage(tr("Restart Drawpile to switch the interface."));
	});
}

void Shell::handleSmallScreenModeChanged(bool smallScreenMode)
{
	QSettings settings;
	bool classic = settings.value(QStringLiteral("mobileui/classic"), false)
					   .toBool() ||
				   qEnvironmentVariableIsSet("DRAWPILE_MOBILE_CLASSIC");
	if(smallScreenMode && !classic && !m_active) {
		activate();
	} else if(!smallScreenMode && m_active) {
		deactivate();
	} else if(smallScreenMode && m_active) {
		// Upstream re-applied its own small-screen state, re-assert ours.
		if(m_mw) {
			m_mw->canvasWrapper()->setShowToggleItems(false, false);
		}
		updateLayout();
	}
}

void Shell::activate()
{
	if(m_active || !m_mw) {
		return;
	}
	m_active = true;
	MainWindow *mw = m_mw;
	bool updatesWereEnabled = mw->updatesEnabled();
	mw->setUpdatesEnabled(false);
	Theme::refresh();

	m_parking = new QWidget(mw);
	m_parking->setObjectName(QStringLiteral("mobileParking"));
	m_parking->hide();
	m_parking->setGeometry(0, 0, 0, 0);

	{
		QScopedValueRollback<bool> rollback(m_internalChange, true);
		mw->menuBar()->hide();
	}
	mw->menuBar()->installEventFilter(this);

	parkToolBars();
	createChrome();
	adoptDocks();
	adoptStatusAndChat();
	buildMorePanel();
	bindDocument();

	// The edge toggles on the canvas are replaced by the tool rail.
	mw->canvasWrapper()->setShowToggleItems(false, false);
	// Gesture scrolling instead of tiny desktop scroll bars.
	m_canvasScrollArea = mw->canvasWrapper()->viewWidget();
	if(m_canvasScrollArea) {
		m_hScrollPolicy = int(m_canvasScrollArea->horizontalScrollBarPolicy());
		m_vScrollPolicy = int(m_canvasScrollArea->verticalScrollBarPolicy());
		m_canvasScrollArea->setHorizontalScrollBarPolicy(
			Qt::ScrollBarAlwaysOff);
		m_canvasScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	}

	mw->installEventFilter(this);
	if(mw->centralWidget()) {
		mw->centralWidget()->installEventFilter(this);
	}
	qApp->installEventFilter(this);

	updateLayout();
	updateTitle();
	updateConnection();
	updateColors();
	updateQuickSliderTargets();
	mw->setUpdatesEnabled(updatesWereEnabled);
}

void Shell::deactivate()
{
	if(!m_active || !m_mw) {
		return;
	}
	MainWindow *mw = m_mw;
	qApp->removeEventFilter(this);
	mw->removeEventFilter(this);
	if(mw->centralWidget()) {
		mw->centralWidget()->removeEventFilter(this);
	}
	mw->menuBar()->removeEventFilter(this);

	if(m_sheet) {
		m_sheet->setAnimationsEnabled(false);
		m_sheet->close();
	}
	if(m_commands) {
		m_commands->leave();
	}
	hideHub();

	releaseDocks();
	releaseStatusAndChat();
	unparkToolBars();
	destroyChrome();

	if(m_canvasScrollArea) {
		m_canvasScrollArea->setHorizontalScrollBarPolicy(
			Qt::ScrollBarPolicy(m_hScrollPolicy));
		m_canvasScrollArea->setVerticalScrollBarPolicy(
			Qt::ScrollBarPolicy(m_vScrollPolicy));
	}
	{
		QScopedValueRollback<bool> rollback(m_internalChange, true);
		mw->menuBar()->show();
	}
	delete m_parking;
	m_interfaceHidden = false;
	m_active = false;
}

void Shell::createChrome()
{
	MainWindow *mw = m_mw;
	QString styleSheet = chromeStyleSheet();

	m_topBar = new TopBar(action("undo"), action("redo"));
	m_topHolder = new QToolBar(mw);
	m_topHolder->setObjectName(QStringLiteral("mobileTopBarHolder"));
	m_topHolder->setMovable(false);
	m_topHolder->setFloatable(false);
	m_topHolder->setContextMenuPolicy(Qt::PreventContextMenu);
	m_topHolder->toggleViewAction()->setVisible(false);
	m_topHolder->setStyleSheet(styleSheet);
	m_topHolder->layout()->setContentsMargins(0, 0, 0, 0);
	m_topHolder->layout()->setSpacing(0);
	m_topHolder->addWidget(m_topBar);
	mw->addToolBar(Qt::TopToolBarArea, m_topHolder);

	m_rail = new ToolRail(toolActions());
	m_railHolder = new QToolBar(mw);
	m_railHolder->setObjectName(QStringLiteral("mobileToolRailHolder"));
	m_railHolder->setMovable(false);
	m_railHolder->setFloatable(false);
	m_railHolder->setContextMenuPolicy(Qt::PreventContextMenu);
	m_railHolder->toggleViewAction()->setVisible(false);
	m_railHolder->setStyleSheet(styleSheet);
	m_railHolder->layout()->setContentsMargins(0, 0, 0, 0);
	m_railHolder->layout()->setSpacing(0);
	m_railHolder->addWidget(m_rail);
	m_railArea = Qt::BottomToolBarArea;
	mw->addToolBar(m_railArea, m_railHolder);

	m_sliders = new QuickSliders(action("toolpicker"), mw);
	m_sliders->setStyleSheet(styleSheet);
	m_bubble = new ValueBubble(mw);
	m_bubble->setStyleSheet(styleSheet);
	connect(
		m_sliders, &QuickSliders::dragStateChanged, this,
		[this](bool dragging, const QString &label) {
			if(!m_bubble || !m_sliders) {
				return;
			}
			m_bubble->setText(label);
			m_bubble->setVisible(dragging);
			if(dragging) {
				QRect s = m_sliders->geometry();
				int x = isLeftHanded() ? s.left() - m_bubble->width() - dp(8)
									   : s.right() + dp(8);
				m_bubble->move(x, s.center().y() - m_bubble->height() / 2);
				m_bubble->raise();
			}
		});

	ChromeButton *restore =
		new ChromeButton(QStringLiteral("fullscreen"), tr("Show interface"), mw);
	restore->setStyleSheet(styleSheet);
	restore->setAutoFillBackground(false);
	restore->setFloating(true);
	restore->hide();
	connect(restore, &ChromeButton::clicked, this, [this] {
		setInterfaceHidden(false);
	});
	m_restoreButton = restore;

	m_sheet = new Sheet(mw);
	m_sheet->setStyleSheet(styleSheet);
	m_sheet->setAnimationsEnabled(
		QSettings()
			.value(QStringLiteral("mobileui/animations"), true)
			.toBool());

	// Panels that are always there; dock panels get added by adoptDocks.
	m_sheet->addPanel(QStringLiteral("brush"), tr("Brush"));
	m_sheet->addPanel(QStringLiteral("color"), tr("Color"));
	m_sheet->addPanel(QStringLiteral("layers"), tr("Layers"));
	m_sheet->addPanel(QStringLiteral("animation"), tr("Animation"));
	m_sheet->addPanel(QStringLiteral("view"), tr("Navigator & reference"));
	m_sheet->addPanel(QStringLiteral("chat"), tr("Chat"));

	m_drawer = new ToolDrawer(toolActions());
	m_sheet->addTab(
		QStringLiteral("tools"), QStringLiteral("tools"), tr("Tools"),
		QIcon(), m_drawer, true);
	m_sheet->setPanelTitle(QStringLiteral("tools"), tr("Tools"));
	connect(m_drawer, &ToolDrawer::toolChosen, this, [this](QAction *) {
		if(m_sheet) {
			m_sheet->close();
		}
	});
	connect(m_drawer, &ToolDrawer::toolSettingsRequested, this, [this] {
		openPanel(QStringLiteral("brush"), QStringLiteral("tool"));
	});

	m_commands = new CommandBrowser(mw->menuBar());
	m_commands->setHiddenActionNames(
		{QStringLiteral("smallscreensidetoolbar"),
		 QStringLiteral("smallscreenbottomtoolbar")});
	m_commands->setQuickActions(
		{action("newdocument"), action("opendocument"), action("savedocument"),
		 action("savedocumentas"), action("exportdocument"),
		 action("exportanim"), action("maketimelapse"), action("preferences")});
	m_sheet->addTab(
		QStringLiteral("menu"), QStringLiteral("menu"), tr("Menu"), QIcon(),
		m_commands, false);
	m_sheet->setPanelTitle(QStringLiteral("menu"), tr("Menu"));
	connect(
		m_commands, &CommandBrowser::closeRequested, m_sheet, &Sheet::close);

	m_more = new MorePanel;
	m_sheet->addTab(
		QStringLiteral("more"), QStringLiteral("more"), tr("More"), QIcon(),
		m_more, true);
	m_sheet->setPanelTitle(QStringLiteral("more"), tr("More"));

	m_statusPanel = new StatusPanel;
	m_sheet->addTab(
		QStringLiteral("status"), QStringLiteral("status"), tr("Session"),
		QIcon(), m_statusPanel, true);
	m_sheet->setPanelTitle(QStringLiteral("status"), tr("Session"));

	connect(m_sheet, &Sheet::opened, this, [this](const QString &panelId) {
		if(panelId == QStringLiteral("menu") && m_commands) {
			m_commands->reset();
		}
		if(panelId == QStringLiteral("status") && m_statusBar) {
			QScopedValueRollback<bool> rollback(m_internalChange, true);
			m_statusBar->show();
		}
		updatePanelButtons();
	});
	connect(m_sheet, &Sheet::closed, this, [this](const QString &panelId) {
		if(panelId == QStringLiteral("menu") && m_commands) {
			m_commands->leave();
		}
		updatePanelButtons();
		// Give the keyboard focus back to the canvas.
		if(m_mw && m_mw->canvasWrapper()) {
			m_mw->canvasWrapper()->viewWidget()->setFocus();
		}
	});
	connect(
		m_sheet, &Sheet::tabChanged, this,
		[this](const QString &panelId, const QString &tabId) {
			m_lastTabs.insert(panelId, tabId);
		});
	connect(
		m_sheet, &Sheet::coveredRectChanged, this, &Shell::updateOverlays);

	// Top bar.
	connect(m_topBar, &TopBar::projectsRequested, this, [this] {
		showHub(-1);
	});
	connect(m_topBar, &TopBar::menuRequested, this, [this] {
		m_sheet->toggle(QStringLiteral("menu"));
	});
	connect(m_topBar, &TopBar::moreRequested, this, [this] {
		m_sheet->toggle(QStringLiteral("more"));
	});
	connect(m_topBar, &TopBar::statusRequested, this, [this] {
		m_sheet->toggle(QStringLiteral("status"));
	});
	auto togglePanel = [this](const QString &panelId) {
		return [this, panelId] {
			m_sheet->toggle(panelId);
		};
	};
	connect(
		m_topBar, &TopBar::layersRequested, this,
		togglePanel(QStringLiteral("layers")));
	connect(
		m_topBar, &TopBar::animationRequested, this,
		togglePanel(QStringLiteral("animation")));
	connect(
		m_topBar, &TopBar::colorRequested, this,
		togglePanel(QStringLiteral("color")));

	// Tool rail.
	connect(
		m_rail, &ToolRail::drawerRequested, this,
		togglePanel(QStringLiteral("tools")));
	connect(
		m_rail, &ToolRail::layersRequested, this,
		togglePanel(QStringLiteral("layers")));
	connect(
		m_rail, &ToolRail::animationRequested, this,
		togglePanel(QStringLiteral("animation")));
	connect(
		m_rail, &ToolRail::colorRequested, this,
		togglePanel(QStringLiteral("color")));
	connect(m_rail, &ToolRail::toolSettingsRequested, this, [this] {
		m_sheet->toggle(QStringLiteral("brush"), QStringLiteral("tool"));
	});
	auto swapColors = [this] {
		if(QAction *swap = action("swapcolors")) {
			swap->trigger();
		}
	};
	connect(m_rail, &ToolRail::swapColorsRequested, this, swapColors);
	connect(m_topBar, &TopBar::swapColorsRequested, this, swapColors);

	// Keep the rail in sync with whatever tool is active.
	QVector<QAction *> tools = toolActions();
	if(!tools.isEmpty() && tools.first()->actionGroup()) {
		connect(
			tools.first()->actionGroup(), &QActionGroup::triggered, this,
			[this](QAction *a) {
				if(m_rail) {
					m_rail->noteToolUsed(a);
				}
			});
	}

	if(QAction *lefty = action("smallscreenleftymode")) {
		connect(lefty, &QAction::toggled, this, [this] {
			// Upstream resets its dock layout here; ours gets reclaimed by the
			// parent change handling, then the layout gets mirrored.
			QTimer::singleShot(0, this, [this] {
				if(m_active && m_mw) {
					m_mw->canvasWrapper()->setShowToggleItems(false, false);
					updateLayout();
				}
			});
		});
	}
}

void Shell::destroyChrome()
{
	MainWindow *mw = m_mw;
	if(m_topHolder) {
		mw->removeToolBar(m_topHolder);
		m_topHolder->deleteLater();
	}
	if(m_railHolder) {
		mw->removeToolBar(m_railHolder);
		m_railHolder->deleteLater();
	}
	if(m_sliders) {
		m_sliders->deleteLater();
	}
	if(m_bubble) {
		m_bubble->deleteLater();
	}
	if(m_restoreButton) {
		m_restoreButton->deleteLater();
	}
	if(m_sheet) {
		m_sheet->deleteLater();
	}
	if(m_hub) {
		m_hub->deleteLater();
	}
}

void Shell::adoptDocks()
{
	MainWindow *mw = m_mw;
	QScopedValueRollback<bool> rollback(m_internalChange, true);
	QList<docks::DockBase *> dockList = mw->findChildren<docks::DockBase *>(
		QString(), Qt::FindDirectChildrenOnly);
	QVector<docks::DockBase *> unknown;
	for(const DockPlacement &placement : DOCK_PLACEMENTS) {
		for(docks::DockBase *dock : dockList) {
			if(dock->objectName() == QLatin1String(placement.objectName)) {
				HostedDock hd;
				hd.dock = dock;
				hd.panelId = QString::fromLatin1(placement.panelId);
				hd.tabId = QString::fromLatin1(placement.tabId);
				hd.features = dock->features();
				mw->removeDockWidget(dock);
				dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
				m_sheet->addTab(
					hd.panelId, hd.tabId, dock->windowTitle(),
					mobile::icon(QString::fromLatin1(placement.iconName)), dock,
					placement.scrollable);
				dock->show();
				dock->installEventFilter(this);
				m_docks.append(hd);
			}
		}
	}
	for(docks::DockBase *dock : dockList) {
		bool known = false;
		for(const HostedDock &hd : m_docks) {
			if(hd.dock == dock) {
				known = true;
				break;
			}
		}
		if(!known) {
			HostedDock hd;
			hd.dock = dock;
			hd.panelId = QStringLiteral("other:%1").arg(dock->objectName());
			hd.tabId = dock->objectName();
			hd.features = dock->features();
			mw->removeDockWidget(dock);
			dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
			m_sheet->addPanel(hd.panelId, dock->fullTitle());
			m_sheet->addTab(
				hd.panelId, hd.tabId, dock->windowTitle(), dock->tabIcon(),
				dock, true);
			dock->show();
			dock->installEventFilter(this);
			m_docks.append(hd);
		}
	}
	// Panel titles follow the dock titles where there's only one dock.
	for(const HostedDock &hd : m_docks) {
		if(hd.panelId == QStringLiteral("layers") && hd.dock) {
			m_sheet->setPanelTitle(hd.panelId, hd.dock->windowTitle());
		}
	}
}

void Shell::releaseDocks()
{
	MainWindow *mw = m_mw;
	QScopedValueRollback<bool> rollback(m_internalChange, true);
	for(const HostedDock &hd : m_docks) {
		if(QDockWidget *dock = hd.dock) {
			dock->removeEventFilter(this);
			if(m_sheet) {
				m_sheet->takeTabContent(hd.panelId, hd.tabId);
			}
			dock->setParent(mw);
			dock->setFeatures(hd.features);
			mw->addDockWidget(Qt::LeftDockWidgetArea, dock);
			dock->hide();
		}
	}
	m_docks.clear();
}

void Shell::adoptStatusAndChat()
{
	MainWindow *mw = m_mw;
	QScopedValueRollback<bool> rollback(m_internalChange, true);

	// Status bar: lives in the central widget's layout below the canvas.
	m_statusBar = mw->findChild<widgets::ViewStatusBar *>();
	if(m_statusBar && mw->centralWidget()) {
		QBoxLayout *layout =
			qobject_cast<QBoxLayout *>(mw->centralWidget()->layout());
		m_statusBarIndex = layout ? layout->indexOf(m_statusBar) : -1;
		if(layout) {
			layout->removeWidget(m_statusBar);
		}
		m_statusPanel->setStatusWidget(m_statusBar);
		m_statusBar->installEventFilter(this);
		// Upstream shows this plain tool button when chat messages are
		// unread. Mirror that as a badge on the "more" button.
		for(QToolButton *button : m_statusBar->findChildren<QToolButton *>()) {
			if(qstrcmp(button->metaObject()->className(), "QToolButton") ==
			   0) {
				button->setProperty("mobileChatButton", true);
				button->installEventFilter(this);
			}
		}
	}

	// Chat: second widget in the canvas splitter.
	widgets::ChatBox *chat = mw->findChild<widgets::ChatBox *>();
	if(chat) {
		m_chatBox = chat;
		m_chatSplitter = qobject_cast<QSplitter *>(chat->parentWidget());
		m_chatIndex = m_chatSplitter ? m_chatSplitter->indexOf(chat) : -1;
		QWidget *page = new QWidget;
		QVBoxLayout *pageLayout = new QVBoxLayout(page);
		pageLayout->setContentsMargins(0, 0, 0, 0);
		pageLayout->addWidget(chat);
		chat->show();
		chat->installEventFilter(this);
		m_chatPage = page;
		m_sheet->addTab(
			QStringLiteral("chat"), QStringLiteral("chat"), tr("Chat"),
			mobile::icon(QStringLiteral("chat")), page, false);
	}
}

void Shell::releaseStatusAndChat()
{
	MainWindow *mw = m_mw;
	QScopedValueRollback<bool> rollback(m_internalChange, true);
	if(m_statusBar) {
		m_statusBar->removeEventFilter(this);
		if(m_statusPanel) {
			m_statusPanel->takeStatusWidget();
		}
		QBoxLayout *layout =
			mw->centralWidget()
				? qobject_cast<QBoxLayout *>(mw->centralWidget()->layout())
				: nullptr;
		if(layout) {
			layout->insertWidget(
				m_statusBarIndex < 0 ? layout->count() : m_statusBarIndex,
				m_statusBar);
		} else {
			m_statusBar->setParent(mw->centralWidget());
		}
		m_statusBar->show();
	}
	if(m_chatBox) {
		m_chatBox->removeEventFilter(this);
		if(m_chatSplitter) {
			m_chatSplitter->insertWidget(
				m_chatIndex < 0 ? m_chatSplitter->count() : m_chatIndex,
				m_chatBox);
		} else {
			m_chatBox->setParent(mw);
		}
		m_chatBox->hide();
	}
}

void Shell::parkToolBars()
{
	MainWindow *mw = m_mw;
	QScopedValueRollback<bool> rollback(m_internalChange, true);
	for(QToolBar *toolBar :
		mw->findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
		if(toolBar == m_topHolder || toolBar == m_railHolder) {
			continue;
		}
		ParkedToolBar parked;
		parked.toolBar = toolBar;
		parked.area = mw->toolBarArea(toolBar);
		parked.visible = !toolBar->isHidden();
		mw->removeToolBar(toolBar);
		toolBar->setParent(m_parking);
		toolBar->installEventFilter(this);
		m_toolBars.append(parked);
	}
}

void Shell::unparkToolBars()
{
	MainWindow *mw = m_mw;
	QScopedValueRollback<bool> rollback(m_internalChange, true);
	for(const ParkedToolBar &parked : m_toolBars) {
		if(QToolBar *toolBar = parked.toolBar) {
			toolBar->removeEventFilter(this);
			toolBar->setParent(mw);
			Qt::ToolBarArea area = parked.area == Qt::NoToolBarArea
									   ? Qt::TopToolBarArea
									   : parked.area;
			mw->addToolBar(area, toolBar);
			toolBar->setVisible(parked.visible);
		}
	}
	m_toolBars.clear();
}

void Shell::buildMorePanel()
{
	CardSection *panels = m_more->addSection(tr("Panels"));
	auto addPanelCard = [this, panels](
							const QString &iconName, const QString &title,
							const QString &panelId, const QString &tabId) {
		ActionCard *card = panels->addCard(iconName, title);
		connect(card, &ActionCard::clicked, this, [this, panelId, tabId] {
			openPanel(panelId, tabId);
		});
		return card;
	};
	addPanelCard(
		QStringLiteral("brush"), tr("Brushes"), QStringLiteral("brush"),
		QStringLiteral("brushes"));
	addPanelCard(
		QStringLiteral("sliders"), tr("Tool settings"),
		QStringLiteral("brush"), QStringLiteral("tool"));
	addPanelCard(
		QStringLiteral("navigator"), tr("Navigator"), QStringLiteral("view"),
		QStringLiteral("navigator"));
	addPanelCard(
		QStringLiteral("reference"), tr("Reference"), QStringLiteral("view"),
		QStringLiteral("reference"));
	addPanelCard(
		QStringLiteral("theme:onion-on"), tr("Onion skin"),
		QStringLiteral("animation"), QStringLiteral("onion"));
	addPanelCard(
		QStringLiteral("chat"), tr("Chat"), QStringLiteral("chat"),
		QStringLiteral("chat"));
	addPanelCard(
		QStringLiteral("status"), tr("Session"), QStringLiteral("status"),
		QStringLiteral("status"));
	for(const HostedDock &hd : m_docks) {
		if(hd.panelId.startsWith(QStringLiteral("other:")) && hd.dock) {
			ActionCard *card = addPanelCard(
				QString(), hd.dock->windowTitle(), hd.panelId, hd.tabId);
			if(docks::DockBase *db = qobject_cast<docks::DockBase *>(hd.dock)) {
				card->setThemeIcon(db->tabIcon());
			}
		}
	}

	CardSection *view = m_more->addSection(tr("View"));
	const char *viewActions[][2] = {
		{"zoomfit", "fullscreen"}, {"zoomone", "theme:zoom-original"},
		{"rotatezero", "theme:transform-rotate"}, {"viewflip", ""},
		{"viewmirror", ""}, {"fullscreen", ""},
	};
	for(const auto &va : viewActions) {
		if(QAction *a = action(va[0])) {
			view->addActionCard(a, QString::fromLatin1(va[1]));
		}
	}
	ActionCard *hideUi = view->addCard(QStringLiteral("hide-ui"), tr("Hide interface"));
	connect(hideUi, &ActionCard::clicked, this, [this] {
		if(m_sheet) {
			m_sheet->close();
		}
		setInterfaceHidden(true);
	});

	CardSection *ui = m_more->addSection(tr("Interface"));
	if(QAction *lefty = action("smallscreenleftymode")) {
		ui->addActionCard(lefty, QStringLiteral("swap"));
	}
	ActionCard *smaller = ui->addCard(QStringLiteral("resize"), tr("Smaller interface"));
	connect(smaller, &ActionCard::clicked, this, [this] {
		setUiScale(uiScale() - 0.1);
		showMessage(tr("Restart Drawpile to apply the new interface size."));
	});
	ActionCard *larger = ui->addCard(QStringLiteral("resize"), tr("Larger interface"));
	connect(larger, &ActionCard::clicked, this, [this] {
		setUiScale(uiScale() + 0.1);
		showMessage(tr("Restart Drawpile to apply the new interface size."));
	});
	ActionCard *animations = ui->addCard(QStringLiteral("play"), tr("Animations"));
	animations->setCheckable(true);
	animations->setChecked(
		QSettings().value(QStringLiteral("mobileui/animations"), true).toBool());
	connect(animations, &ActionCard::toggled, this, [this](bool checked) {
		QSettings().setValue(QStringLiteral("mobileui/animations"), checked);
		if(m_sheet) {
			m_sheet->setAnimationsEnabled(checked);
		}
	});
	if(QAction *scale = action("interfacescale")) {
		ui->addActionCard(scale, QStringLiteral("theme:zoom-select"));
	}
	if(QAction *prefs = action("preferences")) {
		ui->addActionCard(prefs, QStringLiteral("settings"));
	}
	ActionCard *classic =
		ui->addCard(QStringLiteral("classic"), tr("Classic layout"));
	connect(classic, &ActionCard::clicked, this, [this] {
		QMessageBox *box = utils::makeQuestion(
			m_mw, tr("Classic layout"),
			tr("Switch back to Drawpile's classic small-screen layout? This "
			   "takes effect after restarting Drawpile. You can switch back "
			   "in the View menu."));
		connect(box, &QMessageBox::accepted, this, [] {
			QSettings().setValue(QStringLiteral("mobileui/classic"), true);
		});
		box->show();
	});

	// Session actions in the status panel.
	CardSection *session = m_statusPanel->actions();
	const char *sessionActions[][2] = {
		{"invitesession", "share"},	 {"sessionsettings", "settings"},
		{"leavesession", "back"},	 {"viewserverlog", "info"},
		{"togglechat", "chat"},
	};
	for(const auto &sa : sessionActions) {
		if(QAction *a = action(sa[0])) {
			if(qstrcmp(sa[0], "togglechat") == 0) {
				ActionCard *card = session->addCard(
					QString::fromLatin1(sa[1]), stripMnemonic(a->text()));
				connect(card, &ActionCard::clicked, this, [this] {
					openPanel(QStringLiteral("chat"));
				});
			} else {
				session->addActionCard(a, QString::fromLatin1(sa[1]));
			}
		}
	}
}

void Shell::bindDocument()
{
	MainWindow *mw = m_mw;
	if(Document *doc = mw->findChild<Document *>()) {
		connect(
			doc, &Document::currentPathChanged, this, &Shell::updateTitle,
			Qt::UniqueConnection);
		connect(
			doc, &Document::dirtyCanvas, this, &Shell::updateTitle,
			Qt::UniqueConnection);
		connect(
			doc, &Document::sessionTitleChanged, this, &Shell::updateTitle,
			Qt::UniqueConnection);
		connect(
			doc, &Document::serverConnected, this, &Shell::updateConnection,
			Qt::UniqueConnection);
		connect(
			doc, &Document::serverDisconnected, this, &Shell::updateConnection,
			Qt::UniqueConnection);
	}
	if(docks::ToolSettings *ts = mw->findChild<docks::ToolSettings *>()) {
		connect(
			ts, &docks::ToolSettings::foregroundColorChanged, this,
			&Shell::updateColors, Qt::UniqueConnection);
		connect(
			ts, &docks::ToolSettings::backgroundColorChanged, this,
			&Shell::updateColors, Qt::UniqueConnection);
		connect(
			ts, &docks::ToolSettings::toolChanged, this,
			[this](tools::Tool::Type tool) {
				QVector<QAction *> tools = toolActions();
				QActionGroup *group =
					tools.isEmpty() ? nullptr : tools.first()->actionGroup();
				if(group && m_rail) {
					m_rail->noteToolUsed(group->actions().value(int(tool)));
				}
				updateQuickSliderTargets();
			});
		connect(
			ts, &docks::ToolSettings::activeBrushChanged, this,
			&Shell::updateQuickSliderTargets);
	}
}

void Shell::reapplyTheme()
{
	m_themeRefreshPending = false;
	if(!m_active) {
		return;
	}
	Theme::refresh();
	QString styleSheet = chromeStyleSheet();
	QWidget *widgets[] = {
		m_topHolder, m_railHolder, m_sliders, m_bubble, m_restoreButton,
		m_sheet,	 m_hub,
	};
	for(QWidget *w : widgets) {
		if(w) {
			w->setStyleSheet(styleSheet);
			w->update();
			for(QWidget *child : w->findChildren<QWidget *>()) {
				if(child->property("mobileChrome").toBool()) {
					child->update();
				}
			}
		}
	}
	if(m_sheet) {
		m_sheet->refreshTheme();
	}
}

void Shell::updateLayout()
{
	if(!m_active || !m_mw || !m_rail) {
		return;
	}
	MainWindow *mw = m_mw;
	bool landscape = isLandscape();
	bool lefty = isLeftHanded();

	Qt::ToolBarArea railArea = landscape ? (lefty ? Qt::RightToolBarArea
												  : Qt::LeftToolBarArea)
										 : Qt::BottomToolBarArea;
	if(railArea != m_railArea) {
		m_railArea = railArea;
		mw->addToolBar(railArea, m_railHolder);
	}
	m_rail->setOrientation(landscape ? Qt::Vertical : Qt::Horizontal);
	m_rail->setLeftHanded(lefty);
	m_rail->setShowPanelButtons(!landscape);
	m_topBar->setShowPanelButtons(landscape);

	int button = dp(48);
	int slotCount;
	if(landscape) {
		int length = mw->height() - m_topBar->sizeHint().height() - dp(8);
		slotCount = (length - button) / dp(44);
	} else {
		int length = mw->width() - dp(8);
		slotCount = (length - button * 4 - dp(9)) / dp(44);
	}
	m_rail->setSlotCount(qBound(2, slotCount, 8));

	m_railHolder->setVisible(!m_interfaceHidden);
	m_topHolder->setVisible(!m_interfaceHidden);
	updatePanelButtons();
	updateOverlays();
}

void Shell::updateOverlays()
{
	if(!m_active || !m_mw || !m_sheet || m_updatingOverlays) {
		return;
	}
	QScopedValueRollback<bool> rollback(m_updatingOverlays, true);
	MainWindow *mw = m_mw;
	QWidget *central = mw->centralWidget();
	if(!central) {
		return;
	}
	QRect area = central->geometry();
	bool landscape = isLandscape();
	bool lefty = isLeftHanded();
	bool side = landscape || isWide();
	m_sheet->setArea(
		area, side ? Sheet::Placement::Side : Sheet::Placement::Bottom, lefty);

	// Quick sliders along the edge, kept clear of the sheet.
	QRect free = area;
	QRect covered = m_sheet->coveredRect();
	if(!covered.isEmpty()) {
		if(m_sheet->placement() == Sheet::Placement::Bottom) {
			free.setBottom(qMin(free.bottom(), covered.top() - 1));
		}
	}
	bool showSliders = !m_interfaceHidden && m_sliders->hasTargets() &&
					   free.height() > dp(150);
	if(showSliders &&
	   m_sheet->placement() == Sheet::Placement::Side && !covered.isEmpty()) {
		// A side sheet on the same side as the sliders hides them.
		bool sheetLeft = covered.center().x() < area.center().x();
		bool slidersLeft = !lefty;
		if(sheetLeft == slidersLeft) {
			showSliders = false;
		}
	}
	if(showSliders) {
		m_sliders->fitHeight(qMin(free.height() - dp(24), dp(460)));
		QSize s = m_sliders->sizeHint();
		int x = lefty ? free.right() - s.width() - dp(6) : free.left() + dp(6);
		int y = free.top() + (free.height() - s.height()) / 2;
		m_sliders->setGeometry(x, y, s.width(), s.height());
		m_sliders->show();
		m_sliders->raise();
	} else {
		m_sliders->hide();
	}

	if(m_restoreButton) {
		QSize s = m_restoreButton->sizeHint();
		m_restoreButton->setGeometry(
			lefty ? area.left() + dp(8) : area.right() - s.width() - dp(8),
			area.top() + dp(8), s.width(), s.height());
		m_restoreButton->setVisible(m_interfaceHidden);
		m_restoreButton->raise();
	}

	// Stacking: canvas < sliders < sheet < bars < hub.
	if(m_sheet->isVisible()) {
		m_sheet->raise();
	}
	if(m_topHolder) {
		m_topHolder->raise();
	}
	if(m_railHolder) {
		m_railHolder->raise();
	}
	if(m_hub && m_hub->isVisible()) {
		m_hub->raise();
	}
}

void Shell::updateTitle()
{
	if(!m_topBar || !m_mw) {
		return;
	}
	QString title = m_mw->windowTitle();
	title.remove(QStringLiteral("[*]"));
	m_topBar->setTitle(title.trimmed(), m_mw->isWindowModified());
}

void Shell::updateConnection()
{
	if(!m_topBar || !m_mw) {
		return;
	}
	Document *doc = m_mw->findChild<Document *>();
	bool connected = doc && doc->client() && doc->client()->isConnected();
	m_topBar->setConnection(connected, tr("Online"));
	if(m_statusPanel) {
		m_statusPanel->setSessionText(
			connected ? tr("Connected to a session.")
					  : tr("Not connected. Host or join a session from the "
						   "projects screen or the Session menu."));
	}
}

void Shell::updateColors()
{
	if(!m_mw) {
		return;
	}
	if(docks::ToolSettings *ts = m_mw->findChild<docks::ToolSettings *>()) {
		QColor fg = ts->foregroundColor();
		QColor bg = ts->backgroundColor();
		if(m_rail) {
			m_rail->setColors(fg, bg);
		}
		if(m_topBar) {
			m_topBar->setColors(fg, bg);
		}
	}
}

void Shell::updateQuickSliderTargets()
{
	if(!m_sliders || !m_mw) {
		return;
	}
	docks::ToolSettings *ts = m_mw->findChild<docks::ToolSettings *>();
	KisSliderSpinBox *size = nullptr;
	KisSliderSpinBox *opacity = nullptr;
	qreal exponent = 1.0;
	if(ts && isBrushTool(int(ts->currentTool()))) {
		KisSliderSpinBox *radius = ts->findChild<KisSliderSpinBox *>(
			QStringLiteral("radiusLogarithmicBox"));
		KisSliderSpinBox *pixels = ts->findChild<KisSliderSpinBox *>(
			QStringLiteral("brushsizeBox"));
		// MyPaint brushes use the logarithmic radius, classic ones pixels.
		if(radius && !radius->isHidden()) {
			size = radius;
		} else if(pixels) {
			size = pixels;
			exponent = 3.0;
		}
		opacity = ts->findChild<KisSliderSpinBox *>(QStringLiteral("opacityBox"));
		if(opacity && opacity->isHidden()) {
			opacity = nullptr;
		}
	}
	m_sliders->setTargets(size, exponent, opacity);
	updateOverlays();
}

void Shell::updatePanelButtons()
{
	QString panel = m_sheet ? m_sheet->currentPanel() : QString();
	if(m_rail) {
		m_rail->setPanelChecked(panel);
	}
	if(m_topBar) {
		m_topBar->setPanelChecked(panel);
		m_topBar->setMenuChecked(panel == QStringLiteral("menu"));
		m_topBar->setMoreChecked(panel == QStringLiteral("more"));
	}
}

void Shell::setInterfaceHidden(bool hidden)
{
	m_interfaceHidden = hidden;
	updateLayout();
}

void Shell::scheduleDockVisibilityCheck()
{
	if(!m_dockCheckPending) {
		m_dockCheckPending = true;
		QTimer::singleShot(0, this, &Shell::checkDockVisibility);
	}
}

void Shell::checkDockVisibility()
{
	m_dockCheckPending = false;
	if(!m_active || !m_sheet) {
		return;
	}

	struct Hosted {
		QWidget *widget;
		QString panelId;
		QString tabId;
	};
	QVector<Hosted> hosted;
	for(const HostedDock &hd : m_docks) {
		if(hd.dock) {
			hosted.append({hd.dock, hd.panelId, hd.tabId});
		}
	}
	if(m_chatBox) {
		hosted.append(
			{m_chatBox, QStringLiteral("chat"), QStringLiteral("chat")});
	}

	const Hosted *bestShow = nullptr;
	bool currentHidden = false;
	for(const Hosted &h : hosted) {
		QString request = h.widget->property("mobileVisibilityRequest").toString();
		h.widget->setProperty("mobileVisibilityRequest", QVariant());
		bool shown = !h.widget->isHidden();
		if(request == QStringLiteral("show") && shown) {
			if(!bestShow ||
			   panelPriority(h.panelId) < panelPriority(bestShow->panelId)) {
				bestShow = &h;
			}
		} else if(request == QStringLiteral("hide") && !shown) {
			if(m_sheet->isOpen() && m_sheet->currentPanel() == h.panelId &&
			   m_sheet->currentTab() == h.tabId) {
				currentHidden = true;
			}
		}
	}

	if(bestShow) {
		m_sheet->open(bestShow->panelId, bestShow->tabId);
	} else if(currentHidden) {
		m_sheet->close();
	}

	// Hosted widgets always stay "shown" inside their tab; their visibility on
	// screen is controlled by the sheet.
	QScopedValueRollback<bool> rollback(m_internalChange, true);
	for(const Hosted &h : hosted) {
		if(h.widget->isHidden()) {
			h.widget->show();
		}
	}
}

void Shell::reclaimLater(QWidget *widget)
{
	QPointer<QWidget> guarded = widget;
	QTimer::singleShot(0, this, [this, guarded] {
		if(!guarded || !m_active || !m_mw || !m_sheet) {
			return;
		}
		QWidget *w = guarded;
		QScopedValueRollback<bool> rollback(m_internalChange, true);
		for(const HostedDock &hd : m_docks) {
			if(hd.dock == w) {
				if(w->parentWidget() == m_mw) {
					m_mw->removeDockWidget(hd.dock);
					hd.dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
				}
				m_sheet->reattachTabContent(hd.panelId, hd.tabId);
				w->show();
				return;
			}
		}
		for(const ParkedToolBar &parked : m_toolBars) {
			if(parked.toolBar == w && w->parentWidget() != m_parking) {
				m_mw->removeToolBar(parked.toolBar);
				w->setParent(m_parking);
				return;
			}
		}
		if(w == m_chatBox && m_chatPage &&
		   w->parentWidget() != m_chatPage.data()) {
			m_chatPage->layout()->addWidget(w);
			w->show();
		}
	});
}

void Shell::showMessage(const QString &message)
{
	if(m_mw) {
		m_mw->showPopupMessage(message);
	}
}

QAction *Shell::action(const char *name) const
{
	return m_mw ? m_mw->findChild<QAction *>(QString::fromLatin1(name))
				: nullptr;
}

QVector<QAction *> Shell::toolActions() const
{
	QVector<QAction *> result;
	QAction *brush = action("toolbrush");
	if(brush && brush->actionGroup()) {
		for(QAction *a : brush->actionGroup()->actions()) {
			result.append(a);
		}
	}
	return result;
}

bool Shell::isLeftHanded() const
{
	QAction *lefty = action("smallscreenleftymode");
	return lefty && lefty->isChecked();
}

bool Shell::isLandscape() const
{
	return m_mw && m_mw->width() > m_mw->height();
}

bool Shell::isWide() const
{
	return m_mw && qMin(m_mw->width(), m_mw->height()) >= dp(600);
}

}
