// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpengine/save_enums.h>
}
// Drawpile Mobile (fork): scripted UI driver used for automated screenshots,
// functional checks and timing measurements on desktop builds. It is inert
// unless DRAWPILE_MOBILE_TEST_SCRIPT points to a script file.
//
// Script commands, one per line (# starts a comment):
//   wait <ms>                    process events for the given time
//   resize <w> <h>               resize the main window (logical pixels)
//   shot <file.png>              grab the main window into a PNG
//   panel <id> [tab]             open a sheet panel
//   closepanel                   close the sheet
//   hub | hidehub                show or hide the project hub
//   trigger <action>             trigger a QAction by object name
//   check <action> <0|1>         set a checkable action's state
//   tap <x> <y>                  click at a window position
//   longpress <x> <y>            press for 700ms at a window position
//   drag <x1> <y1> <x2> <y2> [n] press, move in n steps, release
//   key <name>                   send a key press and release (e.g. Back)
//   inventory <path>             write the action inventory
//   time <label>                 log milliseconds since the previous mark
//   log <text>                   write a line to the log
//   quit                         exit the application without prompts
#include "desktop/mainwindow.h"
#include "desktop/utils/hostparams.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/canvas/userlist.h"
#include "libclient/document.h"
#include "libclient/drawdance/canvasstate.h"
#include "libclient/drawdance/documentmetadata.h"
#include "libclient/drawdance/layerlist.h"
#include "libclient/drawdance/timeline.h"
#include "libclient/net/client.h"
#include <QAbstractButton>
#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include "desktop/mobile/mobileui.h"
#include "desktop/mobile/shell.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <cstdlib>
#include <functional>

namespace mobile {

namespace {

class TestDriver final : public QObject {
public:
	TestDriver(MainWindow *mw, const QString &scriptPath, const QString &logPath)
		: QObject(mw)
		, m_mw(mw)
		, m_log(logPath)
	{
		QFile file(scriptPath);
		if(file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QTextStream in(&file);
			while(!in.atEnd()) {
				QString line = in.readLine().trimmed();
				if(!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
					m_lines.append(line);
				}
			}
		}
		m_log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
		m_timer.start();
		m_mark.start();
		QTimer::singleShot(1500, this, &TestDriver::next);
	}

private:
	void log(const QString &text)
	{
		QString line = QStringLiteral("[%1] %2\n")
						   .arg(m_timer.elapsed(), 7)
						   .arg(text);
		m_log.write(line.toUtf8());
		m_log.flush();
	}

	void pump(int ms)
	{
		QElapsedTimer t;
		t.start();
		do {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
			QThread::msleep(2);
		} while(t.elapsed() < ms);
	}

	QWidget *widgetAt(const QPoint &pos, QPoint &localPos)
	{
		QPoint global = m_mw->mapToGlobal(pos);
		QWidget *w = QApplication::widgetAt(global);
		if(!w) {
			w = m_mw->childAt(pos);
		}
		if(!w) {
			w = m_mw;
		}
		localPos = w->mapFromGlobal(global);
		return w;
	}

	void sendMouse(QEvent::Type type, const QPoint &pos, Qt::MouseButtons buttons)
	{
		QPoint local;
		QWidget *w = m_grabbed && type != QEvent::MouseButtonPress
						 ? m_grabbed.data()
						 : widgetAt(pos, local);
		if(m_grabbed && type != QEvent::MouseButtonPress) {
			local = w->mapFromGlobal(m_mw->mapToGlobal(pos));
		}
		if(type == QEvent::MouseButtonPress) {
			m_grabbed = w;
		}
		QPoint global = m_mw->mapToGlobal(pos);
		Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton
														   : Qt::LeftButton;
		QMouseEvent event(
			type, QPointF(local), QPointF(global), button, buttons,
			Qt::NoModifier);
		QCoreApplication::sendEvent(w, &event);
		if(type == QEvent::MouseButtonRelease) {
			m_grabbed.clear();
		}
	}

