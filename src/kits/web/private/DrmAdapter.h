#ifndef VOS_DRM_ADAPTER_H
#define VOS_DRM_ADAPTER_H


#include <functional>
#include <string>


class DrmAdapter {
public:
	static DrmAdapter&	Instance();

			bool		LoadWidevine(const std::string& cdmPath = "");
			bool		IsLoaded() const { return fLoaded; }
	const std::string&	CdmVersion() const { return fCdmVersion; }
	const std::string&	CdmPath() const { return fCdmPath; }

			std::function<void(bool loaded)> onDRMStateChanged;

private:
						DrmAdapter();
						~DrmAdapter();

						DrmAdapter(const DrmAdapter&) = delete;
	DrmAdapter&			operator=(const DrmAdapter&) = delete;

			std::string	_SearchCDM();

			std::string	fCdmPath;
			std::string	fCdmVersion;
			bool		fLoaded;
			void*		fDlHandle;
};


#endif // VOS_DRM_ADAPTER_H
