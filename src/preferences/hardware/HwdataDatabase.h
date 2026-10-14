/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HWDATA_DATABASE_H
#define _HWDATA_DATABASE_H


#include <stdint.h>

#include <map>
#include <string>


// Parser for the Debian hwdata / pci.ids / usb.ids name tables. The
// preflet loads the system copies at runtime; the tests load fixture
// snippets. Names are looked up case-sensitively by numeric id, the
// same way lspci -k and lsusb resolve them.


namespace Hardware {


class HwdataDatabase {
public:
							HwdataDatabase();

			bool			LoadPci(const char* path);
			bool			LoadUsb(const char* path);

			// First existing path wins; empty string when none exist.
	static	std::string		FindPciIds();
	static	std::string		FindUsbIds();

			const char*		PciVendor(uint32_t vendor) const;
			const char*		PciProduct(uint32_t vendor, uint32_t product) const;
			const char*		PciSubsystem(uint32_t vendor, uint32_t subsystemVendor,
									uint32_t product, uint32_t subsystemProduct) const;
			const char*		PciClassCode(uint32_t classCode) const;

			const char*		UsbVendor(uint32_t vendor) const;
			const char*		UsbProduct(uint32_t vendor, uint32_t product) const;
			const char*		UsbClassCode(uint32_t classCode) const;

private:
	typedef std::map<uint32_t, std::string>				IdNameMap;
	typedef std::map<uint64_t, std::string>				KeyedNameMap;
	typedef std::map<uint32_t, std::string>				ClassMap;

			void			_ClearPci();
			void			_ClearUsb();
			bool			_LoadPciFile(const std::string& path);
			bool			_LoadUsbFile(const std::string& path);

			IdNameMap		fPciVendors;
			KeyedNameMap	fPciProducts;
			KeyedNameMap	fPciSubsystems;
			ClassMap		fPciClasses;

			IdNameMap		fUsbVendors;
			KeyedNameMap	fUsbProducts;
			ClassMap		fUsbClasses;
};


}	// namespace Hardware


#endif	// _HWDATA_DATABASE_H
