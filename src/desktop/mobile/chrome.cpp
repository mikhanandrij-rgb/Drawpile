// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/mobile/chrome.h"
#include "desktop/mobile/theme.h"
#include "desktop/mobile/widgets.h"
#include "desktop/widgets/kis_slider_spin_box.h"
#include <QAction>
#include <QBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QtMath>

namespace mobile {

namespace {
QPoint eventPos(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return event->position().toPoint();
#else
	return event->pos();
#endif
}
}

TopBar::TopBar(QAction *undo, QAction *redo, QWidget *parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("mobileTopBar"));
	setProperty("mobileChrome", true);
	setAttribute(Qt::WA_StyledBackground, true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(dp(4), dp(2), dp(4), dp(2));
	layout->setSpacing(0);

	m_projectsButton =
		new ChromeButton(QStringLiteral("projects"), tr("Projects"));
	connect(
		m_projectsButton, &ChromeButton::clicked, this,
		&TopBar::projectsRequested);
	layout->addWidget(m_projectsButton);

	m_menuButton = new ChromeButton(QStringLiteral("menu"), tr("Menu"));
	m_menuButton->setCheckable(true);
	connect(
		m_menuButton, &ChromeButton::clicked, this, &TopBar::menuRequested);
	layout->addWidget(m_menuButton);

	m_title = new QLabel;
	m_title->setProperty("mobileRole", QStringLiteral("title"));
	m_title->setMinimumWidth(dp(40));
	m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_title->installEventFilter(this);
	applyFont(m_title, TextRole::Body, true);
	layout->addSpacing(dp(4));
	layout->addWidget(m_title, 1);

	m_status = new Pill;
	m_status->hide();
	connect(m_status, &Pill::clicked, this, &TopBar::statusRequested);
	layout->addWidget(m_status);

	m_undoButton = new ChromeButton(QStringLiteral("undo"), tr("Undo"));
	m_undoButton->bindAction(undo);
	layout->addWidget(m_undoButton);

	m_redoButton = new ChromeButton(QStringLiteral("redo"), tr("Redo"));
	m_redoButton->bindAction(redo);
	layout->addWidget(m_redoButton);

	m_layersButton = new ChromeButton(QStringLiteral("layers"), tr("Layers"));
	m_layersButton->setCheckable(true);
	m_layersButton->hide();
	connect(
		m_layersButton, &ChromeButton::clicked, this, &TopBar::layersRequested);
	layout->addWidget(m_layersButton);

	m_animationButton =
		new ChromeButton(QStringLiteral("timeline"), tr("Animation"));
	m_animationButton->setCheckable(true);
	m_animationButton->hide();
	connect(
		m_animationButton, &ChromeButton::clicked, this,
		&TopBar::animationRequested);
	layout->addWidget(m_animationButton);

	m_colorButton = new ChromeButton(QString(), tr("Color"));
	m_colorButton->setCheckable(true);
	m_colorButton->setSwatch(Qt::black, Qt::white);
	m_colorButton->hide();
	connect(
		m_colorButton, &ChromeButton::clicked, this, &TopBar::colorRequested);
	connect(
		m_colorButton, &ChromeButton::longPressed, this,
		&TopBar::swapColorsRequested);
	layout->addWidget(m_colorButton);

	m_moreButton = new ChromeButton(QStringLiteral("more"), tr("More"));
	m_moreButton->setCheckable(true);
	connect(
		m_moreButton, &ChromeButton::clicked, this, &TopBar::moreRequested);
	layout->addWidget(m_moreButton);
}

void TopBar::setTitle(const QString &title, bool dirty)
{
	QString text = dirty ? QStringLiteral("%1 •").arg(title) : title;
	m_title->setProperty("fullText", text);
	m_title->setToolTip(title);
	QFontMetrics fm(m_title->font());
	m_title->setText(fm.elidedText(text, Qt::ElideMiddle, m_title->width()));
}

void TopBar::setConnection(bool connected, const QString &text)
{
	m_status->setVisible(connected);
	m_status->setText(text);
	m_status->setDotColor(QColor(0x3d, 0xd6, 0x8c));
	m_status->updateGeometry();
}

void TopBar::setMenuChecked(bool checked)
{
	m_menuButton->setChecked(checked);
}

void TopBar::setMoreChecked(bool checked)
{
	m_moreButton->setChecked(checked);
}

void TopBar::setChatBadge(const QString &badge)
{
	m_moreButton->setBadge(badge);
}

void TopBar::setLeftHanded(bool leftHanded)
{
	Q_UNUSED(leftHanded);
}

void TopBar::setShowPanelButtons(bool show)
{
	m_layersButton->setVisible(show);
	m_animationButton->setVisible(show);
	m_colorButton->setVisible(show);
}

void TopBar::setPanelChecked(const QString &panelId)
{
	m_layersButton->setChecked(panelId == QStringLiteral("layers"));
	m_animationButton->setChecked(panelId == QStringLiteral("animation"));
	m_colorButton->setChecked(panelId == QStringLiteral("color"));
}

void TopBar::setColors(const QColor &foreground, const QColor &background)
{
	m_colorButton->setSwatch(foreground, background);
}

QSize TopBar::sizeHint() const
{
	return QSize(dp(360), dp(52));
}

bool TopBar::eventFilter(QObject *watched, QEvent *event)
{
	if(watched == m_title) {
		if(event->type() == QEvent::MouseButtonRelease) {
			emit menuRequested();
			return true;
		} else if(event->type() == QEvent::Resize) {
			QString text = m_title->property("fullText").toString();
			QFontMetrics fm(m_title->font());
			m_title->setText(
				fm.elidedText(text, Qt::ElideMiddle, m_title->width()));
		}
	}
	return QWidget::eventFilter(watched, event);
}

ToolRail::ToolRail(const QVector<QAction *> &toolActions, QWidget *parent)
	: QWidget(parent)
	, m_toolActions(toolActions)
{
	setObjectName(QStringLiteral("mobileToolRail"));
	setProperty("mobileChrome", true);
	setAttribute(Qt::WA_StyledBackground, true);

	m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
	m_layout->setContentsMargins(dp(4), dp(4), dp(4), dp(4));
	m_layout->setSpacing(0);

	m_drawerButton = new ChromeButton(QStringLiteral("drawer"), tr("All tools"));
	m_drawerButton->setCheckable(true);
	connect(
		m_drawerButton, &ChromeButton::clicked, this,
		&ToolRail::drawerRequested);

	m_layersButton = new ChromeButton(QStringLiteral("layers"), tr("Layers"));
	m_layersButton->setCheckable(true);
	connect(
		m_layersButton, &ChromeButton::clicked, this,
		&ToolRail::layersRequested);

	m_animationButton =
		new ChromeButton(QStringLiteral("timeline"), tr("Animation"));
	m_animationButton->setCheckable(true);
	connect(
		m_animationButton, &ChromeButton::clicked, this,
		&ToolRail::animationRequested);

	m_colorButton = new ChromeButton(QString(), tr("Color"));
	m_colorButton->setCheckable(true);
	m_colorButton->setSwatch(Qt::black, Qt::white);
	connect(
		m_colorButton, &ChromeButton::clicked, this, &ToolRail::colorRequested);
	connect(
		m_colorButton, &ChromeButton::longPressed, this,
		&ToolRail::swapColorsRequested);

	m_separator = new QWidget;
	m_separator->setAttribute(Qt::WA_StyledBackground, true);
	m_separator->setStyleSheet(QStringLiteral("background: %1;")
								   .arg(Theme::current().outline.name()));

	loadSlots();
	rebuild();
}

void ToolRail::setOrientation(Qt::Orientation orientation)
{
	if(orientation != m_orientation) {
		m_orientation = orientation;
		rebuild();
	}
}

void ToolRail::setSlotCount(int count)
{
	count = qBound(2, count, 10);
	if(count != m_slotCount) {
		m_slotCount = count;
		loadSlots();
		rebuild();
	}
}

void ToolRail::setColors(const QColor &foreground, const QColor &background)
{
	m_colorButton->setSwatch(foreground, background);
}

void ToolRail::setPanelChecked(const QString &panelId)
{
	m_drawerButton->setChecked(panelId == QStringLiteral("tools"));
	m_layersButton->setChecked(panelId == QStringLiteral("layers"));
	m_animationButton->setChecked(panelId == QStringLiteral("animation"));
	m_colorButton->setChecked(panelId == QStringLiteral("color"));
}

void ToolRail::setLeftHanded(bool leftHanded)
{
	if(leftHanded != m_leftHanded) {
		m_leftHanded = leftHanded;
		rebuild();
	}
}

void ToolRail::setShowPanelButtons(bool show)
{
	if(show != m_showPanelButtons) {
		m_showPanelButtons = show;
		rebuild();
	}
}

void ToolRail::noteToolUsed(QAction *action)
{
	if(!action) {
		return;
	}
	QString name = action->objectName();
	int index = m_slots.indexOf(name);
	if(index == -1) {
		// Replace the least recently used slot in place, so the positions of
		// the other tools don't jump around.
		int lru = 0;
		for(int i = 1; i < m_usage.size(); ++i) {
			if(m_usage[i] < m_usage[lru]) {
				lru = i;
			}
		}
		if(lru < m_slots.size()) {
			m_slots[lru] = name;
			index = lru;
			if(lru < m_slotButtons.size()) {
				ChromeButton *button = m_slotButtons[lru];
				button->setThemeIcon(action->icon());
				button->bindAction(action);
			}
		}
	}
	if(index >= 0 && index < m_usage.size()) {
		m_usage[index] = ++m_usageCounter;
	}
	saveSlots();
}

QSize ToolRail::sizeHint() const
{
	int thickness = dp(56);
	return m_orientation == Qt::Horizontal ? QSize(dp(360), thickness)
										   : QSize(thickness, dp(360));
}

void ToolRail::rebuild()
{
	while(m_layout->count() > 0) {
		QLayoutItem *item = m_layout->takeAt(0);
		delete item;
	}
	for(ChromeButton *button : m_slotButtons) {
		button->deleteLater();
	}
	m_slotButtons.clear();

	bool horizontal = m_orientation == Qt::Horizontal;
	QBoxLayout::Direction direction;
	if(horizontal) {
		direction = m_leftHanded ? QBoxLayout::RightToLeft
								 : QBoxLayout::LeftToRight;
	} else {
		direction = QBoxLayout::TopToBottom;
	}
	m_layout->setDirection(direction);

	for(int i = 0; i < m_slots.size(); ++i) {
		QAction *action = actionByName(m_slots[i]);
		if(!action) {
			continue;
		}
		ChromeButton *button = new ChromeButton;
		button->setThemeIcon(action->icon());
		button->bindAction(action);
		// Tapping the tool that's already active opens its settings, just
		// like in most mobile painting apps. The check happens on press,
		// before the click activates the tool.
		connect(button, &ChromeButton::pressed, this, [this, button] {
			button->setProperty("wasActive", button->isChecked());
		});
		connect(button, &ChromeButton::clicked, this, [this, button] {
			if(button->property("wasActive").toBool()) {
				emit toolSettingsRequested();
			}
		});
		connect(
			button, &ChromeButton::longPressed, this,
			[this, action] {
				action->trigger();
				emit toolSettingsRequested();
			});
		m_slotButtons.append(button);
		m_layout->addWidget(button);
	}
	m_layout->addWidget(m_drawerButton);
	m_layout->addStretch(1);
	if(horizontal) {
		m_separator->setFixedSize(1, dp(28));
	} else {
		m_separator->setFixedSize(dp(28), 1);
	}
	m_separator->setVisible(m_showPanelButtons);
	m_layersButton->setVisible(m_showPanelButtons);
	m_animationButton->setVisible(m_showPanelButtons);
	m_colorButton->setVisible(m_showPanelButtons);
	if(m_showPanelButtons) {
		m_layout->addWidget(m_separator, 0, Qt::AlignCenter);
		m_layout->addWidget(m_layersButton);
		m_layout->addWidget(m_animationButton);
		m_layout->addWidget(m_colorButton);
	}
	updateGeometry();
}

void ToolRail::loadSlots()
{
	QSettings settings;
	QStringList saved =
		settings.value(QStringLiteral("mobileui/toolslots")).toStringList();
	QStringList defaults = {
		QStringLiteral("toolbrush"),		 QStringLiteral("tooleraser"),
		QStringLiteral("toolselectpolygon"), QStringLiteral("tooltransform"),
		QStringLiteral("toolfill"),			 QStringLiteral("toolpicker"),
		QStringLiteral("toolline"),			 QStringLiteral("toolgradient"),
		QStringLiteral("toolselectrect"),	 QStringLiteral("toolpan"),
	};
	QStringList result;
	for(const QString &name : saved + defaults) {
		if(result.size() >= m_slotCount) {
			break;
		}
		if(!result.contains(name) && actionByName(name)) {
			result.append(name);
		}
	}
	m_slots = result;
	m_usage.fill(0, m_slots.size());
	// Earlier slots count as more recently used initially.
	for(int i = 0; i < m_usage.size(); ++i) {
		m_usage[i] = m_usage.size() - i;
	}
	m_usageCounter = m_usage.size();
}

void ToolRail::saveSlots() const
{
	QSettings settings;
	QStringList saved =
		settings.value(QStringLiteral("mobileui/toolslots")).toStringList();
	// Keep entries beyond the current slot count, so that rotating to a
	// layout with fewer slots and back doesn't lose them.
	QStringList merged = m_slots;
	for(const QString &name : saved) {
		if(!merged.contains(name)) {
			merged.append(name);
		}
	}
	settings.setValue(QStringLiteral("mobileui/toolslots"), merged.mid(0, 10));
}

QAction *ToolRail::actionByName(const QString &name) const
{
	for(QAction *action : m_toolActions) {
		if(action->objectName() == name) {
			return action;
		}
	}
	return nullptr;
}

QuickSlider::QuickSlider(Kind kind, QWidget *parent)
	: QWidget(parent)
	, m_kind(kind)
{
	setProperty("mobileChrome", true);
	setAccessibleName(kind == Kind::Size ? tr("Brush size") : tr("Opacity"));
	setToolTip(accessibleName());
}

void QuickSlider::setTarget(KisSliderSpinBox *target, qreal exponent)
{
	if(m_target) {
		disconnect(m_target, nullptr, this, nullptr);
	}
	m_target = target;
	m_exponent = exponent;
	if(target) {
		connect(
			target, QOverload<int>::of(&QSpinBox::valueChanged), this,
			QOverload<>::of(&QWidget::update));
	}
	setEnabled(target != nullptr);
	update();
}

void QuickSlider::setLength(int length)
{
	m_length = length;
	updateGeometry();
}

QSize QuickSlider::sizeHint() const
{
	return QSize(dp(44), m_length);
}

void QuickSlider::paintEvent(QPaintEvent *)
{
	const Theme &t = Theme::current();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	qreal w = dp(m_dragging ? 36 : 30);
	QRectF track((width() - w) / 2.0, dp(2), w, height() - dp(4));
	qreal radius = w / 2.0;
	QColor bg = t.surface;
	bg.setAlphaF(0.92);
	painter.setPen(QPen(t.outline, 1.0));
	painter.setBrush(bg);
	painter.drawRoundedRect(track, radius, radius);

	if(m_target && isEnabled()) {
		qreal f = fraction();
		qreal fillHeight = qMax(w, track.height() * f);
		QRectF fill(
			track.left(), track.bottom() - fillHeight, track.width(),
			fillHeight);
		QColor accent = t.accent;
		accent.setAlphaF(m_dragging ? 0.95 : 0.8);
		painter.setPen(Qt::NoPen);
		painter.setBrush(accent);
		painter.drawRoundedRect(fill, radius, radius);
	}

	// Small glyph at the top hinting what the slider does.
	QColor glyph = isEnabled() ? t.textDim : t.outline;
	painter.setPen(Qt::NoPen);
	painter.setBrush(glyph);
	QPointF c(width() / 2.0, track.top() + radius);
	if(m_kind == Kind::Size) {
		painter.drawEllipse(c, dp(5), dp(5));
	} else {
		QColor half = glyph;
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(glyph, 1.5));
		painter.drawEllipse(c, dp(5), dp(5));
		half.setAlphaF(0.5);
		painter.setPen(Qt::NoPen);
		painter.setBrush(half);
		painter.drawEllipse(c, dp(3), dp(3));
	}
}

