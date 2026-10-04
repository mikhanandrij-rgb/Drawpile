// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpmsg/message.h>
}
#include "desktop/mobile/hub.h"
#include "cmake-config/config.h"
#include "desktop/dialogs/colordialog.h"
#include "desktop/dialogs/startdialog.h"
#include "desktop/main.h"
#include "desktop/mainwindow.h"
#include "desktop/mobile/theme.h"
#include "desktop/mobile/thumbnails.h"
#include "desktop/mobile/widgets.h"
#include "desktop/utils/recents.h"
#include "desktop/utils/widgetutils.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/config/config.h"
#include "libclient/document.h"
#include "libclient/io/pathinfo.h"
#include "libclient/net/client.h"
#include "libclient/net/message.h"
#include "libclient/project/recoverymodel.h"
#include "libshared/util/paths.h"
#include <QAction>
#include <QButtonGroup>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtColorWidgets/ColorDialog>
#include <functional>

namespace mobile {

namespace {

QLabel *sectionLabel(const QString &text)
{
	QLabel *label = new QLabel(text);
	label->setProperty("mobileRole", QStringLiteral("title"));
	applyFont(label, TextRole::Subtitle, true);
	label->setContentsMargins(dp(4), dp(16), dp(4), dp(6));
	return label;
}

QScrollArea *makeScroll(QWidget *content)
{
	QScrollArea *scroll = new QScrollArea;
	scroll->setProperty("mobileChrome", true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setWidget(content);
	utils::bindKineticScrollingWith(
		scroll, Qt::ScrollBarAlwaysOff, Qt::ScrollBarAsNeeded);
	return scroll;
}

bool isLocalFile(const QString &path)
{
	return !path.startsWith(QStringLiteral("content:")) &&
		   QFileInfo(path).isFile();
}

void drawChecker(QPainter &painter, const QRectF &rect, const Theme &t)
{
	painter.save();
	painter.setClipRect(rect);
	QColor a = t.dark ? QColor(0x3a, 0x3c, 0x42) : QColor(0xe8, 0xe9, 0xec);
	QColor b = t.dark ? QColor(0x2e, 0x30, 0x35) : QColor(0xf6, 0xf6, 0xf8);
	painter.fillRect(rect, a);
	int s = dp(8);
	for(int y = int(rect.top()); y < rect.bottom(); y += s) {
		for(int x = int(rect.left()); x < rect.right(); x += s) {
			if(((x - int(rect.left())) / s + (y - int(rect.top())) / s) % 2) {
				painter.fillRect(QRect(x, y, s, s), b);
			}
		}
	}
	painter.restore();
}

}

// Card in the recent projects grid.
class RecentCard final : public QAbstractButton {
public:
	RecentCard(const QString &path, long long id, QWidget *parent = nullptr)
		: QAbstractButton(parent)
		, m_path(path)
		, m_id(id)
	{
		setFocusPolicy(Qt::NoFocus);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		io::PathInfo info(path);
		m_name = info.basenameWithoutExtension();
		m_extension = info.extension().toUpper();
		setText(m_name);
		setAccessibleName(m_name);
		m_longPress.setSingleShot(true);
		m_longPress.setInterval(450);
		QObject::connect(&m_longPress, &QTimer::timeout, this, [this] {
			if(isDown()) {
				m_longPressed = true;
				setDown(false);
				if(onMenu) {
					onMenu();
				}
			}
		});
	}

	const QString &path() const { return m_path; }
	long long id() const { return m_id; }
	bool thumbnailLoaded() const { return m_loaded; }

	void setThumbnail(const QImage &img, const ProjectMeta &meta)
	{
		m_loaded = true;
		m_thumb = img;
		QStringList parts;
		if(meta.valid && meta.size.isValid()) {
			parts.append(QStringLiteral("%1×%2")
							 .arg(meta.size.width())
							 .arg(meta.size.height()));
		}
		QFileInfo info(m_path);
		QDateTime modified;
		if(isLocalFile(m_path)) {
			modified = info.lastModified();
			qint64 bytes = info.size();
			parts.append(QLocale().formattedDataSize(
				bytes, 1, QLocale::DataSizeTraditionalFormat));
		} else if(meta.valid) {
			modified = meta.updated;
		}
		if(modified.isValid()) {
			parts.append(QLocale().toString(modified.date(), QLocale::ShortFormat));
		}
		m_meta = parts.join(QStringLiteral(" · "));
		if(meta.animated) {
			m_badge = meta.framerate > 0.0
						  ? Hub::tr("▶ %1 fps").arg(QLocale().toString(
								meta.framerate, 'g', 3))
						  : Hub::tr("▶ Animation");
		}
		update();
	}

	std::function<void()> onMenu;

	QSize sizeHint() const override
	{
		int w = qMax(dp(150), width());
		return QSize(dp(160), thumbHeight(w) + dp(60));
	}

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return thumbHeight(w) + dp(60); }

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		m_longPressed = false;
		QAbstractButton::mousePressEvent(event);
		if(isDown()) {
			m_longPress.start();
		}
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		m_longPress.stop();
		if(m_longPressed) {
			m_longPressed = false;
			setDown(false);
			return;
		}
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		QPoint pos = event->position().toPoint();
#else
		QPoint pos = event->pos();
#endif
		if(isDown() && menuRect().contains(pos)) {
			setDown(false);
			if(onMenu) {
				onMenu();
			}
			return;
		}
		QAbstractButton::mouseReleaseEvent(event);
	}

