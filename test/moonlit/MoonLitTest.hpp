#pragma once

#include <QString>

#include <functional>

namespace MoonLitTest {

inline bool expect(bool condition, const char *what, QString *failure)
{
	if (!condition) {
		if (failure) {
			if (!failure->isEmpty()) {
				failure->append(QStringLiteral("; "));
			}
			failure->append(QString::fromLatin1(what));
		}
		return false;
	}
	return true;
}

using TestBody = std::function<bool(QString *failure)>;

bool registerTest(const char *name, TestBody body);

} // namespace MoonLitTest

#define MOONLIT_TEST(name)                                                                                     \
	static bool name(QString *failure);                                                                        \
	static const bool name##_registered = MoonLitTest::registerTest(#name, [](QString *failure) {               \
		return name(failure);                                                                                 \
	});                                                                                                        \
	static bool name(QString *failure)
