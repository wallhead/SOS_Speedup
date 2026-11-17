#include "WinterCache.h"

#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <locale>
#include <process.h>
#include <sstream>

#include <boost/container_hash/hash.hpp>

namespace
{
	constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";

	std::string CacheHeaderPrefix(std::size_t a_fingerprint)
	{
		return std::format("; Seasons of Skyrim winter cache v3 {:016x} ", a_fingerprint);
	}

	std::size_t CacheChecksum(std::string_view a_metadata, std::string_view a_body)
	{
		std::size_t checksum = 0;
		boost::hash_combine(checksum, a_metadata);
		boost::hash_combine(checksum, a_body);
		return checksum;
	}
}

WinterCache::LoadResult WinterCache::Load(const std::filesystem::path& a_path, std::size_t a_fingerprint, CSimpleIniA& a_ini, CategoryCounts& a_counts)
{
	std::ifstream file(a_path, std::ios::binary);
	std::string line;
	if (!std::getline(file, line)) {
		return LoadResult::kMiss;
	}

	std::string_view header = line;
	if (header.starts_with(utf8Bom)) {
		header.remove_prefix(utf8Bom.size());
	}
	if (header.ends_with('\r')) {
		header.remove_suffix(1);
	}
	const auto prefix = CacheHeaderPrefix(a_fingerprint);
	if (!header.starts_with(prefix)) {
		return LoadResult::kMiss;
	}

	const auto checksumSeparator = header.rfind(' ');
	if (checksumSeparator < prefix.size()) {
		return LoadResult::kInvalid;
	}
	const auto metadata = header.substr(0, checksumSeparator);
	const auto checksumText = header.substr(checksumSeparator + 1);
	std::size_t expectedChecksum = 0;
	const auto checksumEnd = checksumText.data() + checksumText.size();
	const auto parsed = std::from_chars(checksumText.data(), checksumEnd, expectedChecksum, 16);
	if (checksumText.size() != 16 || parsed.ec != std::errc{} || parsed.ptr != checksumEnd) {
		return LoadResult::kInvalid;
	}

	CategoryCounts counts{};
	std::istringstream fields{ std::string(metadata.substr(prefix.size())) };
	fields.imbue(std::locale::classic());
	for (auto& count : counts) {
		if (!(fields >> count) || count < kDisabled) {
			return LoadResult::kInvalid;
		}
	}
	fields >> std::ws;
	if (!fields.eof()) {
		return LoadResult::kInvalid;
	}

	const std::string body(std::istreambuf_iterator<char>{ file }, {});
	if (file.bad() || CacheChecksum(metadata, body) != expectedChecksum || a_ini.LoadData(body) < SI_OK) {
		return LoadResult::kInvalid;
	}

	a_counts = counts;
	return LoadResult::kLoaded;
}

bool WinterCache::Save(const std::filesystem::path& a_path, std::size_t a_fingerprint, const CSimpleIniA& a_ini, const CategoryCounts& a_counts)
{
	std::string body;
	if (a_ini.Save(body) < SI_OK) {
		return false;
	}

	auto metadata = CacheHeaderPrefix(a_fingerprint);
	for (const auto count : a_counts) {
		metadata.append(std::to_string(count)).push_back(' ');
	}
	metadata.pop_back();
	const auto header = std::format("{}{} {:016x}\r\n", utf8Bom, metadata, CacheChecksum(metadata, body));

	auto tempPath = a_path;
	tempPath += std::format(L".{}.tmp", _getpid());
	std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
	file.write(header.data(), static_cast<std::streamsize>(header.size()));
	file.write(body.data(), static_cast<std::streamsize>(body.size()));
	file.close();

	std::error_code error;
	if (file) {
		std::filesystem::rename(tempPath, a_path, error);
		if (!error) {
			return true;
		}
	}

	std::filesystem::remove(tempPath, error);
	return false;
}
