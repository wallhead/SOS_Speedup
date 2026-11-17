#include "FormSwapMap.h"

#include <charconv>

namespace
{
	RE::TESForm* ResolveCachedForm(std::string_view a_token)
	{
		const auto separator = a_token.find('~');
		if (!a_token.starts_with("0x") || separator == std::string_view::npos || separator <= 2 || separator + 1 == a_token.size()) {
			return nullptr;
		}

		RE::FormID localID = 0;
		const auto end = a_token.data() + separator;
		const auto parsed = std::from_chars(a_token.data() + 2, end, localID, 16);
		if (parsed.ec != std::errc{} || parsed.ptr != end || localID > 0xFFFFFF) {
			return nullptr;
		}

		// Generated entries already name the active plugin and its local ID.
		const auto form = RE::TESDataHandler::GetSingleton()->LookupForm(localID, a_token.substr(separator + 1));
		// CommonLib masks off high bits when it resolves a light plugin record.
		return form && form->GetLocalFormID() == localID ? form : nullptr;
	}
}

RE::TESLandTexture* FormSwapMap::GenerateLandTextureSnowVariant(const RE::TESLandTexture* a_landTexture)
{
	static constexpr std::array blackList = { "Snow"sv, "Ice"sv, "Winter"sv, "Frozen"sv, "Coast"sv, "River"sv };

	const auto editorID = edid::get_editorID(a_landTexture);
	if (!editorID.empty() && std::ranges::any_of(blackList, [&](const auto str) { return REX::STR::ICONTAINS(editorID, str); })) {
		return nullptr;
	}

	static constexpr RE::FormID LSnow01 = 0x0000089B;
	static constexpr RE::FormID LSnow02 = 0x0006A1B1;

	const auto mat = a_landTexture->materialType;
	const auto matID = mat ? mat->materialID : RE::MATERIAL_ID::kNone;

	RE::FormID formID;

	switch (matID) {
	case RE::MATERIAL_ID::kGrass:
		{
			static constexpr RE::FormID LGrassSnow01NoGrass = 0x0008B01E;
			static constexpr RE::FormID LGrassSnow01 = 0x00000894;

			switch (a_landTexture->GetFormID()) {
			case 0x0001342A:  // LFieldGrass02
			case 0x00024E46:  // LFieldGrass01NoGrass
			case 0x00024E30:  // LTundra01
			case 0x000A2741:  // LTundra01NoGrass
				formID = LSnow01;
				break;
			case 0x000134B7:  // LFieldDirtGrass01
			case 0x000300E4:  // LTundra02
				formID = LSnow02;
				break;
			default:
				formID = !a_landTexture->textureGrassList.empty() ? LGrassSnow01 : LGrassSnow01NoGrass;
				break;
			}
		}
		break;
	case RE::MATERIAL_ID::kDirt:
		{
			static constexpr RE::FormID LDirtSnowPath01 = 0x0001B082;

			switch (a_landTexture->GetFormID()) {
			case 0x00000C16:  // LDirt02
				formID = LSnow02;
				break;
			case 0xB424C:  // LDirtPath01
				formID = LDirtSnowPath01;
				break;
			default:
				formID = LSnow01;
				break;
			}
		}
		break;
	case RE::MATERIAL_ID::kStone:
	case RE::MATERIAL_ID::kStoneBroken:
	case RE::MATERIAL_ID::kGravel:
		{
			static constexpr RE::FormID LSnowRocks01 = 0x0006A1AF;
			static constexpr RE::FormID LSnowRockswGrass = 0x000F871F;

			switch (a_landTexture->GetFormID()) {
			case 0x0002C6C6:  // LTundraRocks01
				formID = LSnowRocks01;
				break;
			case 0x0006DE8B:  // LTundraRocks01NoRocks
				formID = LSnow01;
				break;
			default:
				formID = !a_landTexture->textureGrassList.empty() ? LSnowRockswGrass : LSnowRocks01;
				break;
			}
		}
		break;
	case RE::MATERIAL_ID::kSnow:
	case RE::MATERIAL_ID::kIce:
	case RE::MATERIAL_ID::kSand:
	case RE::MATERIAL_ID::kMud:
		return nullptr;
	default:
		formID = LSnow02;
		break;
	}

	return RE::TESForm::LookupByID<RE::TESLandTexture>(formID);
}

void FormSwapMap::LoadFormSwaps(RECORD a_record, CSimpleIniA::TNamesDepend& a_keys)
{
	if (a_keys.empty()) {
		return;
	}

	a_keys.sort(CSimpleIniA::Entry::LoadOrder());
	REX::INFO("\t\t[{}] read {} variants", get_name(a_record), a_keys.size());

	auto& map = get_map(a_record);
	for (const auto& key : a_keys) {
		const auto formPair = REX::STR::SPLIT(key.pItem, "|");
		if (formPair.size() <= kSwap) {
			continue;
		}

		try {
			const auto formID = INI::parse_form(formPair[kBase]);
			const auto swapFormID = INI::parse_form(formPair[kSwap]);
			if (formID != 0 && swapFormID != 0) {
				map.insert_or_assign(formID, swapFormID);
			}
		} catch (const std::logic_error&) {
			REX::WARN("\t\t[{}] invalid swap: {}", get_name(a_record), key.pItem);
		}
	}
}

