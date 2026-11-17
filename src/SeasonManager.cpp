#include "SeasonManager.h"
#include "Papyrus.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <process.h>
#include <Windows.h>
#undef ERROR

namespace
{
	constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
	constexpr std::string_view cacheStampPrefix = "; Seasons of Skyrim winter cache v2 ";

	bool HashPluginFile(std::size_t& a_hash, const RE::TESFile& a_plugin)
	{
		const auto path = std::filesystem::path(L"Data") / a_plugin.GetFilename();
		const auto file = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		BY_HANDLE_FILE_INFORMATION identity{};
		FILE_BASIC_INFO times{};
		const bool read = ::GetFileInformationByHandle(file, &identity) &&
			::GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof(times));
		::CloseHandle(file);
		if (!read) {
			return false;
		}

		boost::hash_combine(a_hash, a_plugin.GetFilename());
		boost::hash_combine(a_hash, a_plugin.GetCompileIndex());
		boost::hash_combine(a_hash, a_plugin.GetSmallFileCompileIndex());
		boost::hash_combine(a_hash, identity.dwVolumeSerialNumber);
		boost::hash_combine(a_hash, identity.nFileIndexHigh);
		boost::hash_combine(a_hash, identity.nFileIndexLow);
		boost::hash_combine(a_hash, identity.nFileSizeHigh);
		boost::hash_combine(a_hash, identity.nFileSizeLow);
		boost::hash_combine(a_hash, times.LastWriteTime.QuadPart);
		boost::hash_combine(a_hash, times.ChangeTime.QuadPart);
		return true;
	}

	std::optional<std::size_t> GetWinterCacheFingerprint()
	{
		const auto dataHandler = RE::TESDataHandler::GetSingleton();
		std::size_t hash = 0;
		const auto hashFiles = [&](const auto& a_files) {
			for (const auto* plugin : a_files) {
				if (plugin && plugin->GetCompileIndex() != 0xFF && !HashPluginFile(hash, *plugin)) {
					REX::WARN("\tCouldn't fingerprint loaded plugin {}", plugin->GetFilename());
					return false;
				}
			}
			return true;
		};

#ifdef SKYRIMVR
		if (const auto mods = dataHandler->VRcompiledFileCollection) {
			if (!hashFiles(mods->files) || !hashFiles(mods->smallFiles)) {
				return std::nullopt;
			}
		} else if (!hashFiles(dataHandler->files)) {
			return std::nullopt;
		}
#else
		const auto& mods = dataHandler->compiledFileCollection;
		if (!hashFiles(mods.files) || !hashFiles(mods.smallFiles)) {
			return std::nullopt;
		}
#endif
		return hash;
	}

	bool LoadWinterCache(CSimpleIniA& a_ini, const std::filesystem::path& a_path, std::string_view a_stamp)
	{
		std::ifstream file(a_path, std::ios::binary);
		std::string header;
		if (!std::getline(file, header)) {
			return false;
		}
		std::string_view stamp = header;
		if (stamp.starts_with(utf8Bom)) {
			stamp.remove_prefix(utf8Bom.size());
		}
		if (stamp.ends_with('\r')) {
			stamp.remove_suffix(1);
		}
		if (stamp != a_stamp) {
			return false;
		}

		const std::string contents(std::istreambuf_iterator<char>{ file }, {});
		return !file.bad() && a_ini.LoadData(contents) >= SI_OK;
	}

	bool SaveWinterCache(const CSimpleIniA& a_ini, const std::filesystem::path& a_path, std::string_view a_stamp)
	{
		std::string contents{ utf8Bom };
		contents.append(a_stamp).append("\r\n");
		if (a_ini.Save(contents) < SI_OK) {
			return false;
		}

		auto tempPath = a_path;
		tempPath += std::format(L".{}.tmp", _getpid());
		std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
		file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
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
}

