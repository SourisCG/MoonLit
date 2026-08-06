#include "MoonLitTest.hpp"

#include <moonlit/capture/GameListMatch.hpp>

using namespace MoonLit;
using namespace MoonLitTest;

MOONLIT_TEST(game_list_matches_exact_executable)
{
	const QStringList list = {QStringLiteral("D:\\Games\\Elden Ring\\eldenring.exe"),
				  QStringLiteral("C:\\Steam\\steamapps\\common\\Hades\\Hades.exe")};
	bool ok = expect(matchesGameListPath(QStringLiteral("d:\\games\\elden ring\\eldenring.exe"), list),
			 "case-insensitive exact match", failure);
	ok &= expect(matchesGameListPath(QStringLiteral("D:/Games/Elden Ring/eldenring.exe"), list),
		     "forward slashes are normalized", failure);
	ok &= expect(!matchesGameListPath(QStringLiteral("D:\\Games\\Other\\game.exe"), list),
		     "non-listed executable does not match", failure);
	return ok;
}

MOONLIT_TEST(game_list_matches_directory_entry)
{
	const QStringList list = {QStringLiteral("C:\\Epic Games\\")};
	bool ok = expect(matchesGameListPath(QStringLiteral("C:\\Epic Games\\SomeGame\\Game.exe"), list),
			 "directory entry matches descendants", failure);
	ok &= expect(!matchesGameListPath(QStringLiteral("C:\\Epic Games.exe\\not-a-dir\\g.exe"), list),
		     "trailing slash entry needs a real directory prefix", failure);
	return ok;
}

MOONLIT_TEST(game_list_rejects_empty_entries)
{
	bool ok = expect(!matchesGameListPath(QString(), {QStringLiteral("C:\\Games\\")}),
			 "empty path never matches", failure);
	ok &= expect(!matchesGameListPath(QStringLiteral("C:\\Games\\Game.exe"), {QString(), QStringLiteral("  ")}),
		     "empty list entries are ignored", failure);
	return ok;
}