	void paintEvent(QPaintEvent *) override
	{
		const Theme &t = Theme::current();
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		qreal radius = radiusMedium();
		QPainterPath clip;
		clip.addRoundedRect(r, radius, radius);
		painter.fillPath(clip, isDown() ? t.surface3 : t.surface2);

		QRectF thumbRect(r.left(), r.top(), r.width(), thumbHeight(width()));
		painter.save();
		painter.setClipPath(clip);
		drawChecker(painter, thumbRect, t);
		if(!m_thumb.isNull()) {
			QSizeF s = QSizeF(m_thumb.size())
						   .scaled(thumbRect.size(), Qt::KeepAspectRatio);
			QRectF target(
				thumbRect.center().x() - s.width() / 2.0,
				thumbRect.center().y() - s.height() / 2.0, s.width(),
				s.height());
			painter.drawImage(target, m_thumb);
		} else {
			// Placeholder: file type in the middle.
			QFont f = font();
			f.setPixelSize(dp(18));
			f.setBold(true);
			painter.setFont(f);
			painter.setPen(t.textDim);
			painter.drawText(
				thumbRect, Qt::AlignCenter,
				m_extension.isEmpty() ? QStringLiteral("?") : m_extension);
		}
		painter.restore();

		// Menu affordance.
		QRectF m = menuRect();
		QColor bubble = t.surface;
		bubble.setAlphaF(0.85);
		painter.setPen(Qt::NoPen);
		painter.setBrush(bubble);
		painter.drawEllipse(m.adjusted(dp(6), dp(6), -dp(6), -dp(6)));
		mobile::icon(QStringLiteral("more"), t.text)
			.paint(
				&painter, m.adjusted(dp(12), dp(12), -dp(12), -dp(12)).toRect());

		if(!m_badge.isEmpty()) {
			QFont f = font();
			f.setPixelSize(fontPixelSize(TextRole::Caption));
			f.setBold(true);
			painter.setFont(f);
			QFontMetrics fm(f);
			int h = dp(22);
			int w = fm.horizontalAdvance(m_badge) + dp(14);
			QRectF badge(thumbRect.left() + dp(8), thumbRect.top() + dp(8), w, h);
			painter.setPen(Qt::NoPen);
			painter.setBrush(t.accent);
			painter.drawRoundedRect(badge, h / 2.0, h / 2.0);
			painter.setPen(t.accentText);
			painter.drawText(badge, Qt::AlignCenter, m_badge);
		}

		QFont titleFont = font();
		titleFont.setPixelSize(fontPixelSize(TextRole::Label));
		titleFont.setBold(true);
		QFontMetrics tfm(titleFont);
		QFont metaFont = font();
		metaFont.setPixelSize(fontPixelSize(TextRole::Caption));
		QFontMetrics mfm(metaFont);
		qreal x = r.left() + dp(12);
		qreal w = r.width() - dp(24);
		qreal y = thumbRect.bottom() + dp(10);
		painter.setFont(titleFont);
		painter.setPen(t.text);
		painter.drawText(
			QRectF(x, y, w, tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
			tfm.elidedText(m_name, Qt::ElideMiddle, int(w)));
		painter.setFont(metaFont);
		painter.setPen(t.textDim);
		painter.drawText(
			QRectF(x, y + tfm.height() + dp(2), w, mfm.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			mfm.elidedText(
				m_meta.isEmpty() ? m_extension : m_meta, Qt::ElideRight,
				int(w)));
	}

private:
	int thumbHeight(int w) const { return qRound(w * 0.72); }
	QRectF menuRect() const
	{
		int s = dp(48);
		return QRectF(width() - s, 0, s, s);
	}

	QString m_path;
	long long m_id;
	QString m_name;
	QString m_extension;
	QString m_meta;
	QString m_badge;
	QImage m_thumb;
	bool m_loaded = false;
	bool m_longPressed = false;
	QTimer m_longPress;
};

// Simple modal list of choices sliding up from the bottom of the hub.
class ChoiceOverlay final : public QWidget {
public:
	ChoiceOverlay(
		QWidget *parent, const QString &title,
		const QVector<QPair<QString, QString>> &choices,
		const std::function<void(int)> &onChosen)
		: QWidget(parent)
		, m_onChosen(onChosen)
	{
		setGeometry(parent->rect());
		QVBoxLayout *outer = new QVBoxLayout(this);
		outer->setContentsMargins(0, 0, 0, 0);
		outer->addStretch(1);
		m_panel = new QWidget;
		m_panel->setObjectName(QStringLiteral("mobileChoicePanel"));
		m_panel->setAttribute(Qt::WA_StyledBackground, true);
		const Theme &t = Theme::current();
		m_panel->setStyleSheet(
			QStringLiteral("QWidget#mobileChoicePanel { background: %1; "
						   "border-top-left-radius: %2px; "
						   "border-top-right-radius: %2px; }")
				.arg(t.surface.name())
				.arg(radiusLarge()));
		m_panel->setMaximumWidth(dp(560));
		QVBoxLayout *layout = new QVBoxLayout(m_panel);
		layout->setContentsMargins(dp(16), dp(16), dp(16), dp(20));
		layout->setSpacing(dp(8));
		QLabel *label = new QLabel(title);
		label->setProperty("mobileRole", QStringLiteral("title"));
		label->setWordWrap(true);
		applyFont(label, TextRole::Subtitle, true);
		layout->addWidget(label);
		for(int i = 0; i < choices.size(); ++i) {
			ActionCard *card =
				new ActionCard(choices[i].first, choices[i].second);
			QObject::connect(card, &ActionCard::clicked, this, [this, i] {
				choose(i);
			});
			layout->addWidget(card);
		}
		QHBoxLayout *center = new QHBoxLayout;
		center->addWidget(m_panel);
		outer->addLayout(center);
		show();
		raise();
	}

	void choose(int i)
	{
		std::function<void(int)> fn = m_onChosen;
		hide();
		deleteLater();
		if(fn && i >= 0) {
			fn(i);
		}
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor(0, 0, 0, 120));
	}

	void mouseReleaseEvent(QMouseEvent *) override { choose(-1); }

private:
	QWidget *m_panel;
	std::function<void(int)> m_onChosen;
};

Hub::Hub(MainWindow *mw, QWidget *parent)
	: QWidget(parent)
	, m_mw(mw)
{
	setObjectName(QStringLiteral("mobileHub"));
	setProperty("mobileChrome", true);
	setAttribute(Qt::WA_StyledBackground, true);
	hide();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_pages = new QStackedWidget;
	layout->addWidget(m_pages);
	m_mainPage = buildMainPage();
	m_newPage = buildNewPage();
	m_pages->addWidget(m_mainPage);
	m_pages->addWidget(m_newPage);

	connect(
		&dpApp().recents(), &utils::Recents::recentFilesChanged, this, [this] {
			if(isVisible()) {
				refreshRecents();
			}
		});

	if(Document *doc = mw->findChild<Document *>()) {
		auto loaded = [this] {
			if(m_expectLoad && isVisible()) {
				m_expectLoad = false;
				emit closeRequested();
			}
		};
		connect(doc, &Document::canvasChanged, this, loaded);
		connect(doc, &Document::canvasChanged, this, [this] {
			if(isVisible()) {
				QTimer::singleShot(250, this, &Hub::refreshContinueCard);
			}
		});
		connect(doc, &Document::serverConnected, this, loaded);
		// Remember what saved files look like for the recent grid.
		connect(
			doc, &Document::currentPathChanged, this,
			[doc](const QString &path) {
				if(!path.isEmpty() && doc->canvas()) {
					thumbnails::storeFromCanvas(path, doc->canvas());
				}
			});
		connect(doc, &Document::dirtyCanvas, this, [doc](bool dirty) {
			// Becoming clean means the document was just saved.
			if(!dirty && doc->haveCurrentPath() && doc->canvas()) {
				thumbnails::storeFromCanvas(doc->currentPath(), doc->canvas());
			}
		});
	}
}

void Hub::showForStartPage(int startDialogPage)
{
	if(startDialogPage == int(dialogs::StartDialog::Create)) {
		showNewPage();
	} else {
		showMainPage();
	}
}

bool Hub::handleBack(bool release)
{
	if(m_pages->currentWidget() == m_newPage) {
		if(release) {
			showMainPage();
		}
		return true;
	}
	for(QObject *child : children()) {
		if(ChoiceOverlay *overlay = dynamic_cast<ChoiceOverlay *>(child)) {
			if(overlay->isVisible()) {
				if(release) {
					overlay->choose(-1);
				}
				return true;
			}
		}
	}
	// Going back from the hub returns to the canvas if there is one.
	Document *doc = m_mw ? m_mw->findChild<Document *>() : nullptr;
	if(doc && doc->canvas()) {
		if(release) {
			emit closeRequested();
		}
		return true;
	}
	return false;
}

void Hub::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	reflowGrids();
	for(QObject *child : children()) {
		if(QWidget *w = qobject_cast<QWidget *>(child)) {
			if(dynamic_cast<ChoiceOverlay *>(w)) {
				w->setGeometry(rect());
			}
		}
	}
}