void FormSwapMap::LoadFormSwaps(const CSimpleIniA& a_ini)
{
	for (const auto record : all_records()) {
		const auto name = get_name(record);

		CSimpleIniA::TNamesDepend keys;
		a_ini.GetAllKeys(name.data(), keys);
		LoadFormSwaps(record, keys);
	}
}

bool FormSwapMap::LoadCachedFormSwaps(RECORD a_record, const CSimpleIniA::TNamesDepend& a_keys)
{
	static constexpr std::array recordTypes{
		RE::TESLandTexture::FORMTYPE, RE::TESObjectACTI::FORMTYPE, RE::TESFurniture::FORMTYPE,
		RE::BGSMovableStatic::FORMTYPE, RE::TESObjectSTAT::FORMTYPE, RE::TESObjectTREE::FORMTYPE
	};
	static_assert(recordTypes.size() == std::to_underlying(RECORD::kFlora));

	const auto type = recordTypes[std::to_underlying(a_record)];
	FlatMap<RE::FormID, RE::FormID> loaded;
	loaded.reserve(a_keys.size());
	for (const auto& key : a_keys) {
		const std::string_view pair = key.pItem;
		const auto separator = pair.find('|');
		if (separator == std::string_view::npos || pair.find('|', separator + 1) != std::string_view::npos) {
			return false;
		}
		const auto base = ResolveCachedForm(pair.substr(0, separator));
		const auto swap = ResolveCachedForm(pair.substr(separator + 1));
		if (!base || !swap || base->GetFormType() != type || swap->GetFormType() != type) {
			return false;
		}
		if (!loaded.try_emplace(base->GetFormID(), swap->GetFormID()).second) {
			return false;
		}
	}

	get_map(a_record) = std::move(loaded);
	REX::INFO("\t\t[{}] read {} variants", get_name(a_record), a_keys.size());
	return true;
}

bool FormSwapMap::LoadOrGenerateWinterFormSwaps(CSimpleIniA& a_ini, WinterCache::CategoryCounts& a_counts, const std::array<bool, std::to_underlying(RECORD::kFlora)>& a_skipRecords)
{
	static_assert(std::tuple_size_v<WinterCache::CategoryCounts> == std::to_underlying(RECORD::kFlora));
	bool save = false;

	for (const auto record : standard_records()) {
		const auto index = std::to_underlying(record);
		const auto type = get_name(record);
		auto& count = a_counts[index];
		if (a_skipRecords[index]) {
			if (count == WinterCache::kNotGenerated) {
				count = WinterCache::kDisabled;
				save = true;
			}
			REX::INFO("\t\t[{}] skipping...", type);
			continue;
		}

		CSimpleIniA::TNamesDepend keys;
		a_ini.GetAllKeys(type.data(), keys);
		if (count >= 0 && std::cmp_equal(count, keys.size()) && LoadCachedFormSwaps(record, keys)) {
			continue;
		}
		if (count >= 0 || !keys.empty()) {
			REX::WARN("\t\t[{}] invalid or incomplete cached swaps, regenerating", type);
		}

		a_ini.Delete(type.data(), nullptr, true);
		get_map(record).clear();
		save = true;
		TempFormSwapMap map{};
		switch (record) {
		case RECORD::kLandTextures:
			get_snow_variants<RE::TESLandTexture>(a_ini, record, map);
			break;
		case RECORD::kActivators:
			get_snow_variants<RE::TESObjectACTI>(a_ini, record, map);
			break;
		case RECORD::kFurniture:
			get_snow_variants<RE::TESFurniture>(a_ini, record, map);
			break;
		case RECORD::kMovableStatics:
			get_snow_variants<RE::BGSMovableStatic>(a_ini, record, map);
			break;
		case RECORD::kStatics:
			get_snow_variants<RE::TESObjectSTAT>(a_ini, record, map);
			break;
		case RECORD::kTrees:
			get_snow_variants<RE::TESObjectTREE>(a_ini, record, map);
			break;
		default:
			break;
		}
		count = static_cast<std::int64_t>(get_map(record).size());
	}

	return save;
}

RE::TESBoundObject* FormSwapMap::GetSwapForm(const RE::TESForm* a_form)
{
	auto& map = get_map(a_form->GetFormType());
	if (map.empty()) {
		return nullptr;
	}

	const auto it = map.find(a_form->GetFormID());
	return it != map.end() ? RE::TESForm::LookupByID<RE::TESBoundObject>(it->second) : nullptr;
}

RE::TESLandTexture* FormSwapMap::GetSwapLandTexture(const RE::TESLandTexture* a_landTxst)
{
	const auto& map = get_map(RECORD::kLandTextures);
	if (map.empty()) {
		return nullptr;
	}

	const auto it = map.find(a_landTxst->GetFormID());
	return it != map.end() ? RE::TESForm::LookupByID<RE::TESLandTexture>(it->second) : nullptr;
}

RE::TESLandTexture* FormSwapMap::GetSwapLandTexture(const RE::BGSTextureSet* a_txst)
{
	const auto landTexture = Cache::DataHolder::GetSingleton()->GetLandTextureFromTextureSet(a_txst);
	return GetSwapLandTexture(landTexture);
}