void QuickSlider::mousePressEvent(QMouseEvent *event)
{
	if(event->button() == Qt::LeftButton && m_target && isEnabled()) {
		m_dragging = true;
		m_dragStartY = eventPos(event).y();
		m_dragStartFraction = fraction();
		emit dragStateChanged(true, valueLabel());
		update();
	}
	event->accept();
}

void QuickSlider::mouseMoveEvent(QMouseEvent *event)
{
	if(m_dragging) {
		int dy = eventPos(event).y() - m_dragStartY;
		qreal len = qMax(1, height() - dp(8));
		setFraction(m_dragStartFraction - qreal(dy) / len);
		emit dragStateChanged(true, valueLabel());
	}
	event->accept();
}

void QuickSlider::mouseReleaseEvent(QMouseEvent *event)
{
	if(m_dragging) {
		m_dragging = false;
		emit dragStateChanged(false, valueLabel());
		update();
	}
	event->accept();
}

qreal QuickSlider::fraction() const
{
	if(!m_target) {
		return 0.0;
	}
	int min = m_target->minimum();
	int max = m_target->maximum();
	if(max <= min) {
		return 0.0;
	}
	qreal linear = qreal(m_target->value() - min) / qreal(max - min);
	return qPow(qBound(0.0, linear, 1.0), 1.0 / m_exponent);
}

