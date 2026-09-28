// Unit tests for the pure logic in ParsingUtils.h - the same header MainFrame.cpp
// uses, so a passing test here is a guarantee about what actually ships, not
// about a separate copy.
//
// Uses Catch2 v3 (installed via vcpkg). See SETUP.md for how to wire this into
// a second project inside the same Visual Studio solution.

#include <catch2/catch_test_macros.hpp>

#include "ParsingUtils.h"

using namespace clu2;

// --- Trim --------------------------------------------------------------

TEST_CASE("Trim removes leading and trailing whitespace", "[Trim]")
{
	CHECK(Trim(L"  hello  ") == L"hello");
	CHECK(Trim(L"hello") == L"hello");
	CHECK(Trim(L"\t\nhello\r\n") == L"hello");
}

TEST_CASE("Trim does not touch whitespace in the middle", "[Trim]")
{
	CHECK(Trim(L"  hello world  ") == L"hello world");
}

TEST_CASE("Trim of an all-whitespace or empty string is empty", "[Trim]")
{
	CHECK(Trim(L"").empty());
	CHECK(Trim(L"   ").empty());
	CHECK(Trim(L"\t\r\n").empty());
}

// --- GetRoots ------------------------------------------------------------

TEST_CASE("GetRoots splits on newlines", "[GetRoots]")
{
	auto const roots = GetRoots(L"C:\\a\nC:\\b\nC:\\c");

	REQUIRE(roots.size() == 3);
	CHECK(roots[0] == std::filesystem::path(L"C:\\a"));
	CHECK(roots[1] == std::filesystem::path(L"C:\\b"));
	CHECK(roots[2] == std::filesystem::path(L"C:\\c"));
}

TEST_CASE("GetRoots splits on semicolons", "[GetRoots]")
{
	auto const roots = GetRoots(L"C:\\a;C:\\b;C:\\c");

	REQUIRE(roots.size() == 3);
	CHECK(roots[0] == std::filesystem::path(L"C:\\a"));
	CHECK(roots[2] == std::filesystem::path(L"C:\\c"));
}

TEST_CASE("GetRoots accepts a mix of newlines and semicolons", "[GetRoots]")
{
	auto const roots = GetRoots(L"C:\\a;C:\\b\nC:\\c");

	REQUIRE(roots.size() == 3);
}

TEST_CASE("GetRoots trims whitespace around each entry", "[GetRoots]")
{
	auto const roots = GetRoots(L"  C:\\a  ; \tC:\\b\t \n  C:\\c  ");

	REQUIRE(roots.size() == 3);
	CHECK(roots[0] == std::filesystem::path(L"C:\\a"));
	CHECK(roots[1] == std::filesystem::path(L"C:\\b"));
}

TEST_CASE("GetRoots skips empty entries", "[GetRoots]")
{
	auto const roots = GetRoots(L"C:\\a\n\n;;C:\\b\n   \n");

	REQUIRE(roots.size() == 2);
}

TEST_CASE("GetRoots on blank input returns nothing", "[GetRoots]")
{
	CHECK(GetRoots(L"").empty());
	CHECK(GetRoots(L"   \n  \n").empty());
}

// --- IsImageFile -----------------------------------------------------------

TEST_CASE("IsImageFile accepts every supported extension", "[IsImageFile]")
{
	CHECK(IsImageFile(L"photo.bmp"));
	CHECK(IsImageFile(L"photo.gif"));
	CHECK(IsImageFile(L"photo.jpeg"));
	CHECK(IsImageFile(L"photo.jpg"));
	CHECK(IsImageFile(L"photo.png"));
	CHECK(IsImageFile(L"photo.tif"));
	CHECK(IsImageFile(L"photo.tiff"));
	CHECK(IsImageFile(L"photo.webp"));
}

TEST_CASE("IsImageFile is case-insensitive", "[IsImageFile]")
{
	CHECK(IsImageFile(L"PHOTO.JPG"));
	CHECK(IsImageFile(L"Photo.PnG"));
}

TEST_CASE("IsImageFile rejects non-image extensions", "[IsImageFile]")
{
	CHECK_FALSE(IsImageFile(L"notes.txt"));
	CHECK_FALSE(IsImageFile(L"data.csv"));
	CHECK_FALSE(IsImageFile(L"archive.zip"));
}

TEST_CASE("IsImageFile rejects a file with no extension", "[IsImageFile]")
{
	CHECK_FALSE(IsImageFile(L"README"));
}

// --- TryExtractDdd -----------------------------------------------------------
//
// The default pattern used throughout the app (matches the original hardcoded
// rule: first '_', then somewhere later "____", id = before, status = after).
namespace
{
	std::wregex const kDefaultIdPattern(LR"(^([^_]+)_+.*?____(.*)$)");
}

TEST_CASE("TryExtractDdd matches a well-formed DMC file name", "[TryExtractDdd]")
{
	std::wregex const suffixPattern(L"NOK");
	std::wstring ddd;

	REQUIRE(TryExtractDdd(L"D000001_cam0____NOK.jpg", kDefaultIdPattern, suffixPattern, ddd));
	CHECK(ddd == L"D000001");
}

TEST_CASE("TryExtractDdd rejects a status that doesn't match the suffix pattern", "[TryExtractDdd]")
{
	std::wregex const suffixPattern(L"^OK$"); // exact match only
	std::wstring ddd;

	// "NOK" contains "OK" as a substring, but ^OK$ requires an exact match.
	CHECK_FALSE(TryExtractDdd(L"D000001_cam0____NOK.jpg", kDefaultIdPattern, suffixPattern, ddd));
}

TEST_CASE("A substring suffix pattern matches a superstring status", "[TryExtractDdd]")
{
	std::wregex const suffixPattern(L"OK");
	std::wstring ddd;

	// This is the documented, slightly surprising behavior: "OK" also matches "NOK".
	CHECK(TryExtractDdd(L"D000001_cam0____NOK.jpg", kDefaultIdPattern, suffixPattern, ddd));
}

TEST_CASE("TryExtractDdd rejects a name with no underscore", "[TryExtractDdd]")
{
	std::wregex const suffixPattern(L"NOK");
	std::wstring ddd;

	CHECK_FALSE(TryExtractDdd(L"weirdname.jpg", kDefaultIdPattern, suffixPattern, ddd));
}

TEST_CASE("TryExtractDdd rejects a name missing the ____ separator", "[TryExtractDdd]")
{
	std::wregex const suffixPattern(L"NOK");
	std::wstring ddd;

	CHECK_FALSE(TryExtractDdd(L"D000001_cam0_NOK.jpg", kDefaultIdPattern, suffixPattern, ddd));
}

TEST_CASE("TryExtractDdd works with a custom pattern for a different naming scheme", "[TryExtractDdd]")
{
	// Example of a differently-shaped name: "UNIT-42-status-FAIL.jpg"
	std::wregex const customIdPattern(LR"(^UNIT-(\d+)-status-(\w+)$)");
	std::wregex const suffixPattern(L"FAIL");
	std::wstring ddd;

	REQUIRE(TryExtractDdd(L"UNIT-42-status-FAIL.jpg", customIdPattern, suffixPattern, ddd));
	CHECK(ddd == L"42");
}