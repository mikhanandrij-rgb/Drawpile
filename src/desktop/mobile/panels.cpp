// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/mobile/panels.h"
#include "desktop/mobile/theme.h"
#include "desktop/mobile/widgets.h"
#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace mobile {

namespace {
QString stripMnemonic(QString text)
{
	text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
	text.remove(QLatin1Char('&'));
	text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
	return text;
}

QLabel *makeSectionTitle(const QString &title)
{
	QLabel *label = new QLabel(title.toUpper());
	label->setProperty("mobileRole", QStringLiteral("dim"));
	applyFont(label, TextRole::Caption, true);
	label->setContentsMargins(dp(4), dp(12), dp(4), dp(4));
	return label;
}
}

ToolDrawer::ToolDrawer(const QVector<QAction *> &toolActions, QWidget *parent)
	: QWidget(parent)
{
	setProperty("mobileChrome", true);
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(dp(12), 0, dp(12), dp(12));
	layout->setSpacing(dp(8));

	QWidget *gridWidget = new QWidget;
	m_grid = new QGridLayout(gridWidget);
	m_grid->setContentsMargins(0, 0, 0, 0);
	m_grid->setHorizontalSpacing(dp(4));
	m_grid->setVerticalSpacing(dp(8));
	for(QAction *action : toolActions) {
		ChromeButton *button = new ChromeButton;
		button->setShowLabel(true);
		button->setButtonSize(52);
		button->setThemeIcon(action->icon());
		button->bindAction(action);
		button->setText(stripMnemonic(action->text()));
		connect(button, &ChromeButton::clicked, this, [this, action] {
			emit toolChosen(action);
		});
		connect(button, &ChromeButton::longPressed, this, [this, action] {
			action->trigger();
			emit toolSettingsRequested();
		});
		m_buttons.append(button);
	}
	layout->addWidget(gridWidget);

	QLabel *hint = new QLabel(
		tr("Tap a tool to use it. Tap the active tool in the bar again, or "
		   "long-press any tool, to open its settings."));
	hint->setWordWrap(true);
	hint->setProperty("mobileRole", QStringLiteral("dim"));
	applyFont(hint, TextRole::Caption);
	layout->addWidget(hint);
	layout->addStretch(1);
	reflow();
}

void ToolDrawer::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	reflow();
}

void ToolDrawer::reflow()
{
	int columns = qBound(3, (width() - dp(24)) / dp(80), 8);
	if(columns == m_columns) {
		return;
	}
	m_columns = columns;
	for(ChromeButton *button : m_buttons) {
		m_grid->removeWidget(button);
	}
	for(int i = 0; i < m_buttons.size(); ++i) {
		m_grid->addWidget(m_buttons[i], i / columns, i % columns);
	}
}

CardSection::CardSection(const QString &title, QWidget *parent)
	: QWidget(parent)
{
	setProperty("mobileChrome", true);
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(dp(4));
	if(!title.isEmpty()) {
		layout->addWidget(makeSectionTitle(title));
	}
	QWidget *gridWidget = new QWidget;
	m_grid = new QGridLayout(gridWidget);
	m_grid->setContentsMargins(0, 0, 0, 0);
	m_grid->setSpacing(dp(8));
	layout->addWidget(gridWidget);
}

ActionCard *CardSection::addCard(const QString &iconName, const QString &title)
{
	ActionCard *card = new ActionCard(iconName, title);
	card->setCompact(true);
	m_cards.append(card);
	m_columns = 0;
	reflow();
	return card;
}

ActionCard *CardSection::addActionCard(QAction *action, const QString &iconName)
{
	ActionCard *card = addCard(iconName, stripMnemonic(action->text()));
	if(iconName.isEmpty()) {
		card->setThemeIcon(action->icon());
	}
	auto sync = [card, action] {
		card->setEnabled(action->isEnabled());
		card->setCheckable(action->isCheckable());
		if(action->isCheckable()) {
			card->setChecked(action->isChecked());
		}
		card->setVisible(action->isVisible());
		card->update();
	};
	sync();
	connect(action, &QAction::changed, card, sync);
	connect(action, &QAction::toggled, card, sync);
	connect(card, &ActionCard::clicked, action, [action, sync] {
		action->trigger();
		sync();
	});
	return card;
}

void CardSection::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	reflow();
}

void CardSection::reflow()
{
	int columns = qBound(3, width() / dp(96), 6);
	if(columns == m_columns) {
		return;
	}
	m_columns = columns;
	for(ActionCard *card : m_cards) {
		m_grid->removeWidget(card);
	}
	for(int i = 0; i < m_cards.size(); ++i) {
		m_grid->addWidget(m_cards[i], i / columns, i % columns);
	}
	for(int c = 0; c < columns; ++c) {
		m_grid->setColumnStretch(c, 1);
	}
}

MorePanel::MorePanel(QWidget *parent)
	: QWidget(parent)
{
	setProperty("mobileChrome", true);
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(dp(16), 0, dp(16), dp(16));
	m_layout->setSpacing(dp(8));
	m_layout->addStretch(1);
}

CardSection *MorePanel::addSection(const QString &title)
{
	CardSection *section = new CardSection(title);
	m_layout->insertWidget(m_layout->count() - 1, section);
	return section;
}

StatusPanel::StatusPanel(QWidget *parent)
	: QWidget(parent)
{
	setProperty("mobileChrome", true);
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(dp(16), 0, dp(16), dp(16));
	layout->setSpacing(dp(8));

	m_session = new QLabel;
	m_session->setWordWrap(true);
	applyFont(m_session, TextRole::Body);
	layout->addWidget(m_session);

	QWidget *host = new QWidget;
	host->setObjectName(QStringLiteral("mobileStatusHost"));
	m_hostLayout = new QVBoxLayout(host);
	m_hostLayout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(host);

	m_actions = new CardSection(tr("Session"));
	layout->addWidget(m_actions);
	layout->addStretch(1);
}

void StatusPanel::setStatusWidget(QWidget *widget)
{
	m_statusWidget = widget;
	if(widget) {
		m_hostLayout->addWidget(widget);
		widget->show();
	}
}

QWidget *StatusPanel::takeStatusWidget()
{
	QWidget *widget = m_statusWidget;
	if(widget) {
		m_hostLayout->removeWidget(widget);
	}
	m_statusWidget.clear();
	return widget;
}

void StatusPanel::setSessionText(const QString &text)
{
	m_session->setText(text);
}

}