void Hub::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refreshContinueCard();
	// At startup the canvas may still be loading.
	QTimer::singleShot(500, this, &Hub::refreshContinueCard);
	QTimer::singleShot(1500, this, &Hub::refreshContinueCard);
	refreshRecents();
	refreshRecoveryBadge();
	reflowGrids();
}

void Hub::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.fillRect(rect(), Theme::current().background);
}

QWidget *Hub::buildMainPage()
{
	const Theme &t = Theme::current();
	QWidget *content = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(content);
	layout->setContentsMargins(dp(16), dp(8), dp(16), dp(24));
	layout->setSpacing(dp(8));

	// Header.
	QHBoxLayout *header = new QHBoxLayout;
	header->setSpacing(dp(4));
	QVBoxLayout *titles = new QVBoxLayout;
	titles->setSpacing(0);
	QLabel *title = new QLabel(QStringLiteral("Drawpile"));
	title->setProperty("mobileRole", QStringLiteral("title"));
	QFont titleFont = title->font();
	titleFont.setPixelSize(dp(26));
	titleFont.setBold(true);
	title->setFont(titleFont);
	titles->addWidget(title);
	QLabel *subtitle = new QLabel(
		tr("Mobile interface (unofficial fork) · %1")
			.arg(QString::fromUtf8(cmake_config::version())));
	subtitle->setProperty("mobileRole", QStringLiteral("dim"));
	subtitle->setWordWrap(true);
	subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	applyFont(subtitle, TextRole::Caption);
	titles->addWidget(subtitle);
	header->addLayout(titles, 1);
	ChromeButton *settings =
		new ChromeButton(QStringLiteral("settings"), tr("Preferences"));
	connect(settings, &ChromeButton::clicked, this, [this] {
		if(QAction *a = m_mw->findChild<QAction *>(QStringLiteral("preferences"))) {
			a->trigger();
		}
	});
	header->addWidget(settings);
	ChromeButton *more = new ChromeButton(QStringLiteral("more"), tr("More"));
	connect(more, &ChromeButton::clicked, this, [this] {
		showChoices(
			tr("More"),
			{{QStringLiteral("info"), tr("Welcome, news and links")},
			 {QStringLiteral("open"), tr("Open recording or player…")},
			 {QStringLiteral("info"), tr("About Drawpile")},
			 {QStringLiteral("back"), tr("Quit")}},
			[this](int i) {
				switch(i) {
				case 0:
					startDialogPage(int(dialogs::StartDialog::Welcome));
					break;
				case 1:
					if(QAction *a = m_mw->findChild<QAction *>(
						   QStringLiteral("openplayback"))) {
						expectDocumentLoad();
						a->trigger();
					}
					break;
				case 2:
					MainWindow::about();
					break;
				case 3:
					if(QAction *a = m_mw->findChild<QAction *>(
						   QStringLiteral("exitprogram"))) {
						a->trigger();
					}
					break;
				default:
					break;
				}
			});
	});
	header->addWidget(more);
	layout->addLayout(header);

	// Continue where you left off.
	m_continueCard = new QWidget;
	m_continueCard->setObjectName(QStringLiteral("mobileContinueCard"));
	m_continueCard->setAttribute(Qt::WA_StyledBackground, true);
	m_continueCard->setStyleSheet(
		QStringLiteral("QWidget#mobileContinueCard { background: %1; "
					   "border: 1px solid %2; border-radius: %3px; }")
			.arg(t.surface2.name(), t.outline.name())
			.arg(radiusMedium()));
	QHBoxLayout *continueLayout = new QHBoxLayout(m_continueCard);
	continueLayout->setContentsMargins(dp(12), dp(12), dp(12), dp(12));
	continueLayout->setSpacing(dp(12));
	m_continueThumb = new QLabel;
	m_continueThumb->setFixedSize(dp(84), dp(84));
	m_continueThumb->setAlignment(Qt::AlignCenter);
	continueLayout->addWidget(m_continueThumb);
	QVBoxLayout *continueText = new QVBoxLayout;
	continueText->setSpacing(dp(2));
	QLabel *continueHeading = new QLabel(tr("Continue drawing"));
	continueHeading->setProperty("mobileRole", QStringLiteral("dim"));
	applyFont(continueHeading, TextRole::Caption, true);
	continueText->addWidget(continueHeading);
	m_continueTitle = new QLabel;
	m_continueTitle->setProperty("mobileRole", QStringLiteral("title"));
	m_continueTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	applyFont(m_continueTitle, TextRole::Subtitle, true);
	continueText->addWidget(m_continueTitle);
	m_continueMeta = new QLabel;
	m_continueMeta->setProperty("mobileRole", QStringLiteral("dim"));
	m_continueMeta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	applyFont(m_continueMeta, TextRole::Caption);
	continueText->addWidget(m_continueMeta);
	continueText->addStretch(1);
	continueLayout->addLayout(continueText, 1);
	QPushButton *continueButton = new QPushButton(tr("Open"));
	continueButton->setProperty("mobileRole", QStringLiteral("primary"));
	connect(
		continueButton, &QPushButton::clicked, this, &Hub::closeRequested);
	continueLayout->addWidget(continueButton, 0, Qt::AlignVCenter);
	layout->addWidget(m_continueCard);

	// Start actions.
	layout->addWidget(sectionLabel(tr("Start")));
	QWidget *actions = new QWidget;
	m_actionGrid = new QGridLayout(actions);
	m_actionGrid->setContentsMargins(0, 0, 0, 0);
	m_actionGrid->setSpacing(dp(8));
	auto addAction = [this](
						 const QString &iconName, const QString &label,
						 const QString &sub, const std::function<void()> &fn) {
		ActionCard *card = new ActionCard(iconName, label, sub);
		connect(card, &ActionCard::clicked, this, fn);
		m_actionCards.append(card);
		return card;
	};
	addAction(
		QStringLiteral("plus"), tr("New canvas"), tr("Presets & custom size"),
		[this] {
			showNewPage();
		});
	addAction(
		QStringLiteral("open"), tr("Open file"), tr("Projects and images"),
		[this] {
			expectDocumentLoad();
			m_mw->open();
		});
	addAction(
		QStringLiteral("import"), tr("Import image"),
		tr("Start a canvas from a picture"), [this] {
			expectDocumentLoad();
			m_mw->open();
		});
	m_recoverCard = addAction(
		QStringLiteral("recover"), tr("Recover"), tr("Autosaved work"),
		[this] {
			startDialogPage(int(dialogs::StartDialog::Recover));
		});
	addAction(
		QStringLiteral("join"), tr("Join session"), tr("Invite link or address"),
		[this] {
			startDialogPage(int(dialogs::StartDialog::Join));
		});
	addAction(
		QStringLiteral("browse"), tr("Browse sessions"), tr("Public sessions"),
		[this] {
			startDialogPage(int(dialogs::StartDialog::Browse));
		});
	addAction(
		QStringLiteral("host"), tr("Host session"),
		tr("Share the open canvas"), [this] {
			startDialogPage(int(dialogs::StartDialog::Host));
		});
	layout->addWidget(actions);

	// Recent projects.
	layout->addWidget(sectionLabel(tr("Recent")));
	QWidget *recent = new QWidget;
	m_recentGrid = new QGridLayout(recent);
	m_recentGrid->setContentsMargins(0, 0, 0, 0);
	m_recentGrid->setSpacing(dp(10));
	layout->addWidget(recent);
	m_recentEmpty = new QLabel(
		tr("Files you open or save show up here."));
	m_recentEmpty->setProperty("mobileRole", QStringLiteral("dim"));
	m_recentEmpty->setWordWrap(true);
	applyFont(m_recentEmpty, TextRole::Body);
	m_recentEmpty->setContentsMargins(dp(4), dp(8), dp(4), dp(8));
	layout->addWidget(m_recentEmpty);
	layout->addStretch(1);

	m_mainScroll = makeScroll(content);
	return m_mainScroll;
}

