#ifndef VOS_PDF_LOADER_H
#define VOS_PDF_LOADER_H


#include <string>


class PdfLoader {
public:
						PdfLoader();
						~PdfLoader();

			void		SetPdfjsPath(const std::string& path);
	std::string			BuildViewerURL(const std::string& sourceURL) const;
			bool		IsPDFContentType(const std::string& mimeType) const;
			bool		HasViewer() const;

private:
			std::string	fPdfjsPath;
};


#endif // VOS_PDF_LOADER_H
