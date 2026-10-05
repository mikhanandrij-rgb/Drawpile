// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/mobile/layercards.h"
#include "desktop/mainwindow.h"
#include "desktop/mobile/theme.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/layerlist.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/document.h"
#include "libclient/drawdance/canvasstate.h"
#include "libclient/drawdance/layercontent.h"
#include "libclient/drawdance/layergroup.h"
#include "libclient/drawdance/layerprops.h"
#include <QAbstractItemView>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <variant>

namespace mobile {

namespace {
// Upstream's glyph size (LayerListDelegate::GLYPH_SIZE), in logical pixels.
constexpr int UPSTREAM_GLYPH_SIZE = 24;
// Don't re-render a layer's thumbnail more often than this while drawing.
constexpr qint64 THUMB_MIN_INTERVAL_MS = 1200;
constexpr char PROPERTY[] = "mobileLayerCardDelegate";
}

LayerCardDelegate::LayerCardDelegate(
	QAbstractItemDelegate *inner, MainWindow *mw, QAbstractItemView *view)
	: QAbstractItemDelegate(view)
	, m_inner(inner)
	, m_mw(mw)
	, m_view(view)
	, m_refreshTimer(new QTimer(this))
	, m_pollTimer(new QTimer(this))
{
	m_clock.start();
	m_refreshTimer->setSingleShot(true);
	connect(m_refreshTimer, &QTimer::timeout, this, [this] {
		if(m_view && m_view->isVisible()) {
			m_view->viewport()->update();
		}
	});
	// Pixel changes don't touch the layer model, so look for changed layer
	// contents periodically while the list is on screen. Unchanged layers are
	// detected by pointer comparison, which is cheap.
	m_pollTimer->setInterval(1500);
	connect(m_pollTimer, &QTimer::timeout, this, [this] {
		if(m_view && m_view->isVisible()) {
			m_view->viewport()->update();
		}
	});
	m_pollTimer->start();
	if(inner) {
		connect(
			inner, &QAbstractItemDelegate::sizeHintChanged, this,
			&QAbstractItemDelegate::sizeHintChanged);
		connect(
			inner, &QAbstractItemDelegate::commitData, this,
			&QAbstractItemDelegate::commitData);
		connect(
			inner, &QAbstractItemDelegate::closeEditor, this,
			&QAbstractItemDelegate::closeEditor);
	}
}

void LayerCardDelegate::install(MainWindow *mw, QAbstractItemView *view)
{
	if(!view || view->property(PROPERTY).value<QObject *>()) {
		return;
	}
	QAbstractItemDelegate *inner = view->itemDelegate();
	LayerCardDelegate *cards = new LayerCardDelegate(inner, mw, view);
	view->setProperty(PROPERTY, QVariant::fromValue<QObject *>(cards));
	view->setItemDelegate(cards);
	view->doItemsLayout();
}

void LayerCardDelegate::uninstall(QAbstractItemView *view)
{
	if(!view) {
		return;
	}
	LayerCardDelegate *cards = qobject_cast<LayerCardDelegate *>(
		view->property(PROPERTY).value<QObject *>());
	view->setProperty(PROPERTY, QVariant());
	if(cards) {
		if(cards->m_inner) {
			view->setItemDelegate(cards->m_inner);
		}
		cards->deleteLater();
		view->doItemsLayout();
	}
}

QRect LayerCardDelegate::thumbRect(const QRect &rowRect) const
{
	int pad = dp(5);
	int h = rowRect.height() - pad * 2;
	int w = h * 4 / 3;
	return QRect(rowRect.left() + dp(4), rowRect.top() + pad, w, h);
}

QStyleOptionViewItem
LayerCardDelegate::innerOption(const QStyleOptionViewItem &option) const
{
	QStyleOptionViewItem opt = option;
	QRect thumb = thumbRect(option.rect);
	opt.rect.setLeft(thumb.right() + dp(6));
	return opt;
}

void LayerCardDelegate::paint(
	QPainter *painter, const QStyleOptionViewItem &option,
	const QModelIndex &index) const
{
	const Theme &t = Theme::current();
	QStyleOptionViewItem opt = innerOption(option);

	// Selection/background behind the thumbnail matches the row.
	bool selected = option.state.testFlag(QStyle::State_Selected);
	if(selected) {
		QPalette::ColorGroup cg = option.state.testFlag(QStyle::State_Enabled)
									  ? QPalette::Normal
									  : QPalette::Disabled;
		QRect left = option.rect;
		left.setRight(opt.rect.left() - 1);
		painter->fillRect(left, option.palette.brush(cg, QPalette::Highlight));
	}

	if(m_inner) {
		m_inner->paint(painter, opt, index);
	}

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	QRect thumb = thumbRect(option.rect);
	QPainterPath clip;
	clip.addRoundedRect(QRectF(thumb), dp(6), dp(6));
	painter->setClipPath(clip);
	// Checkerboard for transparency.
	int cell = qMax(4, dp(6));
	painter->fillRect(thumb, QColor(0xcc, 0xcc, 0xcc));
	for(int y = 0; y * cell < thumb.height(); ++y) {
		for(int x = (y % 2); x * cell < thumb.width(); x += 2) {
			painter->fillRect(
				QRect(thumb.left() + x * cell, thumb.top() + y * cell, cell, cell),
				QColor(0xf4, 0xf4, 0xf4));
		}
	}
	int layerId = index.data(canvas::LayerListModel::IdRole).toInt();
	qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
	QImage img = thumbnailFor(
		layerId, QSize(qRound(thumb.width() * dpr), qRound(thumb.height() * dpr)));
	if(!img.isNull()) {
		// Hidden layers get a faded thumbnail, like upstream fades the name.
		if(index.data(canvas::LayerListModel::IsHiddenInTreeRole).toBool()) {
			painter->setOpacity(0.35);
		}
		QSizeF size = QSizeF(img.size()) / dpr;
		QRectF target(
			thumb.left() + (thumb.width() - size.width()) / 2.0,
			thumb.top() + (thumb.height() - size.height()) / 2.0, size.width(),
			size.height());
		painter->drawImage(target, img);
	}
	painter->setOpacity(1.0);
	painter->setClipping(false);
	painter->setPen(QPen(selected ? t.accent : t.outline, 1.0));
	painter->setBrush(Qt::NoBrush);
	painter->drawRoundedRect(QRectF(thumb).adjusted(0.5, 0.5, -0.5, -0.5), dp(6), dp(6));
	// Thin separator between cards.
	painter->setPen(QPen(t.outline, 1.0));
	painter->drawLine(
		QPointF(option.rect.left() + dp(4), option.rect.bottom() + 0.5),
		QPointF(option.rect.right() - dp(4), option.rect.bottom() + 0.5));
	painter->restore();
}

QSize LayerCardDelegate::sizeHint(
	const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	QSize size = m_inner ? m_inner->sizeHint(option, index) : QSize();
	int h = qMax(size.height(), dp(56));
	QRect thumb = thumbRect(QRect(0, 0, 100, h));
	return QSize(size.width() + thumb.width() + dp(10), h);
}

bool LayerCardDelegate::editorEvent(
	QEvent *event, QAbstractItemModel *model,
	const QStyleOptionViewItem &option, const QModelIndex &index)
{
	if(!m_inner) {
		return false;
	}
	QStyleOptionViewItem opt = innerOption(option);
	QEvent::Type type = event->type();
	if(type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease ||
	   type == QEvent::MouseButtonDblClick || type == QEvent::MouseMove) {
		QMouseEvent *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		QPointF pos = me->position();
		QPointF globalPos = me->globalPosition();
#else
		QPointF pos = me->localPos();
		QPointF globalPos = me->screenPos();
#endif
		// Snap taps near the small glyphs onto them, giving finger-sized hit
		// areas without changing upstream's delegate.
		int zone = dp(46);
		int centerY = opt.rect.center().y();
		QPointF mapped = pos;
		if(pos.x() >= opt.rect.left() && pos.x() < opt.rect.left() + zone) {
			mapped = QPointF(opt.rect.left() + UPSTREAM_GLYPH_SIZE / 2.0, centerY);
		} else if(
			pos.x() > opt.rect.right() - zone && pos.x() <= opt.rect.right() &&
			index.data(canvas::LayerListModel::CheckModeRole).toBool()) {
			mapped = QPointF(opt.rect.right() - UPSTREAM_GLYPH_SIZE / 2.0, centerY);
		}
		if(type == QEvent::MouseMove) {
			// Drag-toggling across rows uses the row, not the exact position.
			mapped = pos;
		}
		if(mapped != pos) {
			QMouseEvent copy(
				type, mapped, globalPos + (mapped - pos), me->button(),
				me->buttons(), me->modifiers());
			bool accepted = m_inner->editorEvent(&copy, model, opt, index);
			event->setAccepted(copy.isAccepted());
			return accepted;
		}
	}
	return m_inner->editorEvent(event, model, opt, index);
}

bool LayerCardDelegate::helpEvent(
	QHelpEvent *event, QAbstractItemView *view,
	const QStyleOptionViewItem &option, const QModelIndex &index)
{
	return m_inner && m_inner->helpEvent(event, view, innerOption(option), index);
}

QWidget *LayerCardDelegate::createEditor(
	QWidget *parent, const QStyleOptionViewItem &option,
	const QModelIndex &index) const
{
	return m_inner ? m_inner->createEditor(parent, innerOption(option), index)
				   : nullptr;
}

void LayerCardDelegate::setEditorData(
	QWidget *editor, const QModelIndex &index) const
{
	if(m_inner) {
		m_inner->setEditorData(editor, index);
	}
}

void LayerCardDelegate::setModelData(
	QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	if(m_inner) {
		m_inner->setModelData(editor, model, index);
	}
}

void LayerCardDelegate::updateEditorGeometry(
	QWidget *editor, const QStyleOptionViewItem &option,
	const QModelIndex &index) const
{
	if(m_inner) {
		m_inner->updateEditorGeometry(editor, innerOption(option), index);
	}
}

void LayerCardDelegate::scheduleRefresh() const
{
	if(!m_refreshTimer->isActive()) {
		m_refreshTimer->start(int(THUMB_MIN_INTERVAL_MS));
	}
}

QImage LayerCardDelegate::thumbnailFor(int layerId, const QSize &size) const
{
	if(layerId <= 0 || !m_mw || size.isEmpty()) {
		return QImage();
	}
	Document *doc = m_mw->findChild<Document *>();
	canvas::CanvasModel *canvas = doc ? doc->canvas() : nullptr;
	if(!canvas) {
		m_thumbs.clear();
		return QImage();
	}
	drawdance::CanvasState cs = canvas->paintEngine()->viewCanvasState();
	if(cs.isNull()) {
		return QImage();
	}
	drawdance::LayerSearchResult result = cs.searchLayer(layerId, false);
	const void *content = nullptr;
	if(const drawdance::LayerContent *lc =
		   std::get_if<drawdance::LayerContent>(&result.data)) {
		content = lc->get();
	} else if(
		const drawdance::LayerGroup *lg =
			std::get_if<drawdance::LayerGroup>(&result.data)) {
		content = lg->get();
	}
	if(!content) {
		m_thumbs.remove(layerId);
		return QImage();
	}

	Thumb &thumb = m_thumbs[layerId];
	qint64 now = m_clock.elapsed();
	bool stale = thumb.content != content || thumb.requested != size;
	if(!stale) {
		return thumb.image;
	}
	if(thumb.requested == size && thumb.renderedAt >= 0 &&
	   now - thumb.renderedAt < THUMB_MIN_INTERVAL_MS) {
		// Changed recently: keep showing the old one, update a bit later.
		scheduleRefresh();
		return thumb.image;
	}

	QRect rect(0, 0, cs.width(), cs.height());
	QImage full;
	if(const drawdance::LayerContent *lc =
		   std::get_if<drawdance::LayerContent>(&result.data)) {
		full = lc->toImage(rect);
	} else if(
		const drawdance::LayerGroup *lg =
			std::get_if<drawdance::LayerGroup>(&result.data)) {
		full = lg->toImage(result.props, rect);
	}
	thumb.keepAlive = std::make_shared<drawdance::LayerSearchResult>(result);
	thumb.requested = size;
	if(full.isNull()) {
		thumb.content = content;
		thumb.image = QImage();
		thumb.renderedAt = now;
		return QImage();
	}
	// Two-step scaling: a fast reduction followed by a smooth one keeps
	// large canvases (e.g. A4 at 300 dpi) cheap without looking blocky.
	QSize target = full.size().scaled(size, Qt::KeepAspectRatio);
	if(full.width() > target.width() * 4) {
		full = full.scaled(
			target * 4, Qt::KeepAspectRatio, Qt::FastTransformation);
	}
	thumb.image =
		full.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	thumb.content = content;
	thumb.renderedAt = now;
	return thumb.image;
}

}
