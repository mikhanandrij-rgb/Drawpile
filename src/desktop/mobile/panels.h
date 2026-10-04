// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): panels shown inside the sheet that aren't upstream
// docks: the tool drawer, the "more" panel and the session status panel.
#ifndef DESKTOP_MOBILE_PANELS_H
#define DESKTOP_MOBILE_PANELS_H
#include <QPointer>
#include <QVector>
#include <QWidget>

class QAction;
class QGridLayout;
class QLabel;
class QVBoxLayout;

namespace mobile {

class ActionCard;
class ChromeButton;

// Grid with every drawing tool, labelled.
class ToolDrawer final : public QWidget {
	Q_OBJECT
public:
	explicit ToolDrawer(
		const QVector<QAction *> &toolActions, QWidget *parent = nullptr);

signals:
	void toolChosen(QAction *action);
	void toolSettingsRequested();

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void reflow();

	QGridLayout *m_grid;
	QVector<ChromeButton *> m_buttons;
	int m_columns = 0;
};

// A titled grid of cards, used to build the "more" panel.
class CardSection final : public QWidget {
	Q_OBJECT
public:
	explicit CardSection(const QString &title, QWidget *parent = nullptr);
	ActionCard *addCard(const QString &iconName, const QString &title);
	// Card that mirrors and triggers an action; checkable actions are shown
	// with a highlighted state.
	ActionCard *addActionCard(QAction *action, const QString &iconName);

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void reflow();

	QGridLayout *m_grid;
	QVector<ActionCard *> m_cards;
	int m_columns = 0;
};

class MorePanel final : public QWidget {
	Q_OBJECT
public:
	explicit MorePanel(QWidget *parent = nullptr);
	CardSection *addSection(const QString &title);

private:
	QVBoxLayout *m_layout;
};

// Hosts upstream's status bar (connection, recording, chat notification)
// while the mobile interface is active.
class StatusPanel final : public QWidget {
	Q_OBJECT
public:
	explicit StatusPanel(QWidget *parent = nullptr);
	void setStatusWidget(QWidget *widget);
	QWidget *takeStatusWidget();
	void setSessionText(const QString &text);
	CardSection *actions() const { return m_actions; }

private:
	QLabel *m_session;
	QVBoxLayout *m_hostLayout;
	QPointer<QWidget> m_statusWidget;
	CardSection *m_actions;
};

}

#endif