QWidget *Hub::buildNewPage()
{
	QWidget *page = new QWidget;
	QVBoxLayout *pageLayout = new QVBoxLayout(page);
	pageLayout->setContentsMargins(0, 0, 0, 0);
	pageLayout->setSpacing(0);

	QHBoxLayout *header = new QHBoxLayout;
	header->setContentsMargins(dp(8), dp(4), dp(16), dp(4));
	ChromeButton *back = new ChromeButton(QStringLiteral("back"), tr("Back"));
	connect(back, &ChromeButton::clicked, this, &Hub::showMainPage);
	header->addWidget(back);
	QLabel *title = new QLabel(tr("New canvas"));
	title->setProperty("mobileRole", QStringLiteral("title"));
	applyFont(title, TextRole::Title, true);
	header->addWidget(title, 1);
	pageLayout->addLayout(header);

	QWidget *content = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(content);
	layout->setContentsMargins(dp(16), 0, dp(16), dp(24));
	layout->setSpacing(dp(8));

	QSize portrait = phoneSize(true);
	m_presets = {
		{QStringLiteral("portrait"), tr("Phone portrait"), portrait, false},
		{QStringLiteral("landscape"), tr("Phone landscape"),
		 portrait.transposed(), false},
		{QStringLiteral("square"), tr("Square"), QSize(2048, 2048), false},
		{QStringLiteral("a4"), tr("A4, 300 dpi"), QSize(2480, 3508), false},
		{QStringLiteral("film"), tr("Animation 1080p"), QSize(1920, 1080),
		 true},
		{QStringLiteral("film"), tr("Animation 720p"), QSize(1280, 720), true},
	};

	layout->addWidget(sectionLabel(tr("Presets")));
	QWidget *presets = new QWidget;
	m_presetGrid = new QGridLayout(presets);
	m_presetGrid->setContentsMargins(0, 0, 0, 0);
	m_presetGrid->setSpacing(dp(8));
	for(int i = 0; i < m_presets.size(); ++i) {
		const Preset &p = m_presets[i];
		ActionCard *card = new ActionCard(
			p.iconName, p.title,
			QStringLiteral("%1 × %2").arg(p.size.width()).arg(p.size.height()));
		card->setCheckable(true);
		connect(card, &ActionCard::clicked, this, [this, i] {
			selectPreset(i);
		});
		m_presetCards.append(card);
	}
	layout->addWidget(presets);

	m_fpsRow = new QWidget;
	QVBoxLayout *fpsLayout = new QVBoxLayout(m_fpsRow);
	fpsLayout->setContentsMargins(0, 0, 0, 0);
	fpsLayout->addWidget(sectionLabel(tr("Frame rate")));
	QWidget *fpsChipsWidget = new QWidget;
	FlowLayout *fpsChips = new FlowLayout(fpsChipsWidget);
	m_fpsGroup = new QButtonGroup(this);
	for(int fps : {8, 12, 15, 24, 30, 60}) {
		QToolButton *chip = new QToolButton;
		chip->setProperty("mobileChip", true);
		chip->setCheckable(true);
		chip->setText(tr("%1 fps").arg(fps));
		applyFont(chip, TextRole::Label, true);
		m_fpsGroup->addButton(chip, fps);
		fpsChips->addWidget(chip);
		if(fps == 24) {
			chip->setChecked(true);
		}
	}
	fpsLayout->addWidget(fpsChipsWidget);
	QLabel *fpsNote = new QLabel(
		tr("The frame rate and range can be changed later in the animation "
		   "panel."));
	fpsNote->setWordWrap(true);
	fpsNote->setProperty("mobileRole", QStringLiteral("dim"));
	applyFont(fpsNote, TextRole::Caption);
	fpsLayout->addWidget(fpsNote);
	m_fpsRow->hide();
	layout->addWidget(m_fpsRow);

	layout->addWidget(sectionLabel(tr("Size in pixels")));
	QHBoxLayout *sizeRow = new QHBoxLayout;
	sizeRow->setSpacing(dp(8));
	auto makeSpin = [] {
		QSpinBox *box = new QSpinBox;
		box->setProperty("mobileChrome", true);
		box->setRange(1, 32767);
		box->setButtonSymbols(QAbstractSpinBox::NoButtons);
		box->setAlignment(Qt::AlignCenter);
		applyFont(box, TextRole::Body, true);
		return box;
	};
	m_widthBox = makeSpin();
	m_widthBox->setAccessibleName(tr("Width"));
	m_heightBox = makeSpin();
	m_heightBox->setAccessibleName(tr("Height"));
	QSize initial = dpApp().safeNewCanvasSize();
	m_widthBox->setValue(initial.width());
	m_heightBox->setValue(initial.height());
	sizeRow->addWidget(m_widthBox, 1);
	QLabel *times = new QLabel(QStringLiteral("×"));
	applyFont(times, TextRole::Subtitle, true);
	sizeRow->addWidget(times);
	sizeRow->addWidget(m_heightBox, 1);
	ChromeButton *swap =
		new ChromeButton(QStringLiteral("swap"), tr("Swap width and height"));
	connect(swap, &ChromeButton::clicked, this, [this] {
		int w = m_widthBox->value();
		m_widthBox->setValue(m_heightBox->value());
		m_heightBox->setValue(w);
	});
	sizeRow->addWidget(swap);
	layout->addLayout(sizeRow);
	auto sizeEdited = [this] {
		if(m_selectedPreset >= 0) {
			const Preset &p = m_presets[m_selectedPreset];
			if(p.size != QSize(m_widthBox->value(), m_heightBox->value()) &&
			   p.size.transposed() !=
				   QSize(m_widthBox->value(), m_heightBox->value())) {
				m_presetCards[m_selectedPreset]->setChecked(false);
				m_selectedPreset = -1;
				m_fpsRow->hide();
			}
		}
		updateNewPageSummary();
	};
	connect(
		m_widthBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
		sizeEdited);
	connect(
		m_heightBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
		sizeEdited);

	layout->addWidget(sectionLabel(tr("Background")));
	QWidget *bgRowWidget = new QWidget;
	FlowLayout *bgRow = new FlowLayout(bgRowWidget);
	m_backgroundGroup = new QButtonGroup(this);
	QStringList bgNames = {
		tr("White"), tr("Transparent"), tr("Black"), tr("Custom…")};
	for(int i = 0; i < bgNames.size(); ++i) {
		QToolButton *chip = new QToolButton;
		chip->setProperty("mobileChip", true);
		chip->setCheckable(true);
		chip->setText(bgNames[i]);
		applyFont(chip, TextRole::Label, true);
		m_backgroundGroup->addButton(chip, i);
		bgRow->addWidget(chip);
	}
	m_backgroundGroup->button(0)->setChecked(true);
	m_customBackground = dpAppConfig()->getNewCanvasBackColor();
	connect(
		m_backgroundGroup, QOverload<int>::of(&QButtonGroup::idClicked), this,
		[this](int id) {
			if(id == 3) {
				color_widgets::ColorDialog *dlg =
					dialogs::newDeleteOnCloseColorDialog(
						m_customBackground, this);
				connect(
					dlg, &color_widgets::ColorDialog::colorSelected, this,
					[this](const QColor &color) {
						m_customBackground = color;
						updateNewPageSummary();
					});
				utils::showWindow(dlg, true);
			}
			updateNewPageSummary();
		});
	layout->addWidget(bgRowWidget);

	m_summary = new QLabel;
	m_summary->setWordWrap(true);
	m_summary->setProperty("mobileRole", QStringLiteral("dim"));
	applyFont(m_summary, TextRole::Label);
	m_summary->setContentsMargins(dp(4), dp(12), dp(4), dp(4));
	layout->addWidget(m_summary);

	QPushButton *create = new QPushButton(tr("Create canvas"));
	create->setProperty("mobileRole", QStringLiteral("primary"));
	connect(create, &QPushButton::clicked, this, &Hub::createCanvas);
	layout->addWidget(create);
	layout->addStretch(1);

	pageLayout->addWidget(makeScroll(content), 1);
	updateNewPageSummary();
	return page;
}

