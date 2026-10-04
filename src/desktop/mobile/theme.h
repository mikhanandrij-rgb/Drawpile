// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): design tokens for the mobile interface.
#ifndef DESKTOP_MOBILE_THEME_H
#define DESKTOP_MOBILE_THEME_H
#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

namespace mobile {

// Color, spacing and typography tokens. The palette follows the application
// palette chosen in Preferences (dark themes get the dark tokens, light themes
// the light ones), so there is no separate theme setting to keep in sync.
struct Theme {
	bool dark = true;
	QColor background; // behind everything (hub, canvas surround)
	QColor surface;	   // bars, sheets
	QColor surface2;   // cards, inputs
	QColor surface3;   // pressed / hovered
	QColor outline;	   // hairlines
	QColor text;
	QColor textDim;
	QColor accent;
	QColor accentText; // text on accent
	QColor accentSoft; // selected backgrounds
	QColor danger;
	QColor shadow;

	static const Theme &current();
	// Recomputes the tokens from the current application palette.
	static void refresh();
};

// Converts density-independent pixels to logical pixels. On Android, Drawpile
// maps one logical pixel to one dp at the default interface scale, so this is
// identity times the mobile UI scale factor the user can adjust.
int dp(qreal value);
qreal uiScale();
void setUiScale(qreal scale);

// Touch target size, 48dp per Material guidelines.
inline int touchTarget() { return dp(48); }

// Corner radii.
inline int radiusLarge() { return dp(20); }
inline int radiusMedium() { return dp(14); }
inline int radiusSmall() { return dp(10); }

// Fonts sizes in dp.
enum class TextRole { Title, Subtitle, Body, Label, Caption };
void applyFont(QWidget *widget, TextRole role, bool bold = false);
int fontPixelSize(TextRole role);

// Style sheet applied to the mobile chrome (top bar, tool rail, sheets, hub).
// It is scoped by object names so the desktop interface is never affected.
QString chromeStyleSheet();

// Style sheet applied to the containers hosting upstream panels inside sheets.
// It only enlarges touch targets (scroll bars, check boxes, combo boxes).
QString panelStyleSheet();

// Icons. Names without a prefix are looked up in the fork's own icon set
// (":/mobileui/icons/<name>.svg", tinted to the given color); names starting
// with "theme:" use Drawpile's icon theme as-is.
QIcon icon(const QString &name);
QIcon icon(const QString &name, const QColor &color);
QPixmap iconPixmap(const QString &name, int sizeDp, const QColor &color);

}

#endif
