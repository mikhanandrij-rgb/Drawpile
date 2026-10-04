// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): top bar, tool rail and quick sliders.
#ifndef DESKTOP_MOBILE_CHROME_H
#define DESKTOP_MOBILE_CHROME_H
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

class KisSliderSpinBox;
class QAction;
class QBoxLayout;
class QLabel;

namespace mobile {

class ChromeButton;
class Pill;

// Compact bar at the top of the editor, replacing the desktop menu bar.
class TopBar final : public QWidget {
	Q_OBJECT
public:
	explicit TopBar(QAction *undo, QAction *redo, QWidget *parent = nullptr);

	void setTitle(const QString &title, bool dirty);
	void setConnection(bool connected, const QString &text);
	void setMenuChecked(bool checked);
	void setMoreChecked(bool checked);
	void setChatBadge(const QString &badge);
	void setLeftHanded(bool leftHanded);
	// In landscape, the panel buttons move from the tool rail to the top bar
	// so the vertical rail has room for more tools.
	void setShowPanelButtons(bool show);
	void setPanelChecked(const QString &panelId);
	void setColors(const QColor &foreground, const QColor &background);

	QSize sizeHint() const override;

signals:
	void projectsRequested();
	void menuRequested();
	void moreRequested();
	void statusRequested();
	void layersRequested();
	void animationRequested();
	void colorRequested();
	void swapColorsRequested();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	ChromeButton *m_projectsButton;
	ChromeButton *m_menuButton;
	QLabel *m_title;
	Pill *m_status;
	ChromeButton *m_undoButton;
	ChromeButton *m_redoButton;
	ChromeButton *m_moreButton;
	ChromeButton *m_layersButton;
	ChromeButton *m_animationButton;
	ChromeButton *m_colorButton;
};

// Tool bar along the bottom (portrait) or the side (landscape). Shows the
// most recently used tools, a button for the tool drawer and shortcuts to the
// most important panels.
class ToolRail final : public QWidget {
	Q_OBJECT
public:
	explicit ToolRail(
		const QVector<QAction *> &toolActions, QWidget *parent = nullptr);

	void setOrientation(Qt::Orientation orientation);
	Qt::Orientation orientation() const { return m_orientation; }
	// How many recently used tools fit into the rail.
	void setSlotCount(int count);
	int slotCount() const { return m_slotCount; }
	void setColors(const QColor &foreground, const QColor &background);
	void setPanelChecked(const QString &panelId);
	void setLeftHanded(bool leftHanded);
	void setShowPanelButtons(bool show);
	// Called when a tool is activated from anywhere (drawer, shortcut,
	// canvas). Puts it into the rail, replacing the least recently used one.
	void noteToolUsed(QAction *action);

	QSize sizeHint() const override;

signals:
	void toolSettingsRequested();
	void drawerRequested();
	void layersRequested();
	void animationRequested();
	void colorRequested();
	void swapColorsRequested();

private:
	void rebuild();
	void loadSlots();
	void saveSlots() const;
	QAction *actionByName(const QString &name) const;

	QVector<QAction *> m_toolActions;
	QStringList m_slots;	 // object names of the tools in the rail
	QVector<int> m_usage;	 // last-use counter per slot
	int m_usageCounter = 0;
	int m_slotCount = 4;
	Qt::Orientation m_orientation = Qt::Horizontal;
	bool m_leftHanded = false;
	bool m_showPanelButtons = true;
	QBoxLayout *m_layout;
	QVector<ChromeButton *> m_slotButtons;
	ChromeButton *m_drawerButton;
	ChromeButton *m_layersButton;
	ChromeButton *m_animationButton;
	ChromeButton *m_colorButton;
	QWidget *m_separator;
};

// One vertical slider that adjusts a value by relative dragging.
class QuickSlider final : public QWidget {
	Q_OBJECT
public:
	enum class Kind { Size, Opacity };
	explicit QuickSlider(Kind kind, QWidget *parent = nullptr);

	// Binds the slider to an upstream spin box, which remains the single
	// source of truth for the value.
	void setTarget(KisSliderSpinBox *target, qreal exponent);
	KisSliderSpinBox *target() const { return m_target; }
	void setLength(int length);
	QSize sizeHint() const override;

signals:
	void dragStateChanged(bool dragging, const QString &label);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	qreal fraction() const;
	void setFraction(qreal fraction);
	QString valueLabel() const;

	Kind m_kind;
	QPointer<KisSliderSpinBox> m_target;
	qreal m_exponent = 1.0;
	int m_length = 160;
	bool m_dragging = false;
	int m_dragStartY = 0;
	qreal m_dragStartFraction = 0.0;
};

// Edge overlay with brush size and opacity sliders plus an eyedropper.
class QuickSliders final : public QWidget {
	Q_OBJECT
public:
	explicit QuickSliders(QAction *picker, QWidget *parent = nullptr);

	void setTargets(
		KisSliderSpinBox *size, qreal sizeExponent, KisSliderSpinBox *opacity);
	// Lays the sliders out to fit into the given height.
	void fitHeight(int height);
	bool hasTargets() const;

signals:
	void dragStateChanged(bool dragging, const QString &label);

private:
	QuickSlider *m_size;
	QuickSlider *m_opacity;
	ChromeButton *m_picker;
};

// Floating label shown next to the quick sliders while dragging.
class ValueBubble final : public QWidget {
	Q_OBJECT
public:
	explicit ValueBubble(QWidget *parent = nullptr);
	void setText(const QString &text);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString m_text;
};

}

#endif