Season* SeasonManager::GetSeasonImpl(SEASON_TYPE a_season)
{
	switch (a_season) {
	case SEASON_TYPE::kWinter:
		return &winter;
	case SEASON_TYPE::kSpring:
		return &spring;
	case SEASON_TYPE::kSummer:
		return &summer;
	case SEASON_TYPE::kAutumn:
		return &autumn;
	default:
		return nullptr;
	}
}

Season* SeasonManager::GetCurrentSeason(bool a_ignoreOverride)
{
	if (!a_ignoreOverride && seasonOverride != SEASON_TYPE::kNone) {
		return GetSeasonImpl(seasonOverride);
	}

	switch (GetSeasonMode()) {
	case SEASON_MODE::kPermanentWinter:
		return &winter;
	case SEASON_MODE::kPermanentSpring:
		return &spring;
	case SEASON_MODE::kPermanentSummer:
		return &summer;
	case SEASON_MODE::kPermanentAutumn:
		return &autumn;
	case SEASON_MODE::kSeasonal:
		{
			const auto calendar = RE::Calendar::GetSingleton();
			const auto month = calendar ? calendar->GetMonth() : 7;

			if (const auto it = monthToSeasons.find(static_cast<MONTH>(month)); it != monthToSeasons.end()) {
				return GetSeasonImpl(it->second);
			}

			return nullptr;
		}
	default:
		return nullptr;
	}
}

bool SeasonManager::UpdateSeason()
{
	bool shouldUpdate = false;

	if (loadedFromSave) {
		shouldUpdate = true;
	}

	if (seasonOverride != SEASON_TYPE::kNone) {
		const auto tempLastSeason = lastSeason;
		lastSeason = seasonOverride;

		if (!shouldUpdate) {
			shouldUpdate = seasonOverride != tempLastSeason;
		}
		if (!loadedFromSave && shouldUpdate) {
			Papyrus::Events::Manager::GetSingleton()->seasonChange.QueueEvent(std::to_underlying(tempLastSeason), std::to_underlying(seasonOverride), true);
		}

	} else {
		lastSeason = currentSeason;

		if (!shouldUpdate) {
			const auto season = GetCurrentSeason();
			currentSeason = season ? season->GetType() : SEASON_TYPE::kNone;

			shouldUpdate = currentSeason != lastSeason;
		}
		if (!loadedFromSave && shouldUpdate) {
			Papyrus::Events::Manager::GetSingleton()->seasonChange.QueueEvent(std::to_underlying(lastSeason), std::to_underlying(currentSeason), false);
		}
	}

	if (loadedFromSave) {
		loadedFromSave = false;
	}

	return shouldUpdate;
}

Season* SeasonManager::GetSeason()
{
	if (!GetExterior()) {
		return nullptr;
	}

	if (seasonOverride != SEASON_TYPE::kNone) {
		return GetSeasonImpl(seasonOverride);
	} else {
		if (currentSeason == SEASON_TYPE::kNone) {
			UpdateSeason();
		}
		return GetSeasonImpl(currentSeason);
	}
}

void SeasonManager::LoadSettings()
{
	REX::INFO("{:*^30}", "SETTINGS");

	ResetOutdatedSettings();

	const auto store = REX::FIniSettingStore::GetSingleton();
	store->Init(settings, "");
	store->Load();
	store->Save();

	RebuildMonthToSeasonMap();

	using record_type = FormSwapMap::RECORD;

	mainWINSwap.skip = skipWINSwap.GetValue();
	mainWINSwap.skipRecords[std::to_underlying(record_type::kLandTextures)] = skipLandTextures.GetValue();
	mainWINSwap.skipRecords[std::to_underlying(record_type::kActivators)] = skipActivators.GetValue();
	mainWINSwap.skipRecords[std::to_underlying(record_type::kFurniture)] = skipFurniture.GetValue();
	mainWINSwap.skipRecords[std::to_underlying(record_type::kMovableStatics)] = skipMovableStatics.GetValue();
	mainWINSwap.skipRecords[std::to_underlying(record_type::kStatics)] = skipStatics.GetValue();
	mainWINSwap.skipRecords[std::to_underlying(record_type::kTrees)] = skipTrees.GetValue();

	REX::INFO("Season mode is {}", seasonType.GetValue());
}