void Hub::refreshRecents()
{
	for(RecentCard *card : m_recentCards) {
		m_recentGrid->removeWidget(card);
		card->deleteLater();
	}
	m_recentCards.clear();
#ifndef __EMSCRIPTEN__
	for(const utils::Recents::File &file : dpApp().recents().getFiles()) {
		RecentCard *card = new RecentCard(file.path, file.id);
		QString path = file.path;
		long long id = file.id;
		connect(card, &RecentCard::clicked, this, [this, path] {
			openRecent(path);
		});
		card->onMenu = [this, path, id] {
			showRecentActions(path, id);
		};
		m_recentCards.append(card);
	}
#endif
	m_recentEmpty->setVisible(m_recentCards.isEmpty());
	m_columns = 0;
	reflowGrids();
	m_thumbnailCursor = 0;
	QTimer::singleShot(0, this, &Hub::loadNextThumbnail);
}

void Hub::loadNextThumbnail()
{
	// One thumbnail per event loop iteration, so the hub stays responsive
	// even with many large files.
	while(m_thumbnailCursor < m_recentCards.size()) {
		RecentCard *card = m_recentCards[m_thumbnailCursor++];
		if(!card->thumbnailLoaded()) {
			ProjectMeta meta;
			QImage img = thumbnails::load(card->path(), &meta);
			card->setThumbnail(img, meta);
			QTimer::singleShot(0, this, &Hub::loadNextThumbnail);
			return;
		}
	}
}

