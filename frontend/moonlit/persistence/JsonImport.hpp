#pragma once

#include <QString>

struct sqlite3;

namespace MoonLit {

/* One-time import of the legacy JSON clip index into the SQLite database.
 * Kept as its own unit so the format can be dropped once migration is done. */
class JsonImport {
public:
	static bool importClips(sqlite3 *db, const QString &indexPath, QString *error);
};

} // namespace MoonLit