	void next()
	{
		if(!m_mw) {
			return;
		}
		if(m_index >= m_lines.size()) {
			log(QStringLiteral("script finished"));
			return;
		}
		QString line = m_lines[m_index++];
		QStringList args = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		QString cmd = args.takeFirst();
		Shell *shell = Shell::of(m_mw);
		int delay = 50;

		if(cmd == QStringLiteral("wait")) {
			delay = args.value(0).toInt();
		} else if(cmd == QStringLiteral("resize")) {
			m_mw->showNormal();
			m_mw->setGeometry(
				0, 0, args.value(0).toInt(), args.value(1).toInt());
			pump(300);
		} else if(cmd == QStringLiteral("phone")) {
			// Emulate Android, where the window is always exactly the size of
			// the screen, even if that's smaller than the layout's minimum.
			m_mw->setMinimumSize(1, 1);
			m_mw->showNormal();
			m_mw->setGeometry(
				0, 0, args.value(0).toInt(), args.value(1).toInt());
			pump(400);
			log(QStringLiteral("phone %1x%2 -> %3x%4")
					.arg(args.value(0)).arg(args.value(1))
					.arg(m_mw->width()).arg(m_mw->height()));
		} else if(cmd == QStringLiteral("shot")) {
			pump(100);
			QPixmap pixmap = m_mw->grab();
			bool ok = pixmap.save(args.value(0));
			log(QStringLiteral("shot %1 %2x%3 %4")
					.arg(args.value(0))
					.arg(pixmap.width())
					.arg(pixmap.height())
					.arg(ok ? QStringLiteral("ok") : QStringLiteral("FAILED")));
		} else if(cmd == QStringLiteral("panel") && shell) {
			QElapsedTimer t;
			t.start();
			shell->openPanel(args.value(0), args.value(1));
			pump(0);
			log(QStringLiteral("panel %1 opened in %2 ms (event processing)")
					.arg(args.value(0))
					.arg(t.elapsed()));
			delay = 400;
		} else if(cmd == QStringLiteral("closepanel") && shell) {
			shell->closePanel();
			delay = 400;
		} else if(cmd == QStringLiteral("hub") && shell) {
			shell->showHub(-1);
			delay = 300;
		} else if(cmd == QStringLiteral("hidehub") && shell) {
			shell->hideHub();
			delay = 300;
		} else if(cmd == QStringLiteral("trigger") || cmd == QStringLiteral("check")) {
			QAction *action = m_mw->findChild<QAction *>(args.value(0));
			if(!action) {
				log(QStringLiteral("action %1 NOT FOUND").arg(args.value(0)));
			} else if(cmd == QStringLiteral("check")) {
				bool want = args.value(1).toInt() != 0;
				if(action->isChecked() != want) {
					action->trigger();
				}
				log(QStringLiteral("check %1 -> %2").arg(args.value(0)).arg(action->isChecked()));
			} else {
				log(QStringLiteral("trigger %1 (enabled=%2)")
						.arg(args.value(0))
						.arg(action->isEnabled()));
				action->trigger();
			}
			delay = 300;
		} else if(cmd == QStringLiteral("tap")) {
			QPoint pos(args.value(0).toInt(), args.value(1).toInt());
			sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton);
			pump(60);
			sendMouse(QEvent::MouseButtonRelease, pos, Qt::NoButton);
			delay = 300;
		} else if(cmd == QStringLiteral("longpress")) {
			QPoint pos(args.value(0).toInt(), args.value(1).toInt());
			sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton);
			pump(700);
			sendMouse(QEvent::MouseButtonRelease, pos, Qt::NoButton);
			delay = 300;
		} else if(cmd == QStringLiteral("drag")) {
			QPoint a(args.value(0).toInt(), args.value(1).toInt());
			QPoint b(args.value(2).toInt(), args.value(3).toInt());
			int steps = qMax(1, args.value(4, QStringLiteral("20")).toInt());
			QElapsedTimer t;
			t.start();
			sendMouse(QEvent::MouseButtonPress, a, Qt::LeftButton);
			for(int i = 1; i <= steps; ++i) {
				QPoint p = a + (b - a) * i / steps;
				sendMouse(QEvent::MouseMove, p, Qt::LeftButton);
				pump(8);
			}
			sendMouse(QEvent::MouseButtonRelease, b, Qt::NoButton);
			log(QStringLiteral("drag %1 steps in %2 ms").arg(steps).arg(t.elapsed()));
			delay = 200;
		} else if(cmd == QStringLiteral("key")) {
			int key = args.value(0) == QStringLiteral("Back") ? Qt::Key_Back
					  : args.value(0) == QStringLiteral("Escape")
						  ? Qt::Key_Escape
						  : Qt::Key_unknown;
			QWidget *target = QApplication::focusWidget();
			if(!target) {
				target = m_mw;
			}
			QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
			QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
			QCoreApplication::sendEvent(target, &press);
			QCoreApplication::sendEvent(target, &release);
			delay = 300;
		} else if(cmd == QStringLiteral("inventory") && shell) {
			shell->dumpInventory(args.value(0));
		} else if(cmd == QStringLiteral("time")) {
			log(QStringLiteral("time %1: %2 ms").arg(args.join(QLatin1Char(' '))).arg(m_mark.restart()));
		} else if(cmd == QStringLiteral("dump")) {
			log(QStringLiteral("mw geometry %1,%2 %3x%4 min %5x%6 minHint %7x%8")
					.arg(m_mw->x()).arg(m_mw->y()).arg(m_mw->width())
					.arg(m_mw->height()).arg(m_mw->minimumWidth())
					.arg(m_mw->minimumHeight())
					.arg(m_mw->minimumSizeHint().width())
					.arg(m_mw->minimumSizeHint().height()));
			for(QObject *child : m_mw->children()) {
				if(QWidget *w = qobject_cast<QWidget *>(child)) {
					log(QStringLiteral("  %1 '%2' visible=%3 geom=%4,%5 %6x%7 minHint=%8x%9")
							.arg(QString::fromLatin1(w->metaObject()->className()),
								 w->objectName())
							.arg(w->isVisible())
							.arg(w->x()).arg(w->y()).arg(w->width()).arg(w->height())
							.arg(w->minimumSizeHint().width())
							.arg(w->minimumSizeHint().height()));
				}
			}
		} else if(cmd == QStringLiteral("tree")) {
			QWidget *root = m_mw->findChild<QWidget *>(args.value(0));
			std::function<void(QWidget *, int)> walk = [&](QWidget *w, int depth) {
				if(depth > args.value(1, QStringLiteral("6")).toInt() || !w->isVisible()) {
					return;
				}
				log(QStringLiteral("%1%2 '%3' %4,%5 %6x%7 hint=%8x%9 min=%10x%11")
						.arg(QString(depth * 2, QLatin1Char(' ')))
						.arg(QString::fromLatin1(w->metaObject()->className()), w->objectName())
						.arg(w->x()).arg(w->y()).arg(w->width()).arg(w->height())
						.arg(w->sizeHint().width()).arg(w->sizeHint().height())
						.arg(w->minimumSizeHint().width()).arg(w->minimumSizeHint().height()));
				for(QObject *c : w->children()) {
					if(QWidget *cw = qobject_cast<QWidget *>(c)) {
						walk(cw, depth + 1);
					}
				}
			};
			if(root) {
				walk(root, 0);
			} else {
				log(QStringLiteral("tree: %1 not found").arg(args.value(0)));
			}
		} else if(cmd == QStringLiteral("tapname")) {
			QString name = args.join(QLatin1Char(' '));
			QAbstractButton *found = nullptr;
			QList<QWidget *> roots = QApplication::topLevelWidgets();
			for(QWidget *root : roots) {
				if(!root->isVisible()) {
					continue;
				}
				for(QAbstractButton *b : root->findChildren<QAbstractButton *>()) {
					QString text = b->text();
					text.remove(QLatin1Char('&'));
					if(b->isVisible() && (text == name || b->accessibleName() == name)) {
						found = b; // keep the last one, usually the topmost
					}
				}
			}
			if(found) {
				// Scroll it into view first, like a user would.
				for(QWidget *w = found->parentWidget(); w; w = w->parentWidget()) {
					if(QScrollArea *scroll = qobject_cast<QScrollArea *>(w)) {
						scroll->ensureWidgetVisible(found);
						pump(100);
						break;
					}
				}
				QPoint center = found->rect().center();
				QPoint pos = found->window() == m_mw
								 ? found->mapTo(m_mw, center)
								 : center;
				if(found->window() == m_mw) {
					sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton);
					pump(60);
					sendMouse(QEvent::MouseButtonRelease, pos, Qt::NoButton);
				} else {
					found->click();
				}
				log(QStringLiteral("tapname '%1' -> %2").arg(name, QString::fromLatin1(found->metaObject()->className())));
			} else {
				log(QStringLiteral("tapname '%1' NOT FOUND").arg(name));
			}
			delay = 400;
		} else if(cmd == QStringLiteral("prop")) {
			QObject *o = m_mw->findChild<QObject *>(args.value(0));
			log(QStringLiteral("prop %1.%2 = %3")
					.arg(args.value(0), args.value(1))
					.arg(o ? o->property(qUtf8Printable(args.value(1))).toString()
						   : QStringLiteral("<not found>")));
		} else if(cmd == QStringLiteral("canvasinfo")) {
			Document *doc = m_mw->findChild<Document *>();
			canvas::CanvasModel *canvas = doc ? doc->canvas() : nullptr;
			if(canvas) {
				drawdance::CanvasState cs =
					canvas->paintEngine()->historyCanvasState();
				log(QStringLiteral("canvas %1x%2 layers=%3 tracks=%4 fps=%5 "
								   "path='%6' dirty=%7 connected=%8 users=%9")
						.arg(cs.width()).arg(cs.height())
						.arg(cs.layers().count())
						.arg(cs.timeline().trackCount())
						.arg(cs.documentMetadata().effectiveFramerate())
						.arg(doc->currentPath())
						.arg(doc->isDirty())
						.arg(doc->client()->isConnected())
						.arg(canvas->userlist()->rowCount()));
			} else {
				log(QStringLiteral("canvas: none"));
			}
		} else if(cmd == QStringLiteral("dialogs")) {
			for(QWidget *w : QApplication::topLevelWidgets()) {
				if(w->isVisible() && w != m_mw) {
					log(QStringLiteral("  window %1 '%2'").arg(
						QString::fromLatin1(w->metaObject()->className()),
						w->windowTitle()));
				}
			}
		} else if(cmd == QStringLiteral("closedialogs")) {
			for(QWidget *w : QApplication::topLevelWidgets()) {
				if(w->isVisible() && w != m_mw) {
					log(QStringLiteral("  closing %1").arg(QString::fromLatin1(w->metaObject()->className())));
					if(QDialog *d = qobject_cast<QDialog *>(w)) {
						d->reject();
					} else {
						w->close();
					}
				}
			}
			delay = 300;
		} else if(cmd == QStringLiteral("acceptdialogs")) {
			for(QWidget *w : QApplication::topLevelWidgets()) {
				if(w->isVisible() && w != m_mw) {
					if(QMessageBox *box = qobject_cast<QMessageBox *>(w)) {
						QAbstractButton *yes = box->button(QMessageBox::Yes);
						if(!yes) {
							yes = box->button(QMessageBox::Ok);
						}
						log(QStringLiteral("  accepting message box '%1'").arg(box->text().left(80)));
						if(yes) {
							yes->click();
						} else {
							box->accept();
						}
					} else if(QDialog *d = qobject_cast<QDialog *>(w)) {
						log(QStringLiteral("  accepting %1").arg(QString::fromLatin1(d->metaObject()->className())));
						d->accept();
					}
				}
			}
			delay = 800;
		} else if(cmd == QStringLiteral("type")) {
			QString text = args.join(QLatin1Char(' '));
			QLineEdit *edit = qobject_cast<QLineEdit *>(QApplication::focusWidget());
			if(!edit) {
				for(QWidget *w : QApplication::topLevelWidgets()) {
					if(w->isVisible() && w != m_mw) {
						for(QLineEdit *e : w->findChildren<QLineEdit *>()) {
							if(e->isVisible() && e->isEnabled()) {
								edit = e;
								break;
							}
						}
					}
				}
			}
			if(edit) {
				edit->setFocus();
				edit->clear();
				edit->insert(text);
				log(QStringLiteral("typed into %1").arg(edit->objectName()));
			} else {
				log(QStringLiteral("type: no line edit"));
			}
		} else if(cmd == QStringLiteral("dialogok")) {
			for(QWidget *w : QApplication::topLevelWidgets()) {
				if(w->isVisible() && w != m_mw) {
					for(QDialogButtonBox *box : w->findChildren<QDialogButtonBox *>()) {
						QPushButton *ok = box->button(QDialogButtonBox::Ok);
						if(ok && ok->isVisible() && ok->isEnabled()) {
							log(QStringLiteral("  clicking OK in %1").arg(QString::fromLatin1(w->metaObject()->className())));
							ok->click();
						}
					}
				}
			}
			delay = 800;
		} else if(cmd == QStringLiteral("shotdialogs")) {
			int i = 0;
			for(QWidget *w : QApplication::topLevelWidgets()) {
				if(w->isVisible() && w != m_mw) {
					QString path = QStringLiteral("%1_%2.png").arg(args.value(0)).arg(i++);
					w->grab().save(path);
					log(QStringLiteral("  dialog shot %1 (%2)").arg(path, QString::fromLatin1(w->metaObject()->className())));
				}
			}
		} else if(cmd == QStringLiteral("host")) {
			HostParams params;
			params.title = QStringLiteral("Mobile fork test");
			params.alias = args.value(0);
			params.rememberAddress = false;
			params.nsfm = false;
			params.keepChat = false;
			params.deputies = false;
			params.undoLimit = 60;
			m_mw->hostSession(params, 0);
			log(QStringLiteral("host requested alias=%1").arg(args.value(0)));
			delay = 1500;
		} else if(cmd == QStringLiteral("savecanvas")) {
			Document *doc = m_mw->findChild<Document *>();
			QString path = args.value(0);
			QString ext = path.section(QLatin1Char('.'), -1).toLower();
			DP_SaveImageType type = ext == QStringLiteral("ora")	? DP_SAVE_IMAGE_ORA
									: ext == QStringLiteral("png")	? DP_SAVE_IMAGE_PNG
									: ext == QStringLiteral("psd")	? DP_SAVE_IMAGE_PSD
									: ext == QStringLiteral("dppr") ? DP_SAVE_IMAGE_PROJECT_CANVAS
																	: DP_SAVE_IMAGE_ORA;
			doc->saveCanvasAs(path, type, false, false, false);
			log(QStringLiteral("save requested %1").arg(path));
			delay = 1500;
		} else if(cmd == QStringLiteral("open")) {
			m_mw->openPath(args.value(0));
			log(QStringLiteral("open requested %1").arg(args.value(0)));
			delay = 1500;
		} else if(cmd == QStringLiteral("log")) {
			log(args.join(QLatin1Char(' ')));
		} else if(cmd == QStringLiteral("quit")) {
			log(QStringLiteral("quit"));
			m_log.close();
			std::_Exit(0);
		} else {
			log(QStringLiteral("unknown command: %1").arg(line));
		}
		QTimer::singleShot(delay, this, &TestDriver::next);
	}

	QPointer<MainWindow> m_mw;
	QFile m_log;
	QStringList m_lines;
	int m_index = 0;
	QElapsedTimer m_timer;
	QElapsedTimer m_mark;
	QPointer<QWidget> m_grabbed;
};

}

void startTestDriver(MainWindow *mw)
{
	QByteArray script = qgetenv("DRAWPILE_MOBILE_TEST_SCRIPT");
	if(!script.isEmpty() && mw) {
		QByteArray logPath = qgetenv("DRAWPILE_MOBILE_TEST_LOG");
		new TestDriver(
			mw, QString::fromLocal8Bit(script),
			logPath.isEmpty() ? QStringLiteral("mobile-test.log")
							  : QString::fromLocal8Bit(logPath));
	}
}

}