void Hub::refreshContinueCard()
{
	Document *doc = m_mw ? m_mw->findChild<Document *>() : nullptr;
	canvas::CanvasModel *canvas = doc ? doc->canvas() : nullptr;
	m_continueCard->setVisible(canvas != nullptr);
	if(!canvas) {
		return;
	}
	ProjectMeta meta;
	QImage img = thumbnails::renderCanvas(canvas, &meta);
	qreal dpr = devicePixelRatioF();
	int s = qRound(dp(84) * dpr);
	QPixmap pixmap(s, s);
	pixmap.fill(Qt::transparent);
	{
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		QPainterPath clip;
		clip.addRoundedRect(QRectF(0, 0, s, s), s / 8.0, s / 8.0);
		painter.setClipPath(clip);
		drawChecker(painter, QRectF(0, 0, s, s), Theme::current());
		if(!img.isNull()) {
			QSizeF target = QSizeF(img.size()).scaled(s, s, Qt::KeepAspectRatio);
			painter.drawImage(
				QRectF((s - target.width()) / 2.0, (s - target.height()) / 2.0,
					   target.width(), target.height()),
				img);
		}
	}
	pixmap.setDevicePixelRatio(dpr);
	m_continueThumb->setPixmap(pixmap);
	QString title = m_mw->windowTitle();
	title.remove(QStringLiteral("[*]"));
	m_continueTitle->setText(title.trimmed());
	QStringList parts;
	if(meta.size.isValid() && !meta.size.isEmpty()) {
		parts.append(
			QStringLiteral("%1×%2").arg(meta.size.width()).arg(meta.size.height()));
	}
	if(meta.animated) {
		parts.append(tr("%n key frame(s)", nullptr, meta.keyFrames));
	}
	if(doc->client() && doc->client()->isConnected()) {
		parts.append(tr("in a session"));
	} else if(doc->isDirty()) {
		parts.append(tr("unsaved changes"));
	}
	m_continueMeta->setText(parts.join(QStringLiteral(" · ")));
}

void Hub::refreshRecoveryBadge()
{
	project::RecoveryModel model(utils::paths::autosaveWritablePath());
	bool potential = model.checkPotentialEntries();
	m_recoverCard->setBadge(potential ? QStringLiteral("!") : QString());
	m_recoverCard->setSubtitle(
		potential ? tr("Unsaved work can be recovered") : tr("Autosaved work"));
}

