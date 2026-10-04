// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): project hub shown at startup and via the projects
// button, replacing the start dialog's landing page in the mobile interface.
#ifndef DESKTOP_MOBILE_HUB_H
#define DESKTOP_MOBILE_HUB_H
#include <QColor>
#include <QPointer>
#include <QVector>
#include <QWidget>

class MainWindow;
class QButtonGroup;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;

namespace mobile {

class ActionCard;
class RecentCard;

class Hub final : public QWidget {
	Q_OBJECT
public:
	Hub(MainWindow *mw, QWidget *parent);

	// Shows the page matching a start dialog entry (create or recent), or
	// the main page for anything else.
	void showForStartPage(int startDialogPage);
	// Android back button. Returns true if the hub handled it.
	bool handleBack(bool release);

signals:
	void closeRequested();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

private:
	struct Preset {
		QString iconName;
		QString title;
		QSize size;
		bool animation;
	};

	QWidget *buildMainPage();
	QWidget *buildNewPage();
	void refreshRecents();
	void loadNextThumbnail();
	void refreshContinueCard();
	void refreshRecoveryBadge();
	void reflowGrids();
	void showRecentActions(const QString &path, long long id);
	void showChoices(
		const QString &title,
		const QVector<QPair<QString, QString>> &choices,
		const std::function<void(int)> &onChosen);
	void openRecent(const QString &path);
	void renameRecent(const QString &path, long long id);
	void duplicateRecent(const QString &path);
	void deleteRecent(const QString &path, long long id);
	void saveCopyOf(const QString &path);
	void createCanvas();
	void selectPreset(int index);
	void updateNewPageSummary();
	void expectDocumentLoad();
	void showMainPage();
	void showNewPage();
	void startDialogPage(int page);
	QSize phoneSize(bool portrait) const;

	QPointer<MainWindow> m_mw;
	QStackedWidget *m_pages;
	QWidget *m_mainPage;
	QWidget *m_newPage;
	QScrollArea *m_mainScroll;
	QWidget *m_continueCard;
	QLabel *m_continueThumb;
	QLabel *m_continueTitle;
	QLabel *m_continueMeta;
	QGridLayout *m_actionGrid;
	QVector<ActionCard *> m_actionCards;
	ActionCard *m_recoverCard;
	QGridLayout *m_recentGrid;
	QVector<RecentCard *> m_recentCards;
	QLabel *m_recentEmpty;
	int m_thumbnailCursor = 0;
	int m_columns = 0;

	QVector<Preset> m_presets;
	QVector<ActionCard *> m_presetCards;
	QGridLayout *m_presetGrid;
	int m_presetColumns = 0;
	QWidget *m_fpsRow;
	QButtonGroup *m_fpsGroup;
	QSpinBox *m_widthBox;
	QSpinBox *m_heightBox;
	QButtonGroup *m_backgroundGroup;
	QColor m_customBackground;
	QLabel *m_summary;
	int m_selectedPreset = -1;
	bool m_expectLoad = false;
};

}

#endif
