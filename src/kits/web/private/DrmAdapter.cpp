#include "drm_adapter.h"

#include "logging.h"

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <vector>


//	#pragma mark - Public


DrmAdapter&
DrmAdapter::Instance()
{
	static DrmAdapter adapter;
	return adapter;
}


//	#pragma mark - Constructor/Destructor


DrmAdapter::DrmAdapter()
	:
	fLoaded(false),
	fDlHandle(nullptr)
{
}


DrmAdapter::~DrmAdapter()
{
	if (fDlHandle)
		dlclose(fDlHandle);
}


//	#pragma mark - Private


std::string
DrmAdapter::_SearchCDM()
{
	const char* home = getenv("HOME");
	std::vector<std::string> paths;

	const char* envPath = getenv("NETPLUS_WIDEVINE_PATH");
	if (envPath)
		paths.push_back(envPath);

	if (home) {
		paths.push_back(std::string(home)
			+ "/.local/share/netplus/widevine/libwidevinecdm.so");
	}

	paths.push_back("/usr/lib/netplus/widevine/libwidevinecdm.so");
	paths.push_back("/opt/google/chrome/WidevineCdm/_platform_specific/"
		"linux_x64/libwidevinecdm.so");

	if (home) {
		namespace fs = std::filesystem;
		std::string firefoxBase = std::string(home) + "/.mozilla/firefox";
		if (fs::exists(firefoxBase)) {
			for (const auto& entry : fs::directory_iterator(firefoxBase)) {
				if (entry.is_directory()
					&& entry.path().filename().string().find(".default")
						!= std::string::npos) {
					std::string gmpDir = entry.path().string()
						+ "/gmp-widevinecdm";
					if (fs::exists(gmpDir)) {
						for (const auto& sub :
							fs::recursive_directory_iterator(gmpDir)) {
							if (sub.path().filename() == "libwidevinecdm.so")
								paths.push_back(sub.path().string());
						}
					}
				}
			}
		}
	}

	for (const auto& p : paths) {
		if (std::filesystem::exists(p)) {
			log_info("DrmAdapter: found CDM at %s", p.c_str());
			return p;
		}
	}

	return "";
}


//	#pragma mark - Public


bool
DrmAdapter::LoadWidevine(const std::string& cdmPath)
{
	if (fLoaded) return true;

	std::string path = cdmPath.empty() ? _SearchCDM() : cdmPath;
	if (path.empty()) {
		log_warn("DrmAdapter: no Widevine CDM found");
		return false;
	}

	fDlHandle = dlopen(path.c_str(), RTLD_LAZY);
	if (!fDlHandle) {
		log_error("DrmAdapter: dlopen failed: %s", dlerror());
		return false;
	}

	fCdmPath = path;
	fLoaded = true;

	size_t lastSlash = path.find_last_of('/');
	fCdmVersion = (lastSlash != std::string::npos)
		? path.substr(lastSlash + 1) : path;

	log_info("DrmAdapter: Widevine loaded from %s", path.c_str());

	if (onDRMStateChanged)
		onDRMStateChanged(true);

	return true;
}