void Hub::reflowGrids()
{
	int available = width() - dp(32);
	int actionColumns = qBound(2, available / dp(170), 4);
	if(m_actionGrid->property("columns").toInt() != actionColumns) {
		m_actionGrid->setProperty("columns", actionColumns);
		for(ActionCard *card : m_actionCards) {
			m_actionGrid->removeWidget(card);
		}
		for(int i = 0; i < m_actionCards.size(); ++i) {
			m_actionGrid->addWidget(
				m_actionCards[i], i / actionColumns, i % actionColumns);
		}
	}

	int columns = qBound(2, available / dp(170), 6);
	if(columns != m_columns) {
		m_columns = columns;
		for(RecentCard *card : m_recentCards) {
			m_recentGrid->removeWidget(card);
		}
		for(int i = 0; i < m_recentCards.size(); ++i) {
			m_recentGrid->addWidget(m_recentCards[i], i / columns, i % columns);
		}
		for(int c = 0; c < 6; ++c) {
			m_recentGrid->setColumnStretch(c, c < columns ? 1 : 0);
		}
	}

	int presetColumns = qBound(1, available / dp(220), 3);
	if(presetColumns != m_presetColumns) {
		m_presetColumns = presetColumns;
		for(ActionCard *card : m_presetCards) {
			m_presetGrid->removeWidget(card);
		}
		for(int i = 0; i < m_presetCards.size(); ++i) {
			m_presetGrid->addWidget(
				m_presetCards[i], i / presetColumns, i % presetColumns);
		}
	}
}

void Hub::showRecentActions(const QString &path, long long id)
{
	bool local = isLocalFile(path);
	QVector<QPair<QString, QString>> choices;
	QVector<int> ids;
	choices.append({QStringLiteral("open"), tr("Open")});
	ids.append(0);
	if(local) {
		choices.append({QStringLiteral("rename"), tr("Rename…")});
		ids.append(1);
		choices.append({QStringLiteral("duplicate"), tr("Duplicate")});
		ids.append(2);
	}
	choices.append({QStringLiteral("share"), tr("Save a copy as…")});
	ids.append(3);
	choices.append({QStringLiteral("close"), tr("Remove from list")});
	ids.append(4);
	if(local) {
		choices.append({QStringLiteral("trash"), tr("Delete file…")});
		ids.append(5);
	}
	showChoices(
		io::PathInfo(path).basename(), choices,
		[this, path, id, ids](int i) {
			switch(ids.value(i, -1)) {
			case 0:
				openRecent(path);
				break;
			case 1:
				renameRecent(path, id);
				break;
			case 2:
				duplicateRecent(path);
				break;
			case 3:
				saveCopyOf(path);
				break;
			case 4:
#ifndef __EMSCRIPTEN__
				dpApp().recents().removeFileById(id);
#endif
				break;
			case 5:
				deleteRecent(path, id);
				break;
			default:
				break;
			}
		});
}

void Hub::showChoices(
	const QString &title, const QVector<QPair<QString, QString>> &choices,
	const std::function<void(int)> &onChosen)
{
	new ChoiceOverlay(this, title, choices, onChosen);
}

void Hub::openRecent(const QString &path)
{
	expectDocumentLoad();
	m_mw->openRecent(path);
}

void Hub::renameRecent(const QString &path, long long id)
{
	QFileInfo info(path);
	QString suffix = info.suffix();
	utils::getInputText(
		m_mw, tr("Rename"), tr("New name:"), info.completeBaseName(),
		[this, path, id, info, suffix](const QString &input) {
			QString name = input.trimmed();
			if(name.isEmpty() || name.contains(QLatin1Char('/')) ||
			   name.contains(QLatin1Char('\\'))) {
				return;
			}
			QString target = info.dir().filePath(
				suffix.isEmpty() ? name
								 : QStringLiteral("%1.%2").arg(name, suffix));
			if(target == path) {
				return;
			}
			if(QFileInfo::exists(target)) {
				utils::showWarning(
					m_mw, tr("Rename"),
					tr("A file with that name already exists."));
				return;
			}
			Document *doc = m_mw->findChild<Document *>();
			if(doc && doc->currentPath() == path) {
				utils::showWarning(
					m_mw, tr("Rename"),
					tr("This file is currently open. Close it or save it "
					   "under a new name instead."));
				return;
			}
			if(!QFile::rename(path, target)) {
				utils::showWarning(
					m_mw, tr("Rename"), tr("Could not rename the file."));
				return;
			}
			thumbnails::move(path, target);
#ifndef __EMSCRIPTEN__
			dpApp().recents().removeFileById(id);
			dpApp().recents().addFile(target);
#endif
		});
}

void Hub::duplicateRecent(const QString &path)
{
	QFileInfo info(path);
	QString suffix = info.suffix();
	QString base = info.completeBaseName();
	QString target;
	for(int i = 1; i < 1000; ++i) {
		QString name = i == 1 ? tr("%1 copy").arg(base)
							  : tr("%1 copy %2").arg(base).arg(i);
		target = info.dir().filePath(
			suffix.isEmpty() ? name : QStringLiteral("%1.%2").arg(name, suffix));
		if(!QFileInfo::exists(target)) {
			break;
		}
	}
	if(!QFile::copy(path, target)) {
		utils::showWarning(
			m_mw, tr("Duplicate"), tr("Could not copy the file."));
		return;
	}
	thumbnails::copy(path, target);
#ifndef __EMSCRIPTEN__
	dpApp().recents().addFile(target);
#endif
}

