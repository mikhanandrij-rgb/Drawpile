// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): small building blocks for the mobile chrome.
#ifndef DESKTOP_MOBILE_WIDGETS_H
#define DESKTOP_MOBILE_WIDGETS_H
#include <QAbstractButton>
#include <QColor>
#include <QPointer>
#include <QTimer>

class QAction;

namespace mobile {

// Square, finger-sized icon button with a rounded highlight when checked or
// pressed. Supports long presses, a small badge and a color swatch mode.
class ChromeButton : public QAbstractButton {
	Q_OBJECT
public:
	explicit ChromeButton(QWidget *parent = nullptr);
	ChromeButton(const QString &iconName, const QString &text, QWidget *parent = nullptr);

	void setIconName(const QString &iconName);
	void setThemeIcon(const QIcon &icon);
	void setBadge(const QString &badge);
	void setSwatch(const QColor &color, const QColor &secondary = QColor());
	void setButtonSize(int sizeDp);
	void setShowLabel(bool showLabel);
	// Draws a round backdrop, for buttons floating over the canvas.
	void setFloating(bool floating);
	// Mirrors enabled, checked, tool tip and visibility from the action and
	// triggers it when clicked.
	void bindAction(QAction *action);
	QAction *boundAction() const { return m_action; }
	void refreshIcon();

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void longPressed();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	void syncFromAction();

	QString m_iconName;
	QIcon m_themeIcon;
	bool m_useThemeIcon = false;
	QString m_badge;
	QColor m_swatch;
	QColor m_swatchSecondary;
	int m_sizeDp = 48;
	bool m_showLabel = false;
	bool m_floating = false;
	bool m_longPressFired = false;
	QTimer m_longPressTimer;
	QPointer<QAction> m_action;
	QMetaObject::Connection m_clickConnection;
};

// Rounded label used for the connection status in the top bar.
class Pill final : public QAbstractButton {
	Q_OBJECT
public:
	explicit Pill(QWidget *parent = nullptr);
	void setDotColor(const QColor &color);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QColor m_dot;
};

// Large tappable card with an icon, a title and an optional subtitle. Used by
// the project hub and the "more" panel.
class ActionCard final : public QAbstractButton {
	Q_OBJECT
public:
	ActionCard(
		const QString &iconName, const QString &title,
		const QString &subtitle = QString(), QWidget *parent = nullptr);
	void setSubtitle(const QString &subtitle);
	void setBadge(const QString &badge);
	void setCompact(bool compact);
	void setThemeIcon(const QIcon &icon);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString m_iconName;
	QIcon m_themeIcon;
	QString m_subtitle;
	QString m_badge;
	bool m_compact = false;
};

}

#endif
