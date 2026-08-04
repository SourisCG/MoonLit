#include "MoonLitTest.hpp"

#include <moonlit/MoonLitPaths.hpp>
#include <moonlit/persistence/SqliteClipRepository.hpp>
#include <moonlit/services/ExportQueue.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>

#include <chrono>
#include <thread>

using namespace MoonLit;
using namespace MoonLitTest;

MOONLIT_TEST(export_queue_shutdown_completes)
{
	QTemporaryDir directory;
	MoonLitPaths paths(directory.path());
	QString error;
	SqliteClipRepository repository(paths);
	if (!repository.open(&error)) {
		*failure = QStringLiteral("open failed: %1").arg(error);
		return false;
	}

	ExportQueue queue(&repository);
	queue.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	QElapsedTimer timer;
	timer.start();
	queue.shutdown();
	const qint64 elapsed = timer.elapsed();

	bool ok = expect(elapsed < 3000, "shutdown completes within 3 seconds", failure);
	if (elapsed >= 3000) {
		*failure = QStringLiteral("shutdown took %1 ms").arg(elapsed);
	}
	return ok;
}