void QuickSlider::setFraction(qreal f)
{
	if(!m_target) {
		return;
	}
	f = qBound(0.0, f, 1.0);
	int min = m_target->minimum();
	int max = m_target->maximum();
	int value = min + qRound(qPow(f, m_exponent) * (max - min));
	if(value != m_target->value()) {
		m_target->setValue(value);
	}
	update();
}

QString QuickSlider::valueLabel() const
{
	if(!m_target) {
		return QString();
	}
	QString value = QStringLiteral("%1%2%3").arg(
		m_target->prefix(), QString::number(m_target->value()),
		m_target->suffix());
	return m_kind == Kind::Size ? tr("Size %1").arg(value)
								: tr("Opacity %1").arg(value);
}

QuickSliders::QuickSliders(QAction *picker, QWidget *parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("mobileQuickSliders"));
	setProperty("mobileChrome", true);
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(dp(6));

	m_size = new QuickSlider(QuickSlider::Kind::Size);
	connect(
		m_size, &QuickSlider::dragStateChanged, this,
		&QuickSliders::dragStateChanged);
	layout->addWidget(m_size, 0, Qt::AlignHCenter);

	m_picker = new ChromeButton(QStringLiteral("eyedropper"), tr("Eyedropper"));
	m_picker->setButtonSize(44);
	if(picker) {
		m_picker->bindAction(picker);
	}
	layout->addWidget(m_picker, 0, Qt::AlignHCenter);

	m_opacity = new QuickSlider(QuickSlider::Kind::Opacity);
	connect(
		m_opacity, &QuickSlider::dragStateChanged, this,
		&QuickSliders::dragStateChanged);
	layout->addWidget(m_opacity, 0, Qt::AlignHCenter);
}

