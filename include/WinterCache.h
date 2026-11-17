#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

#include <SimpleIni.h>
#undef ERROR

namespace WinterCache
{
	inline constexpr std::int64_t kNotGenerated = -1;
	inline constexpr std::int64_t kDisabled = -2;
	using CategoryCounts = std::array<std::int64_t, 6>;

	enum class LoadResult
	{
		kMiss,
		kInvalid,
		kLoaded
	};

	LoadResult Load(const std::filesystem::path& a_path, std::size_t a_fingerprint, CSimpleIniA& a_ini, CategoryCounts& a_counts);
	bool Save(const std::filesystem::path& a_path, std::size_t a_fingerprint, const CSimpleIniA& a_ini, const CategoryCounts& a_counts);
}
