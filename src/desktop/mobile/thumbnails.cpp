// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpengine/canvas_state.h>
#include <dpengine/image.h>
}
#include "desktop/mobile/thumbnails.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/drawdance/canvasstate.h"
#include "libclient/drawdance/documentmetadata.h"
#include "libclient/drawdance/image.h"
#include "libclient/drawdance/timeline.h"
#include "libclient/drawdance/track.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtEndian>
#include <sqlite3.h>
#include <zlib.h>

namespace mobile {
namespace thumbnails {

namespace {

constexpr int THUMB_SIZE = 384;

QString cacheDir()
{
	QString dir = QStringLiteral("%1/mobileui/thumbnails")
					  .arg(QStandardPaths::writableLocation(
						  QStandardPaths::AppDataLocation));
	QDir().mkpath(dir);
	return dir;
}

QString cacheKey(const QString &path)
{
	return QString::fromLatin1(
		QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1)
			.toHex());
}

QString cacheImagePath(const QString &path)
{
	return QStringLiteral("%1/%2.png").arg(cacheDir(), cacheKey(path));
}

QString cacheMetaPath(const QString &path)
{
	return QStringLiteral("%1/%2.json").arg(cacheDir(), cacheKey(path));
}

QImage scaled(const QImage &img)
{
	if(img.isNull() || (img.width() <= THUMB_SIZE && img.height() <= THUMB_SIZE)) {
		return img;
	}
	return img.scaled(
		THUMB_SIZE, THUMB_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

ProjectMeta readCachedMeta(const QString &path)
{
	ProjectMeta meta;
	QFile file(cacheMetaPath(path));
	if(file.open(QIODevice::ReadOnly)) {
		QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
		meta.valid = true;
		meta.size = QSize(o.value(QStringLiteral("w")).toInt(),
						  o.value(QStringLiteral("h")).toInt());
		meta.animated = o.value(QStringLiteral("animated")).toBool();
		meta.keyFrames = o.value(QStringLiteral("keyframes")).toInt();
		meta.framerate = o.value(QStringLiteral("fps")).toDouble();
		meta.updated = QDateTime::fromString(
			o.value(QStringLiteral("updated")).toString(), Qt::ISODate);
	}
	return meta;
}

void writeCachedMeta(const QString &path, const ProjectMeta &meta)
{
	QJsonObject o;
	o.insert(QStringLiteral("path"), path);
	o.insert(QStringLiteral("w"), meta.size.width());
	o.insert(QStringLiteral("h"), meta.size.height());
	o.insert(QStringLiteral("animated"), meta.animated);
	o.insert(QStringLiteral("keyframes"), meta.keyFrames);
	o.insert(QStringLiteral("fps"), meta.framerate);
	o.insert(
		QStringLiteral("updated"),
		QDateTime::currentDateTime().toString(Qt::ISODate));
	QFile file(cacheMetaPath(path));
	if(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
	}
}

bool isPlainImage(const QString &suffix)
{
	static const QStringList suffixes = {
		QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
		QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
	};
	return suffixes.contains(suffix);
}

QString suffixOf(const QString &path)
{
	// Content URIs on Android don't necessarily end in an extension, but
	// usually do after decoding.
	QString decoded = QUrl::fromPercentEncoding(path.toUtf8());
	int slash = decoded.lastIndexOf(QLatin1Char('/'));
	int dot = decoded.lastIndexOf(QLatin1Char('.'));
	return dot > slash ? decoded.mid(dot + 1).toLower() : QString();
}

// Inflates a raw deflate stream as found in ZIP files.
QByteArray inflateRaw(const QByteArray &input, qint64 expectedSize)
{
	if(expectedSize <= 0 || expectedSize > 64 * 1024 * 1024) {
		return QByteArray();
	}
	QByteArray output;
	output.resize(int(expectedSize));
	z_stream stream = {};
	stream.next_in =
		reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
	stream.avail_in = uInt(input.size());
	stream.next_out = reinterpret_cast<Bytef *>(output.data());
	stream.avail_out = uInt(output.size());
	if(inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
		return QByteArray();
	}
	int result = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	if(result != Z_STREAM_END) {
		return QByteArray();
	}
	output.resize(int(stream.total_out));
	return output;
}

quint16 le16(const char *p)
{
	return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

quint32 le32(const char *p)
{
	return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

// Reads one file out of a ZIP archive via its central directory. Only what's
// needed for OpenRaster thumbnails: stored or deflated entries, no ZIP64.
QByteArray readZipEntry(const QString &path, const QString &entryName)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	qint64 fileSize = file.size();
	if(fileSize < 22) {
		return QByteArray();
	}
	qint64 tailSize = qMin<qint64>(fileSize, 65557);
	file.seek(fileSize - tailSize);
	QByteArray tail = file.read(tailSize);
	int eocd = -1;
	for(int i = tail.size() - 22; i >= 0; --i) {
		if(le32(tail.constData() + i) == 0x06054b50u) {
			eocd = i;
			break;
		}
	}
	if(eocd < 0) {
		return QByteArray();
	}
	quint16 entries = le16(tail.constData() + eocd + 10);
	quint32 cdSize = le32(tail.constData() + eocd + 12);
	quint32 cdOffset = le32(tail.constData() + eocd + 16);
	if(qint64(cdOffset) + cdSize > fileSize) {
		return QByteArray();
	}
	file.seek(cdOffset);
	QByteArray cd = file.read(cdSize);
	int pos = 0;
	QByteArray wanted = entryName.toUtf8();
	for(int i = 0; i < entries && pos + 46 <= cd.size(); ++i) {
		const char *h = cd.constData() + pos;
		if(le32(h) != 0x02014b50u) {
			break;
		}
		quint16 method = le16(h + 10);
		quint32 compressedSize = le32(h + 20);
		quint32 uncompressedSize = le32(h + 24);
		quint16 nameLen = le16(h + 28);
		quint16 extraLen = le16(h + 30);
		quint16 commentLen = le16(h + 32);
		quint32 localOffset = le32(h + 42);
		if(pos + 46 + nameLen > cd.size()) {
			break;
		}
		QByteArray name(h + 46, nameLen);
		if(name == wanted) {
			file.seek(localOffset);
			QByteArray local = file.read(30);
			if(local.size() < 30 || le32(local.constData()) != 0x04034b50u) {
				return QByteArray();
			}
			quint16 localNameLen = le16(local.constData() + 26);
			quint16 localExtraLen = le16(local.constData() + 28);
			file.seek(localOffset + 30 + localNameLen + localExtraLen);
			QByteArray data = file.read(compressedSize);
			if(method == 0) {
				return data;
			} else if(method == 8) {
				return inflateRaw(data, uncompressedSize);
			} else {
				return QByteArray();
			}
		}
		pos += 46 + nameLen + extraLen + commentLen;
	}
	return QByteArray();
}

}

QImage readOraThumbnail(const QString &path)
{
	QByteArray data =
		readZipEntry(path, QStringLiteral("Thumbnails/thumbnail.png"));
	QImage img;
	if(!data.isEmpty()) {
		img.loadFromData(data, "PNG");
	}
	return img;
}

QImage readProjectThumbnail(const QString &path)
{
	// SQLite can only open real files, not Android content URIs.
	if(path.startsWith(QStringLiteral("content:")) || !QFileInfo::exists(path)) {
		return QImage();
	}
	QImage img;
	sqlite3 *db = nullptr;
	if(sqlite3_open_v2(
		   path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) ==
	   SQLITE_OK) {
		sqlite3_stmt *stmt = nullptr;
		if(sqlite3_prepare_v2(
			   db,
			   "select thumbnail from sessions where thumbnail is not null "
			   "order by session_id desc limit 1",
			   -1, &stmt, nullptr) == SQLITE_OK) {
			if(sqlite3_step(stmt) == SQLITE_ROW) {
				const void *blob = sqlite3_column_blob(stmt, 0);
				int size = sqlite3_column_bytes(stmt, 0);
				if(blob && size > 0) {
					img.loadFromData(static_cast<const uchar *>(blob), size);
				}
			}
			sqlite3_finalize(stmt);
		}
	}
	sqlite3_close(db);
	return img;
}

QImage load(const QString &path, ProjectMeta *outMeta)
{
	ProjectMeta meta = readCachedMeta(path);
	QString suffix = suffixOf(path);
	QImage img;

	if(isPlainImage(suffix)) {
		QImageReader reader(path);
		reader.setAutoTransform(true);
		QSize size = reader.size();
		if(size.isValid()) {
			if(!meta.valid) {
				meta.size = size;
				meta.valid = true;
			}
			QSize target = size.scaled(
				THUMB_SIZE, THUMB_SIZE, Qt::KeepAspectRatio);
			if(target.width() < size.width()) {
				reader.setScaledSize(target);
			}
		}
		img = reader.read();
	}

	// A cached thumbnail from the last save is the most accurate for the
	// remaining formats, so prefer it over embedded ones.
	if(img.isNull()) {
		img.load(cacheImagePath(path), "PNG");
	}
	if(img.isNull() && suffix == QStringLiteral("ora")) {
		img = scaled(readOraThumbnail(path));
	}
	if(img.isNull() && suffix == QStringLiteral("dppr")) {
		img = scaled(readProjectThumbnail(path));
	}

	if(outMeta) {
		*outMeta = meta;
	}
	return img;
}

QImage renderCanvas(canvas::CanvasModel *canvas, ProjectMeta *outMeta)
{
	if(!canvas) {
		return QImage();
	}
	// The history state is updated as soon as messages arrive, the view
	// state only after the next render tick, so prefer the former.
	drawdance::CanvasState cs = canvas->paintEngine()->historyCanvasState();
	if(cs.isNull() || cs.width() <= 0) {
		cs = canvas->paintEngine()->viewCanvasState();
	}
	if(cs.isNull() || cs.width() <= 0 || cs.height() <= 0) {
		return QImage();
	}
	if(outMeta) {
		ProjectMeta meta;
		meta.valid = true;
		meta.size = QSize(cs.width(), cs.height());
		drawdance::Timeline timeline = cs.timeline();
		int keyFrames = 0;
		for(int i = 0, count = timeline.trackCount(); i < count; ++i) {
			keyFrames += timeline.trackAt(i).keyFrameCount();
		}
		meta.keyFrames = keyFrames;
		meta.animated = keyFrames > 1;
		meta.framerate = cs.documentMetadata().effectiveFramerate();
		*outMeta = meta;
	}
	DP_Image *thumb =
		DP_image_thumbnail_from_canvas(cs.get(), nullptr, THUMB_SIZE, THUMB_SIZE);
	return drawdance::wrapImage(thumb).copy();
}

void storeFromCanvas(const QString &path, canvas::CanvasModel *canvas)
{
	if(path.isEmpty() || !canvas) {
		return;
	}
	ProjectMeta meta;
	QImage img = renderCanvas(canvas, &meta);
	if(!img.isNull()) {
		img.save(cacheImagePath(path), "PNG");
		writeCachedMeta(path, meta);
	}
}

void move(const QString &fromPath, const QString &toPath)
{
	QFile::remove(cacheImagePath(toPath));
	QFile::remove(cacheMetaPath(toPath));
	QFile::rename(cacheImagePath(fromPath), cacheImagePath(toPath));
	QFile::rename(cacheMetaPath(fromPath), cacheMetaPath(toPath));
}

void copy(const QString &fromPath, const QString &toPath)
{
	QFile::remove(cacheImagePath(toPath));
	QFile::remove(cacheMetaPath(toPath));
	QFile::copy(cacheImagePath(fromPath), cacheImagePath(toPath));
	QFile::copy(cacheMetaPath(fromPath), cacheMetaPath(toPath));
}

void remove(const QString &path)
{
	QFile::remove(cacheImagePath(path));
	QFile::remove(cacheMetaPath(path));
}

}
}
