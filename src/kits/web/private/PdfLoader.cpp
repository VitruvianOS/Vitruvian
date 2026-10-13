#include "pdf_loader.h"

#include "logging.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>


//	#pragma mark - Constructor/Destructor


PdfLoader::PdfLoader()
{
	const char* home = getenv("HOME");
	if (home)
		fPdfjsPath = std::string(home) + "/.local/share/netplus/pdfjs";
	else
		fPdfjsPath = "/usr/share/netplus/pdfjs";
}


PdfLoader::~PdfLoader()
{
}


//	#pragma mark - Public


void
PdfLoader::SetPdfjsPath(const std::string& path)
{
	fPdfjsPath = path;
}


std::string
PdfLoader::BuildViewerURL(const std::string& sourceURL) const
{
	std::string encoded;
	for (char c : sourceURL) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
			|| c == '~') {
			encoded += c;
		} else {
			char hex[4];
			snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
			encoded += hex;
		}
	}
	return "vos-pdf://viewer?src=" + encoded;
}


bool
PdfLoader::IsPDFContentType(const std::string& mimeType) const
{
	return mimeType == "application/pdf";
}


bool
PdfLoader::HasViewer() const
{
	// The viewer page is static HTML plus pdf.js; without the bundle it
	// cannot render anything, so callers should download instead.
	namespace fs = std::filesystem;
	std::error_code ec;
	return fs::exists(fPdfjsPath + "/viewer.html", ec);
}
