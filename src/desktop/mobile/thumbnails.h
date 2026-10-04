// SPDX-License-Identifier: GPL-3.0-or-later
// Drawpile Mobile (fork): thumbnails and metadata for the project hub.
#ifndef DESKTOP_MOBILE_THUMBNAILS_H
#define DESKTOP_MOBILE_THUMBNAILS_H
#include <QDateTime>
#include <QImage>
#include <QSize>
#include <QString>

namespace canvas {
class CanvasModel;
}

namespace mobile {

struct ProjectMeta {
	bool valid = false;
	QSize size;
	bool animated = false;
	int keyFrames = 0;
	double framerate = 0.0;
	QDateTime updated;
};

// Thumbnails are read from the files themselves where the format has one
// (images, OpenRaster's embedded thumbnail, Drawpile project session
// thumbnails) and otherwise taken from a small cache that gets filled
// whenever a file is saved or opened in Drawpile. The cache lives in the
// app's data directory and never touches the user's files.
namespace thumbnails {

QImage load(const QString &path, ProjectMeta *outMeta);
void storeFromCanvas(const QString &path, canvas::CanvasModel *canvas);
QImage renderCanvas(canvas::CanvasModel *canvas, ProjectMeta *outMeta);
void move(const QString &fromPath, const QString &toPath);
void copy(const QString &fromPath, const QString &toPath);
void remove(const QString &path);

// Exposed for testing.
QImage readOraThumbnail(const QString &path);
QImage readProjectThumbnail(const QString &path);

}

}

#endif