void QuickSliders::setTargets(
	KisSliderSpinBox *size, qreal sizeExponent, KisSliderSpinBox *opacity)
{
	m_size->setTarget(size, sizeExponent);
	m_opacity->setTarget(opacity, 1.0);
}

void QuickSliders::fitHeight(int height)
{
	int pickerHeight = dp(44) + dp(12);
	int length = qBound(dp(96), (height - pickerHeight) / 2, dp(200));
	m_size->setLength(length);
	m_opacity->setLength(length);
	adjustSize();
}

bool QuickSliders::hasTargets() const
{
	return m_size->target() || m_opacity->target();
}

ValueBubble::ValueBubble(QWidget *parent)
	: QWidget(parent)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	applyFont(this, TextRole::Body, true);
	hide();
}

void ValueBubble::setText(const QString &text)
{
	m_text = text;
	adjustSize();
	update();
}

QSize ValueBubble::sizeHint() const
{
	QFontMetrics fm(font());
	return QSize(fm.horizontalAdvance(m_text) + dp(28), dp(40));
}

void ValueBubble::paintEvent(QPaintEvent *)
{
	const Theme &t = Theme::current();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(QPen(t.outline, 1.0));
	QColor bg = t.surface;
	bg.setAlphaF(0.95);
	painter.setBrush(bg);
	painter.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
	painter.setPen(t.text);
	painter.drawText(r, Qt::AlignCenter, m_text);
}

}
