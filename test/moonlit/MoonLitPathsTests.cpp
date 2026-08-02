#include "MoonLitTest.hpp"

#include <moonlit/MoonLitPaths.hpp>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace MoonLit;
using namespace MoonLitTest;

MOONLIT_TEST(moonlit_paths_compose_expected_directories)
{
	MoonLitPaths paths(QStringLiteral("C:/some/root"));
	bool ok = expect(paths.rootPath() == QStringLiteral("C:/some/root"), "root path is kept", failure);
	ok &= expect(paths.clipsPath() == QStringLiteral("C:/some/root/clips"), "clips path", failure);
	ok &= expect(paths.indexPath() == QStringLiteral("C:/some/root/clips/index.json"), "index path", failure);
	ok &= expect(paths.databasePath() == QStringLiteral("C:/some/root/MoonLit.db"), "database path", failure);
	ok &= expect(paths.thumbnailsPath() == QStringLiteral("C:/some/root/thumbnails"), "thumbnails path", failure);
	ok &= expect(paths.exportsPath() == QStringLiteral("C:/some/root/exports"), "exports path", failure);
	ok &= expect(paths.temporaryPath() == QStringLiteral("C:/some/root/tmp"), "temporary path", failure);
	return ok;
}

MOONLIT_TEST(moonlit_paths_sanitizes_ids_and_extensions)
{
	MoonLitPaths paths(QStringLiteral("C:/some/root"));

	bool ok = expect(paths.thumbnailPath(QStringLiteral("abc-123")) ==
			     QStringLiteral("C:/some/root/thumbnails/abc-123.png"),
		     "plain thumbnail id", failure);
	ok &= expect(paths.thumbnailPath(QStringLiteral("a b:c#d")) ==
			     QStringLiteral("C:/some/root/thumbnails/a_b_c_d.png"),
		     "thumbnail id sanitized", failure);
	ok &= expect(paths.exportPath(QStringLiteral("clip-id")) ==
			     QStringLiteral("C:/some/root/exports/clip-id.mkv"),
		     "default export extension is mkv", failure);
	ok &= expect(paths.exportPath(QStringLiteral("clip-id"), QStringLiteral("mp4")) ==
			     QStringLiteral("C:/some/root/exports/clip-id.mp4"),
		     "export extension is honored", failure);
	ok &= expect(paths.exportPath(QStringLiteral("clip id"), QStringLiteral("mov")) ==
			     QStringLiteral("C:/some/root/exports/clip_id.mov"),
		     "export id sanitized", failure);
	return ok;
}

MOONLIT_TEST(moonlit_paths_ensure_directories_creates_tree)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	bool ok = expect(paths.ensureDirectories(&error), "ensureDirectories succeeds", failure);
	ok &= expect(error.isEmpty(), "no error on success", failure);
	ok &= expect(QFileInfo::exists(paths.clipsPath()) && QDir(paths.clipsPath()).exists(), "clips dir exists",
		     failure);
	ok &= expect(QDir(paths.thumbnailsPath()).exists(), "thumbnails dir exists", failure);
	ok &= expect(QDir(paths.exportsPath()).exists(), "exports dir exists", failure);
	ok &= expect(QDir(paths.temporaryPath()).exists(), "temporary dir exists", failure);
	ok &= expect(paths.ensureDirectories(&error), "ensureDirectories is idempotent", failure);
	return ok;
}

MOONLIT_TEST(moonlit_paths_portable_detection)
{
	QTemporaryDir directory;
	const QString rootDir = directory.path();
	const QString appDir = QDir(rootDir).filePath(QStringLiteral("bin/64bit"));
	QDir().mkpath(appDir);

	bool ok = expect(MoonLitPaths::portableDataRoot(appDir).isEmpty(),
			 "no data root without the portable marker", failure);

	QFile marker(QDir(rootDir).filePath(QStringLiteral("portable_mode")));
	ok &= expect(marker.open(QIODevice::WriteOnly), "portable marker is created", failure);
	marker.close();

	const QString root = MoonLitPaths::portableDataRoot(appDir);
	ok &= expect(root == QDir(rootDir).filePath(QStringLiteral("MoonLitData")),
		     "portable data root sits at the app root", failure);
	return ok;
}
