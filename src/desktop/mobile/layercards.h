// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_MOBILE_LAYERCARDS_H
#define DESKTOP_MOBILE_LAYERCARDS_H
#include <QAbstractItemDelegate>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <memory>

class MainWindow;
class QAbstractItemView;

namespace mobile {

// Wraps upstream's layer list delegate to turn its rows into touch-sized
// cards with a thumbnail of the layer's content. Everything the upstream
// delegate does (visibility toggle, check boxes, drag-to-toggle, double click
// for properties, censoring, clipping, colors) keeps working: it still paints
// and handles events, just in the part of the card right of the thumbnail.
// Taps near the small visibility and check box glyphs are snapped onto them so
// that they get finger-sized hit areas.
class LayerCardDelegate final : public QAbstractItemDelegate {
	Q_OBJECT
public:
	LayerCardDelegate(
		QAbstractItemDelegate *inner, MainWindow *mw, QAbstractItemView *view);

	QAbstractItemDelegate *inner() const { return m_inner; }

	// Installs the card delegate on the view, or removes it again.
	static void install(MainWindow *mw, QAbstractItemView *view);
	static void uninstall(QAbstractItemView *view);

	void paint(
		QPainter *painter, const QStyleOptionViewItem &option,
		const QModelIndex &index) const override;

	QSize sizeHint(
		const QStyleOptionViewItem &option,
		const QModelIndex &index) const override;

	bool editorEvent(
		QEvent *event, QAbstractItemModel *model,
		const QStyleOptionViewItem &option, const QModelIndex &index) override;

	bool helpEvent(
		QHelpEvent *event, QAbstractItemView *view,
		const QStyleOptionViewItem &option, const QModelIndex &index) override;

	QWidget *createEditor(
		QWidget *parent, const QStyleOptionViewItem &option,
		const QModelIndex &index) const override;
	void setEditorData(QWidget *editor, const QModelIndex &index) const override;
	void setModelData(
		QWidget *editor, QAbstractItemModel *model,
		const QModelIndex &index) const override;
	void updateEditorGeometry(
		QWidget *editor, const QStyleOptionViewItem &option,
		const QModelIndex &index) const override;

private:
	struct Thumb {
		// Holding a reference keeps the pointer from being reused by a
		// different layer's content while it's used for change detection.
		std::shared_ptr<void> keepAlive;
		const void *content = nullptr;
		QSize requested;
		QImage image;
		qint64 renderedAt = -1;
	};

	QStyleOptionViewItem innerOption(const QStyleOptionViewItem &option) const;
	QRect thumbRect(const QRect &rowRect) const;
	QImage thumbnailFor(int layerId, const QSize &size) const;
	void scheduleRefresh() const;

	QPointer<QAbstractItemDelegate> m_inner;
	QPointer<MainWindow> m_mw;
	QPointer<QAbstractItemView> m_view;
	mutable QHash<int, Thumb> m_thumbs;
	mutable QElapsedTimer m_clock;
	QTimer *m_refreshTimer;
	QTimer *m_pollTimer;
};

}

#endif