void Hub::deleteRecent(const QString &path, long long id)
{
	QMessageBox *box = utils::makeQuestion(
		m_mw, tr("Delete file"),
		tr("Delete \"%1\" permanently? This cannot be undone.")
			.arg(io::PathInfo(path).basename()));
	connect(box, &QMessageBox::accepted, this, [this, path, id] {
		Document *doc = m_mw->findChild<Document *>();
		if(doc && doc->currentPath() == path) {
			utils::showWarning(
				m_mw, tr("Delete file"),
				tr("This file is currently open and can't be deleted."));
			return;
		}
		if(!QFile::remove(path)) {
			utils::showWarning(
				m_mw, tr("Delete file"), tr("Could not delete the file."));
			return;
		}
		thumbnails::remove(path);
#ifndef __EMSCRIPTEN__
		dpApp().recents().removeFileById(id);
#endif
	});
	utils::showMessageBox(box);
}

void Hub::saveCopyOf(const QString &path)
{
	QString name = io::PathInfo(path).basename();
	QFileDialog dialog(m_mw);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	// On Android, the title is used as the initial file name.
	dialog.setWindowTitle(name);
	dialog.selectFile(name);
	if(dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
		return;
	}
	QString target = dialog.selectedFiles().first().trimmed();
	if(target.isEmpty() || target == path) {
		return;
	}
	QFile in(path);
	QFile out(target);
	bool ok = in.open(QIODevice::ReadOnly) &&
			  out.open(QIODevice::WriteOnly | QIODevice::Truncate);
	while(ok && !in.atEnd()) {
		QByteArray chunk = in.read(1024 * 1024);
		ok = out.write(chunk) == chunk.size();
	}
	if(!ok) {
		utils::showWarning(
			m_mw, tr("Save a copy"), tr("Could not write the copy."));
	}
}

void Hub::createCanvas()
{
	QSize size(m_widthBox->value(), m_heightBox->value());
	QColor background;
	switch(m_backgroundGroup->checkedId()) {
	case 1:
		background = QColor(0, 0, 0, 0);
		break;
	case 2:
		background = Qt::black;
		break;
	case 3:
		background = m_customBackground;
		break;
	default:
		background = Qt::white;
		break;
	}

	int fps = 0;
	if(m_selectedPreset >= 0 && m_presets[m_selectedPreset].animation) {
		fps = m_fpsGroup->checkedId();
	}

	Document *doc = m_mw->findChild<Document *>();
	if(doc && fps > 0) {
		// Apply the frame rate once the new canvas exists, using the same
		// commands the timeline's properties dialog sends.
		QPointer<Document> guardedDoc = doc;
		QMetaObject::Connection *connection = new QMetaObject::Connection;
		QTimer *expiry = new QTimer(this);
		expiry->setSingleShot(true);
		*connection = connect(
			doc, &Document::canvasChanged, this,
			[guardedDoc, fps, connection, expiry](canvas::CanvasModel *canvas) {
				disconnect(*connection);
				delete connection;
				expiry->deleteLater();
				if(!guardedDoc || !canvas) {
					return;
				}
				QTimer::singleShot(0, guardedDoc, [guardedDoc, fps] {
					canvas::CanvasModel *c =
						guardedDoc ? guardedDoc->canvas() : nullptr;
					if(!c) {
						return;
					}
					uint8_t contextId = c->localUserId();
					net::Message msgs[] = {
						net::makeUndoPointMessage(contextId),
						net::makeSetMetadataIntMessage(
							contextId, DP_MSG_SET_METADATA_INT_FIELD_FRAMERATE,
							fps),
						net::makeSetMetadataIntMessage(
							contextId,
							DP_MSG_SET_METADATA_INT_FIELD_FRAMERATE_FRACTION,
							0),
					};
					guardedDoc->client()->sendCommands(
						int(sizeof(msgs) / sizeof(msgs[0])), msgs);
				});
			});
		// If the user cancels replacing the canvas, don't apply it later.
		connect(expiry, &QTimer::timeout, this, [connection] {
			disconnect(*connection);
			delete connection;
		});
		expiry->start(120000);
	}

	expectDocumentLoad();
	m_mw->newDocument(size, background);
}

void Hub::selectPreset(int index)
{
	m_selectedPreset = index;
	for(int i = 0; i < m_presetCards.size(); ++i) {
		m_presetCards[i]->setChecked(i == index);
	}
	const Preset &p = m_presets[index];
	{
		QSignalBlocker b1(m_widthBox);
		QSignalBlocker b2(m_heightBox);
		m_widthBox->setValue(p.size.width());
		m_heightBox->setValue(p.size.height());
	}
	m_fpsRow->setVisible(p.animation);
	updateNewPageSummary();
}

void Hub::updateNewPageSummary()
{
	qint64 pixels = qint64(m_widthBox->value()) * m_heightBox->value();
	QString text =
		tr("%1 × %2 pixels").arg(m_widthBox->value()).arg(m_heightBox->value());
	if(pixels > 64LL * 1024 * 1024) {
		text += QStringLiteral("\n") +
				tr("This is a very large canvas and may be slow or run out of "
				   "memory on a phone.");
	}
	m_summary->setText(text);
}

void Hub::expectDocumentLoad()
{
	m_expectLoad = true;
}

void Hub::showMainPage()
{
	m_pages->setCurrentWidget(m_mainPage);
}

void Hub::showNewPage()
{
	m_pages->setCurrentWidget(m_newPage);
	reflowGrids();
}

void Hub::startDialogPage(int page)
{
	expectDocumentLoad();
	m_mw->showStartDialogOnPage(page);
}

QSize Hub::phoneSize(bool portrait) const
{
	QScreen *screen = m_mw && m_mw->screen() ? m_mw->screen()
											 : QGuiApplication::primaryScreen();
	QSize size(1080, 2400);
	if(screen) {
		QSize s(int(screen->size().width() * screen->devicePixelRatio()),
				int(screen->size().height() * screen->devicePixelRatio()));
		if(s.width() > 0 && s.height() > 0) {
			size = s;
		}
	}
	if((size.width() > size.height()) == portrait) {
		size.transpose();
	}
	return size;
}

}
