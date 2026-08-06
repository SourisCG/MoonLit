#pragma once

#include <QMetaType>
#include <QString>

/* Platform-neutral capture identity, Windows-flavored. `window` carries the
 * HWND; the remaining fields describe the process and its main window so a
 * target can be re-validated or re-created later. */
struct MoonLitTarget {
	quintptr window = 0;
	quint32 processId = 0;
	quint64 creationTime = 0;
	QString title;
	QString windowClass;
	QString executable;
	QString executablePath;

	bool isValid() const { return window != 0 && processId != 0 && creationTime != 0; }
};

Q_DECLARE_METATYPE(MoonLitTarget)
