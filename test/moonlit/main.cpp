#include "MoonLitTest.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace MoonLitTest {

std::vector<std::pair<const char *, TestBody>> &registry()
{
	static std::vector<std::pair<const char *, TestBody>> tests;
	return tests;
}

bool registerTest(const char *name, TestBody body)
{
	registry().emplace_back(name, std::move(body));
	return true;
}

} // namespace MoonLitTest

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int failures = 0;
	for (const auto &[name, body] : MoonLitTest::registry()) {
		QString failure;
		if (body(&failure)) {
			std::fprintf(stdout, "PASS %s\n", name);
		} else {
			std::fprintf(stdout, "FAIL %s: %s\n", name, failure.toUtf8().constData());
			++failures;
		}
	}

	std::fprintf(stdout, "%zu test(s), %d failure(s)\n", MoonLitTest::registry().size(), failures);
	std::fflush(stdout);
	return failures == 0 ? 0 : 1;
}