void SeasonManager::RebuildMonthToSeasonMap()
{
	monthToSeasons.clear();

	for (const auto month : enum_range(MONTH::kMorningStar, MONTH::kTotal)) {
		const auto value = monthSettings[std::to_underlying(month)].GetValue();
		if (value >= std::to_underlying(SEASON_TYPE::kTotal)) {
			REX::WARN("Invalid season {} for month {}, defaulting to none", value, std::to_underlying(month));
			monthToSeasons.emplace(month, SEASON_TYPE::kNone);
		} else {
			monthToSeasons.emplace(month, static_cast<SEASON_TYPE>(value));
		}
	}
}

void SeasonManager::ResetOutdatedSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	if (ini.LoadFile(settings) < SI_OK) {
		return;
	}

	// delete and recreate settings if new settings are not found
	if (ini.GetValue("Settings", "Morning Star") == nullptr || ini.GetValue("Winter", "Flora") == nullptr) {
		ini.Delete("Settings", nullptr);
		ini.Delete("Winter", nullptr);
		ini.Delete("Spring", nullptr);
		ini.Delete("Summer", nullptr);
		ini.Delete("Autumn", nullptr);
		(void)ini.SaveFile(settings);
	}
}

void SeasonManager::LoadOrGenerateWinterFormSwap()
{
	if (mainWINSwap.skip) {
		REX::INFO("Main WIN formswap loading disabled in config");
		return;
	}

	constexpr auto path = L"Data/Seasons/MainFormSwap_WIN.ini";
	const auto started = std::chrono::steady_clock::now();

	REX::INFO("Loading main WIN formswap settings");

	CSimpleIniA ini;
	ini.SetUnicode();
	ini.SetMultiKey();
	ini.SetAllowKeyOnly();

	const auto fingerprint = GetWinterCacheFingerprint();
	const auto stamp = fingerprint ? std::format("{}{:016x}", cacheStampPrefix, *fingerprint) : std::string{};
	const auto shouldRegenerate = !fingerprint || !LoadWinterCache(ini, path, stamp);
	if (shouldRegenerate) {
		ini.Reset();
		REX::INFO("\tWinter cache missing or loaded plugins changed, rebuilding main WIN formswap");
	} else {
		REX::INFO("\tLoaded plugin fingerprint matches winter cache");
	}

	auto& winFormSwapMap = winter.GetFormSwapMap();
	if (winFormSwapMap.LoadOrGenerateWinterFormSwaps(ini, shouldRegenerate, mainWINSwap.skipRecords) && fingerprint && !SaveWinterCache(ini, path, stamp)) {
		REX::WARN("\tCouldn't save main WIN formswap settings");
	}
	REX::INFO("\tMain WIN form swaps ready in {:.3f} s", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
}

void SeasonManager::LoadSeasonData(Season& a_season, CSimpleIniA& a_settings)
{
	std::vector<std::string> configs;

	const auto& [type, suffix] = a_season.GetID();

	REX::INFO("{}", type);

	for (constexpr auto folder = R"(Data\Seasons)"; const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.is_regular_file() && entry.path().extension() == ".ini"sv) {
			const auto& path = entry.path().string();
			const auto& pathStem = entry.path().stem().string();

			if (pathStem.ends_with(suffix) && !path.contains("MainFormSwap"sv)) {
				configs.push_back(path);
			}
		}
	}

	if (configs.empty()) {
		REX::WARN("\tNo .ini files with _{} suffix were found in Data/Seasons folder, skipping {} formswaps", suffix, suffix == "WIN"sv ? "secondary" : "all");
		return;
	}

	REX::INFO("\t{} matching inis found", configs.size());

	std::ranges::sort(configs);

	for (auto& path : configs) {
		REX::INFO("\tINI : {}", path);

		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetMultiKey();
		ini.SetAllowKeyOnly();

		if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
			REX::ERROR("\t\tcouldn't read INI");
			continue;
		}

		a_season.LoadData(ini);
	}

	//save worldspaces to settings so DynDOLOD can read them
	a_season.SaveData(a_settings);
}

