#include "WinterCache.h"

#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <process.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			throw std::runtime_error(a_message);
		}
	}

	struct TemporaryCache
	{
		std::filesystem::path path = std::filesystem::temp_directory_path() / std::format(L"sos-winter-cache-test-{}.ini", _getpid());

		~TemporaryCache()
		{
			std::error_code error;
			std::filesystem::remove(path, error);
		}
	};
}

int main()
{
	try {
		const TemporaryCache fixture;
		constexpr std::size_t fingerprint = 0x1234;
		constexpr WinterCache::CategoryCounts counts{ 2, 0, WinterCache::kDisabled, WinterCache::kNotGenerated, 0, 0 };
		constexpr auto first = "0x10~Example.esm|0x11~Example.esm";
		constexpr auto second = "0x12~Example.esm|0x13~Example.esm";
		CSimpleIniA original;
		original.SetUnicode();
		original.SetAllowKeyOnly();
		original.SetValue("LandTextures", first, "");
		original.SetValue("LandTextures", second, "");
		Require(WinterCache::Save(fixture.path, fingerprint, original, counts), "could not save cache");

		CSimpleIniA read;
		read.SetUnicode();
		read.SetAllowKeyOnly();
		WinterCache::CategoryCounts loaded{};
		Require(WinterCache::Load(fixture.path, fingerprint + 1, read, loaded) == WinterCache::LoadResult::kMiss, "stale fingerprint accepted");
		Require(WinterCache::Load(fixture.path, fingerprint, read, loaded) == WinterCache::LoadResult::kLoaded, "valid cache rejected");
		Require(loaded == counts, "populated, empty, disabled or ungenerated counts lost");

		CSimpleIniA::TNamesDepend keys;
		read.GetAllKeys("LandTextures", keys);
		Require(keys.size() == 2 && std::string_view(keys.front().pItem) == first && std::string_view(keys.back().pItem) == second, "swap pairs lost");

		std::ifstream input(fixture.path, std::ios::binary);
		const std::string saved(std::istreambuf_iterator<char>{ input }, {});
		input.close();
		const auto entry = saved.find(first);
		Require(entry != std::string::npos, "saved entry is missing");
		const auto entryEnd = saved.find('\n', entry);
		Require(entryEnd != std::string::npos, "saved entry has no line ending");

		const auto assertRejected = [&](const std::string& a_bytes, const char* a_message) {
			std::ofstream output(fixture.path, std::ios::binary | std::ios::trunc);
			output.write(a_bytes.data(), static_cast<std::streamsize>(a_bytes.size()));
			output.close();
			Require(static_cast<bool>(output), "could not write fixture");
			read.Reset();
			Require(WinterCache::Load(fixture.path, fingerprint, read, loaded) == WinterCache::LoadResult::kInvalid, a_message);
		};

		auto incomplete = saved;
		incomplete.erase(entry, entryEnd + 1 - entry);
		assertRejected(incomplete, "cache missing one complete mapping was accepted");
		auto edited = saved;
		edited[entry + 3] = '4';
		assertRejected(edited, "cache with an edited FormID was accepted");
		auto damagedCounts = saved;
		const auto fields = damagedCounts.find(" 2 0 -2 -1 ");
		Require(fields != std::string::npos, "cache metadata missing");
		damagedCounts[fields + 1] = '1';
		assertRejected(damagedCounts, "cache with edited category counts was accepted");

		Require(WinterCache::Save(fixture.path, fingerprint, original, counts), "could not atomically replace the cache");
		Require(WinterCache::Load(fixture.path, fingerprint, read, loaded) == WinterCache::LoadResult::kLoaded, "regenerated file rejected");
		std::cout << "Winter cache round-trip and integrity tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
