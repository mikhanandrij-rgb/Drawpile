// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/mobile/theme.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QSvgRenderer>
#include <QWidget>
#include <QtMath>

namespace mobile {

namespace {

Theme g_theme;
bool g_themeInitialized = false;
qreal g_uiScale = -1.0;

QColor mix(const QColor &a, const QColor &b, qreal t)
{
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * t,
		a.greenF() + (b.greenF() - a.greenF()) * t,
		a.blueF() + (b.blueF() - a.blueF()) * t,
		a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

qreal luminance(const QColor &c)
{
	return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

QString css(const QColor &c)
{
	if(c.alpha() == 255) {
		return c.name(QColor::HexRgb);
	} else {
		return QStringLiteral("rgba(%1,%2,%3,%4)")
			.arg(c.red())
			.arg(c.green())
			.arg(c.blue())
			.arg(c.alpha());
	}
}

// Renders one of our SVG icons with a solid color. The icon set uses
// "currentColor" for strokes and fills, which gets substituted here.
class TintedIconEngine final : public QIconEngine {
public:
	TintedIconEngine(const QByteArray &svg, const QColor &color)
		: m_svg(svg)
		, m_color(color)
	{
	}

	void paint(
		QPainter *painter, const QRect &rect, QIcon::Mode mode,
		QIcon::State state) override
	{
		Q_UNUSED(state);
		QColor color = colorFor(mode);
		QByteArray data = m_svg;
		data.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
		QSvgRenderer renderer(data);
		if(renderer.isValid()) {
			painter->save();
			painter->setOpacity(painter->opacity() * color.alphaF());
			renderer.render(painter, QRectF(rect));
			painter->restore();
		}
	}

	QPixmap
	pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
	{
		QPixmap pixmap(size);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		paint(&painter, QRect(QPoint(0, 0), size), mode, state);
		return pixmap;
	}

	QIconEngine *clone() const override
	{
		return new TintedIconEngine(m_svg, m_color);
	}

private:
	QColor colorFor(QIcon::Mode mode) const
	{
		if(mode == QIcon::Disabled) {
			QColor c = m_color;
			c.setAlphaF(c.alphaF() * 0.38);
			return c;
		}
		return m_color;
	}

	QByteArray m_svg;
	QColor m_color;
};

QByteArray loadSvg(const QString &name)
{
	static QHash<QString, QByteArray> cache;
	QHash<QString, QByteArray>::const_iterator it = cache.constFind(name);
	if(it != cache.constEnd()) {
		return it.value();
	}
	QFile file(QStringLiteral(":/mobileui/icons/%1.svg").arg(name));
	QByteArray data;
	if(file.open(QIODevice::ReadOnly)) {
		data = file.readAll();
	} else {
		qWarning("Mobile UI: missing icon '%s'", qUtf8Printable(name));
	}
	cache.insert(name, data);
	return data;
}

}

const Theme &Theme::current()
{
	if(!g_themeInitialized) {
		refresh();
	}
	return g_theme;
}

void Theme::refresh()
{
	g_themeInitialized = true;
	QPalette pal = QApplication::palette();
	QColor window = pal.color(QPalette::Window);
	Theme &t = g_theme;
	t.dark = window.lightness() < 128;

	QColor highlight = pal.color(QPalette::Highlight);
	// Very desaturated highlights (some themes use gray) make a poor accent.
	if(highlight.hsvSaturationF() < 0.15) {
		highlight = t.dark ? QColor(0x5b, 0xa8, 0xff) : QColor(0x1f, 0x6f, 0xeb);
	}

	if(t.dark) {
		t.background = QColor(0x10, 0x11, 0x14);
		t.surface = QColor(0x1b, 0x1c, 0x21);
		t.surface2 = QColor(0x25, 0x27, 0x2d);
		t.surface3 = QColor(0x31, 0x33, 0x3a);
		t.outline = QColor(0x3a, 0x3c, 0x44);
		t.text = QColor(0xf1, 0xf2, 0xf5);
		t.textDim = QColor(0xa3, 0xa6, 0xb0);
		t.danger = QColor(0xff, 0x6b, 0x6b);
		t.shadow = QColor(0, 0, 0, 140);
	} else {
		t.background = QColor(0xec, 0xee, 0xf2);
		t.surface = QColor(0xff, 0xff, 0xff);
		t.surface2 = QColor(0xf2, 0xf3, 0xf6);
		t.surface3 = QColor(0xe3, 0xe5, 0xea);
		t.outline = QColor(0xd6, 0xd9, 0xe0);
		t.text = QColor(0x17, 0x18, 0x1c);
		t.textDim = QColor(0x5b, 0x5f, 0x6b);
		t.danger = QColor(0xd3, 0x2f, 0x2f);
		t.shadow = QColor(0, 0, 0, 60);
	}
	t.accent = highlight;
	t.accentText =
		luminance(highlight) > 0.55 ? QColor(0x10, 0x11, 0x14) : Qt::white;
	t.accentSoft = mix(t.surface, highlight, t.dark ? 0.28 : 0.18);
}

qreal uiScale()
{
	if(g_uiScale <= 0.0) {
		QSettings settings;
		g_uiScale = qBound(
			0.75, settings.value(QStringLiteral("mobileui/scale"), 1.0).toReal(),
			1.5);
	}
	return g_uiScale;
}

void setUiScale(qreal scale)
{
	g_uiScale = qBound(0.75, scale, 1.5);
	QSettings settings;
	settings.setValue(QStringLiteral("mobileui/scale"), g_uiScale);
}

int dp(qreal value)
{
	return qRound(value * uiScale());
}

int fontPixelSize(TextRole role)
{
	switch(role) {
	case TextRole::Title:
		return dp(20);
	case TextRole::Subtitle:
		return dp(16);
	case TextRole::Body:
		return dp(15);
	case TextRole::Label:
		return dp(13);
	case TextRole::Caption:
		return dp(12);
	}
	return dp(15);
}

void applyFont(QWidget *widget, TextRole role, bool bold)
{
	QFont font = widget->font();
	font.setPixelSize(fontPixelSize(role));
	font.setBold(bold);
	widget->setFont(font);
}

QString chromeStyleSheet()
{
	const Theme &t = Theme::current();
	QString s;
	// Bars and sheets.
	s += QStringLiteral(
			 "QWidget#mobileTopBar, QWidget#mobileToolRail {"
			 " background: %1; }"
			 "QToolBar#mobileTopBarHolder, QToolBar#mobileToolRailHolder {"
			 " background: %1; border: none; padding: 0px; spacing: 0px; }")
			 .arg(css(t.surface));
	// Generic labels and buttons inside the chrome.
	s += QStringLiteral(
			 "QWidget[mobileChrome=\"true\"] { color: %1; }"
			 "QLabel[mobileRole=\"dim\"] { color: %2; }"
			 "QLabel[mobileRole=\"title\"] { color: %1; }")
			 .arg(css(t.text), css(t.textDim));
	// Text fields.
	s += QStringLiteral(
			 "QLineEdit[mobileChrome=\"true\"] {"
			 " background: %1; color: %2; border: 1px solid %3;"
			 " border-radius: %4px; padding: 0px %5px;"
			 " min-height: %6px; selection-background-color: %7; }"
			 "QLineEdit[mobileChrome=\"true\"]:focus { border-color: %7; }")
			 .arg(css(t.surface2), css(t.text), css(t.outline))
			 .arg(radiusSmall())
			 .arg(dp(12))
			 .arg(dp(44))
			 .arg(css(t.accent));
	// Scroll areas in our chrome are transparent with thin overlay bars.
	s += QStringLiteral(
			 "QScrollArea[mobileChrome=\"true\"] { background: transparent;"
			 " border: none; }"
			 "QScrollArea[mobileChrome=\"true\"] > QWidget > QWidget {"
			 " background: transparent; }"
			 "QScrollArea[mobileChrome=\"true\"] QScrollBar:vertical {"
			 " background: transparent; width: %1px; margin: 0px; }"
			 "QScrollArea[mobileChrome=\"true\"] QScrollBar::handle:vertical {"
			 " background: %2; border-radius: %3px; min-height: %4px; }"
			 "QScrollArea[mobileChrome=\"true\"] QScrollBar::add-line,"
			 "QScrollArea[mobileChrome=\"true\"] QScrollBar::sub-line {"
			 " height: 0px; width: 0px; }"
			 "QScrollArea[mobileChrome=\"true\"] QScrollBar::add-page,"
			 "QScrollArea[mobileChrome=\"true\"] QScrollBar::sub-page {"
			 " background: transparent; }")
			 .arg(dp(4))
			 .arg(css(t.outline))
			 .arg(dp(2))
			 .arg(dp(32));
	// Chips (tabs, presets, frame rates).
	s += QStringLiteral(
			 "QToolButton[mobileChip=\"true\"] {"
			 " background: %1; color: %2; border: 1px solid %3;"
			 " border-radius: %4px; padding: 0px %5px; min-height: %6px; }"
			 "QToolButton[mobileChip=\"true\"]:checked {"
			 " background: %7; color: %8; border-color: %8; }"
			 "QToolButton[mobileChip=\"true\"]:pressed { background: %9; }")
			 .arg(css(t.surface2), css(t.text), css(t.outline))
			 .arg(dp(18))
			 .arg(dp(12))
			 .arg(dp(36))
			 .arg(css(t.accentSoft), css(t.accent), css(t.surface3));
	// Buttons.
	s += QStringLiteral(
			 "QPushButton[mobileRole=\"primary\"] {"
			 " background: %1; color: %2; border: none; border-radius: %3px;"
			 " min-height: %4px; padding: 0px %5px; font-weight: bold; }"
			 "QPushButton[mobileRole=\"primary\"]:pressed { background: %6; }"
			 "QPushButton[mobileRole=\"primary\"]:disabled {"
			 " background: %7; color: %8; }"
			 "QPushButton[mobileRole=\"secondary\"] {"
			 " background: %7; color: %9; border: 1px solid %10;"
			 " border-radius: %3px; min-height: %4px; padding: 0px %5px; }"
			 "QPushButton[mobileRole=\"secondary\"]:pressed {"
			 " background: %11; }")
			 .arg(css(t.accent), css(t.accentText))
			 .arg(radiusSmall())
			 .arg(dp(48))
			 .arg(dp(20))
			 .arg(css(t.accent.darker(115)), css(t.surface2), css(t.textDim),
				  css(t.text), css(t.outline), css(t.surface3));
	// Number inputs in the hub.
	s += QStringLiteral(
			 "QSpinBox[mobileChrome=\"true\"] {"
			 " background: %1; color: %2; border: 1px solid %3;"
			 " border-radius: %4px; min-height: %5px; padding: 0px %6px; }")
			 .arg(css(t.surface2), css(t.text), css(t.outline))
			 .arg(radiusSmall())
			 .arg(dp(44))
			 .arg(dp(10));
	// Hub background.
	s += QStringLiteral("QWidget#mobileHub { background: %1; }")
			 .arg(css(t.background));
	return s;
}

QString panelStyleSheet()
{
	const Theme &t = Theme::current();
	// Only geometry-related tweaks, so that upstream widgets keep their look
	// but get finger-sized scroll bars and indicators.
	return QStringLiteral(
			   "QScrollBar:vertical { width: %1px; }"
			   "QScrollBar:horizontal { height: %1px; }"
			   "QScrollBar::handle:vertical { min-height: %2px; }"
			   "QScrollBar::handle:horizontal { min-width: %2px; }"
			   "QCheckBox::indicator, QRadioButton::indicator {"
			   " width: %3px; height: %3px; }"
			   "QComboBox { min-height: %4px; }"
			   "QToolButton { min-width: %5px; min-height: %5px; }"
			   "QAbstractItemView { selection-background-color: %6; }")
		.arg(dp(10))
		.arg(dp(40))
		.arg(dp(22))
		.arg(dp(40))
		.arg(dp(36))
		.arg(css(t.accentSoft));
}

QIcon icon(const QString &name)
{
	return icon(name, Theme::current().text);
}

QIcon icon(const QString &name, const QColor &color)
{
	if(name.startsWith(QStringLiteral("theme:"))) {
		return QIcon::fromTheme(name.mid(6));
	}
	QByteArray svg = loadSvg(name);
	if(svg.isEmpty()) {
		return QIcon();
	}
	return QIcon(new TintedIconEngine(svg, color));
}

QPixmap iconPixmap(const QString &name, int sizeDp, const QColor &color)
{
	qreal dpr = qApp->devicePixelRatio();
	int px = qCeil(dp(sizeDp) * dpr);
	QPixmap pixmap = icon(name, color).pixmap(QSize(px, px));
	pixmap.setDevicePixelRatio(dpr);
	return pixmap;
}

}