void SeasonManager::LoadSeasonData()
{
	CSimpleIniA settingsINI;
	settingsINI.SetUnicode();

	settingsINI.LoadFile(settings);

	ForEachSeason([&](SEASON_TYPE, Season& a_season) {
		LoadSeasonData(a_season, settingsINI);
	});

	(void)settingsINI.SaveFile(settings);
}

void SeasonManager::LoadValidWorldspaces()
{
	ForEachSeason([](SEASON_TYPE, Season& a_season) {
		a_season.LoadWorldspaces();
	});
}

void SeasonManager::CheckLODExists()
{
	REX::INFO("{:*^30}", "LOD");

	ForEachSeason([](SEASON_TYPE, Season& a_season) {
		a_season.CheckLODExists();
	});
}

void SeasonManager::SaveSeason(std::string_view a_savePath)
{
	if (const auto player = RE::PlayerCharacter::GetSingleton(); !player->parentCell || !player->parentCell->IsExteriorCell()) {
		return;
	}

	CSimpleIniA ini;
	ini.SetUnicode();

	ini.LoadFile(serializedSeasonList);

	const auto season = GetCurrentSeason(true);
	currentSeason = season ? season->GetType() : SEASON_TYPE::kNone;

	const auto seasonData = std::format("{}|{}", std::to_underlying(currentSeason), std::to_underlying(seasonOverride));
	ini.SetValue("Saves", a_savePath.data(), seasonData.c_str(), nullptr);

	(void)ini.SaveFile(serializedSeasonList);
}

void SeasonManager::LoadSeason(const std::string& a_savePath)
{
	CSimpleIniA ini;
	ini.SetUnicode();

	ini.LoadFile(serializedSeasonList);

	const auto seasonData = REX::STR::SPLIT(ini.GetValue("Saves", a_savePath.c_str(), "3"), "|");
	if (seasonData.size() == 2) {
		currentSeason = REX::STR::TO_NUM<SEASON_TYPE>(seasonData[0]);
		seasonOverride = REX::STR::TO_NUM<SEASON_TYPE>(seasonData[1]);
	} else {
		currentSeason = REX::STR::TO_NUM<SEASON_TYPE>(seasonData[0]);
		seasonOverride = SEASON_TYPE::kNone;
	}

	loadedFromSave = true;

	(void)ini.SaveFile(serializedSeasonList);
}

void SeasonManager::ClearSeason(std::string_view a_savePath) const
{
	CSimpleIniA ini;
	ini.SetUnicode();

	ini.LoadFile(serializedSeasonList);

	ini.DeleteValue("Saves", a_savePath.data(), nullptr);

	(void)ini.SaveFile(serializedSeasonList);
}

void SeasonManager::CleanupSerializedSeasonList() const
{
	constexpr auto get_save_directory = []() -> std::optional<std::filesystem::path> {
		if (auto path = SKSE::log::log_directory()) {
			path->remove_filename();  // remove "/SKSE"
			path->append(*"sLocalSavePath:General"_ini);
			return path;
		}
		return std::nullopt;
	};

	const auto directory = get_save_directory();
	if (!directory) {
		return;
	}

	REX::INFO("{:*^30}", "SAVES");

	REX::INFO("Save directory is {}", directory->string());

	CSimpleIniA ini;
	ini.SetUnicode();

	if (const auto rc = ini.LoadFile(serializedSeasonList); rc < 0) {
		return;
	}

	CSimpleIniA::TNamesDepend values;
	ini.GetAllKeys("Saves", values);
	values.sort(CSimpleIniA::Entry::LoadOrder());

	if (!values.empty()) {
		std::vector<std::string> badSaves;
		badSaves.reserve(values.size());
		for (const auto& key : values) {
			if (auto save = std::format("{}{}.ess", directory->string(), key.pItem); !std::filesystem::exists(save)) {
				badSaves.emplace_back(key.pItem);
			}
		}
		for (auto& badSave : badSaves) {
			ini.DeleteValue("Saves", badSave.c_str(), nullptr);
		}
	}

	(void)ini.SaveFile(serializedSeasonList);
}

SEASON_MODE SeasonManager::GetSeasonMode() const
{
	return static_cast<SEASON_MODE>(seasonType.GetValue());
}

SEASON_TYPE SeasonManager::GetCurrentSeasonType()
{
	const auto season = GetCurrentSeason();
	return season ? season->GetType() : SEASON_TYPE::kNone;
}

SEASON_TYPE SeasonManager::GetSeasonType()
{
	const auto season = GetSeason();
	return season ? season->GetType() : SEASON_TYPE::kNone;
}

bool SeasonManager::CanApplySnowShader()
{
	const auto season = GetSeason();
	return season ? season->CanApplySnowShader() : false;
}

std::pair<bool, std::string_view> SeasonManager::CanSwapLOD(LOD_TYPE a_type)
{
	const auto season = GetSeason();
	return season ? std::make_pair(season->CanSwapLOD(a_type), season->GetID().suffix) : std::make_pair(false, ""sv);
}

RE::TESBoundObject* SeasonManager::GetSwapForm(const RE::TESForm* a_form)
{
	const auto season = GetSeason();
	return season && season->CanSwapForm(a_form->GetFormType()) ? season->GetFormSwapMap().GetSwapForm(a_form) : nullptr;
}

RE::TESLandTexture* SeasonManager::GetSwapLandTexture(const RE::TESLandTexture* a_landTxst)
{
	const auto season = GetSeason();
	return season && season->CanSwapLandscape() ? season->GetFormSwapMap().GetSwapLandTexture(a_landTxst) : nullptr;
}

RE::TESLandTexture* SeasonManager::GetSwapLandTexture(const RE::BGSTextureSet* a_txst)
{
	const auto season = GetSeason();
	return season && season->CanSwapLandscape() ? season->GetFormSwapMap().GetSwapLandTexture(a_txst) : nullptr;
}

RE::TESLandTexture* SeasonManager::GetSwapLandTextureForGrass(const RE::TESLandTexture* a_landTxst)
{
	const auto season = GetSeason();
	return season && season->CanSwapForm(RE::FormType::Grass) ? season->GetFormSwapMap().GetSwapLandTexture(a_landTxst) : nullptr;
}

bool SeasonManager::GetExterior()
{
	return isExterior;
}

void SeasonManager::SetExterior(bool a_isExterior)
{
	isExterior = a_isExterior;
}

SEASON_TYPE SeasonManager::GetSeasonOverride() const
{
	return seasonOverride;
}

bool SeasonManager::PreferMultipass() const
{
	return preferMultipass;
}

void SeasonManager::SetSeasonOverride(SEASON_TYPE a_season)
{
	seasonOverride = a_season;
}

SeasonManager::EventResult SeasonManager::ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*)
{
	if (!a_event || GetExterior()) {
		return EventResult::kContinue;
	}

	constexpr auto is_teleport_door = [](auto&& a_ref, auto&& a_object) {
		return a_ref && a_ref->IsPlayerRef() && a_object && a_object->extraList.HasType(RE::ExtraDataType::kTeleport);
	};

	if (!is_teleport_door(a_event->actionRef, a_event->objectActivated)) {
		return EventResult::kContinue;
	}

	if (const auto tes = RE::TES::GetSingleton(); UpdateSeason()) {
		tes->PurgeBufferedCells();
	}

	return EventResult::kContinue;
}
