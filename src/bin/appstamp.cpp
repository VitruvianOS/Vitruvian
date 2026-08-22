/*
 * Author: Vláďa Janeček <vlada@janecek.cloud>
 *
 * appstamp — stamp BeOS application attributes onto a Linux binary from its
 * freedesktop metadata, so Deskbar and Tracker show the application's real
 * icon instead of the generic one.
 *
 * Debian binaries carry no BeOS resources or attributes; their name and icon
 * live in /usr/share/applications/<app>.desktop and the icon theme. Deskbar
 * resolves both purely from the executable's attributes (BAppFileInfo in
 * attributes-only mode for a plain ELF), so bridging is a matter of writing
 * the right xattrs:
 *
 *   BEOS:M:STD_ICON  16x16 B_CMAP8   ('MICN')
 *   BEOS:L:STD_ICON  32x32 B_CMAP8   ('ICON')
 *   BEOS:ICON        HVIF vector     ('VICN', only with --hvif)
 *   BEOS:APP_SIG     MIME signature
 *   SYS:NAME         catalog entry (untranslated label; no catalog needed)
 *
 * dpkg does not track xattrs, so the package content stays pristine.
 *
 * Deliberately no BApplication and no BBitmap: both need a live app_server,
 * and this runs from a root ssh session. PNG via libpng's simplified API;
 * CMAP8 by nearest-colour over the static system palette (Palette.h).
 */
#include <fs_attr.h>

#include <GraphicsDefs.h>
#include <Mime.h>
#include <Node.h>
#include <TypeConstants.h>

#include <Palette.h>

#include <png.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>


static const char* kMiniAttr = "BEOS:M:STD_ICON";
static const char* kLargeAttr = "BEOS:L:STD_ICON";
static const char* kVectorAttr = "BEOS:ICON";

// The ext4 xattr value ceiling is one block minus overhead; the nexus layer
// adds a 4-byte type prefix and does no chunking. Anything near 4K is refused
// by the filesystem anyway — check early for a friendlier message.
static const size_t kMaxAttrPayload = 4000;


struct DesktopEntry {
	std::string path;
	std::string name;
	std::string icon;
	std::string exec;	// raw Exec= line, for --all to resolve the binary
	// Display gating, honored by --all (not in single-binary mode, where the
	// user has chosen the target deliberately).
	bool noDisplay = false;
	bool terminal = false;
	bool isApplication = true;	// Type= defaults to Application when absent
	bool consoleOnly = false;	// Categories contains ConsoleOnly
};


static std::string
leafName(const std::string& path)
{
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


// First word of an Exec= line, stripped of its directory part. Quoting and
// field codes (%f, %u) never appear in the first word of well-formed entries.
static std::string
execBasename(const std::string& execLine)
{
	size_t space = execLine.find(' ');
	return leafName(execLine.substr(0, space));
}


// With an empty wantedLeaf the Exec/TryExec match is not required — used for
// an explicitly given .desktop file, which the caller has already chosen.
static bool
parseDesktopFile(const std::string& path, DesktopEntry& entry,
	const std::string& wantedLeaf)
{
	FILE* f = fopen(path.c_str(), "r");
	if (f == NULL)
		return false;

	bool inDesktopEntry = false;
	bool matches = wantedLeaf.empty();
	std::string name, icon, exec;
	bool noDisplay = false, terminal = false, isApplication = true;
	bool consoleOnly = false;

	char line[1024];
	while (fgets(line, sizeof(line), f) != NULL) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();

		if (!s.empty() && s[0] == '[') {
			// Only the main group holds the keys we want; Desktop Actions
			// repeat Exec= lines that must not cause false matches.
			inDesktopEntry = (s == "[Desktop Entry]");
			continue;
		}
		if (!inDesktopEntry)
			continue;

		size_t eq = s.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = s.substr(0, eq);
		std::string value = s.substr(eq + 1);

		if (key == "Name")
			name = value;
		else if (key == "Icon")
			icon = value;
		else if (key == "Type")
			isApplication = (value == "Application");
		else if (key == "NoDisplay")
			noDisplay = (value == "true");
		else if (key == "Terminal")
			terminal = (value == "true");
		else if (key == "Categories")
			consoleOnly = value.find("ConsoleOnly") != std::string::npos;
		else if (key == "TryExec" || key == "Exec") {
			// Exec is the one to run; TryExec only gates availability.
			if (key == "Exec" && exec.empty())
				exec = value;
			if (execBasename(value) == wantedLeaf)
				matches = true;
		}
	}
	fclose(f);

	if (!matches)
		return false;
	entry.path = path;
	entry.name = name;
	entry.icon = icon;
	entry.exec = exec;
	entry.noDisplay = noDisplay;
	entry.terminal = terminal;
	entry.isApplication = isApplication;
	entry.consoleOnly = consoleOnly;
	return true;
}


static bool
findDesktopFor(const std::string& binary, DesktopEntry& entry)
{
	const std::string leaf = leafName(binary);
	static const char* kDirs[] = {
		"/usr/share/applications",
		"/usr/local/share/applications",
	};

	for (const char* dirPath : kDirs) {
		DIR* dir = opendir(dirPath);
		if (dir == NULL)
			continue;
		struct dirent* ent;
		while ((ent = readdir(dir)) != NULL) {
			std::string fname(ent->d_name);
			if (fname.size() < 9
				|| fname.compare(fname.size() - 8, 8, ".desktop") != 0)
				continue;
			if (parseDesktopFile(std::string(dirPath) + "/" + fname, entry,
					leaf)) {
				closedir(dir);
				return true;
			}
		}
		closedir(dir);
	}
	return false;
}


// ---- PNG loading and scaling ----------------------------------------------

struct Image {
	uint32 width = 0;
	uint32 height = 0;
	std::vector<uint8> rgba;	// width * height * 4
};


static bool
loadPng(const std::string& path, Image& img)
{
	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;

	if (!png_image_begin_read_from_file(&png, path.c_str()))
		return false;
	png.format = PNG_FORMAT_RGBA;
	img.width = png.width;
	img.height = png.height;
	img.rgba.resize(PNG_IMAGE_SIZE(png));
	if (!png_image_finish_read(&png, NULL, img.rgba.data(), 0, NULL)) {
		png_image_free(&png);
		return false;
	}
	return true;
}


// Box filter, correct for both down- and upscaling at these tiny sizes.
// Icons are 16/32 px; nothing here is worth pulling a resampling library for.
static Image
scaleTo(const Image& src, uint32 size)
{
	Image dst;
	dst.width = size;
	dst.height = size;
	dst.rgba.resize(size * size * 4);

	for (uint32 y = 0; y < size; y++) {
		uint32 sy0 = y * src.height / size;
		uint32 sy1 = (y + 1) * src.height / size;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (uint32 x = 0; x < size; x++) {
			uint32 sx0 = x * src.width / size;
			uint32 sx1 = (x + 1) * src.width / size;
			if (sx1 <= sx0)
				sx1 = sx0 + 1;

			uint32 sum[4] = {0, 0, 0, 0};
			uint32 count = 0;
			for (uint32 sy = sy0; sy < sy1 && sy < src.height; sy++) {
				const uint8* p = &src.rgba[(sy * src.width + sx0) * 4];
				for (uint32 sx = sx0; sx < sx1 && sx < src.width; sx++) {
					sum[0] += p[0];
					sum[1] += p[1];
					sum[2] += p[2];
					sum[3] += p[3];
					p += 4;
					count++;
				}
			}
			uint8* d = &dst.rgba[(y * size + x) * 4];
			for (int c = 0; c < 4; c++)
				d[c] = count > 0 ? sum[c] / count : 0;
		}
	}
	return dst;
}


// Icon theme lookup: prefer the exact size, then the smallest larger art
// (downscales cleanly), then the largest smaller one, then pixmaps.
static bool
findThemePng(const std::string& iconName, uint32 size, std::string& path)
{
	if (!iconName.empty() && iconName[0] == '/') {
		path = iconName;
		FILE* f = fopen(path.c_str(), "r");
		if (f != NULL) {
			fclose(f);
			return true;
		}
		return false;
	}

	static const uint32 kSizes[] = {16, 22, 24, 32, 48, 64, 128, 256, 512};
	std::vector<uint32> order;
	order.push_back(size);
	for (uint32 s : kSizes)
		if (s > size)
			order.push_back(s);
	for (int i = sizeof(kSizes) / sizeof(kSizes[0]) - 1; i >= 0; i--)
		if (kSizes[i] < size)
			order.push_back(kSizes[i]);

	char buf[512];
	for (uint32 s : order) {
		snprintf(buf, sizeof(buf),
			"/usr/share/icons/hicolor/%ux%u/apps/%s.png", s, s,
			iconName.c_str());
		FILE* f = fopen(buf, "r");
		if (f != NULL) {
			fclose(f);
			path = buf;
			return true;
		}
	}
	snprintf(buf, sizeof(buf), "/usr/share/pixmaps/%s.png", iconName.c_str());
	FILE* f = fopen(buf, "r");
	if (f != NULL) {
		fclose(f);
		path = buf;
		return true;
	}
	return false;
}


// Nearest colour in the static system palette. PaletteConverter would do
// this, but its lookup methods are inline-defined inside libbe and not
// exported; for an icon's ~1K pixels a direct search is instant anyway.
static uint8
nearestPaletteIndex(uint8 r, uint8 g, uint8 b)
{
	uint32 best = UINT32_MAX;
	uint8 bestIndex = 0;
	for (int i = 0; i < 256; i++) {
		const rgb_color& c = kSystemPalette[i];
		// Weighted euclidean; the eye is most sensitive to green.
		int32 dr = (int32)c.red - r;
		int32 dg = (int32)c.green - g;
		int32 db = (int32)c.blue - b;
		uint32 d = 3 * dr * dr + 6 * dg * dg + 2 * db * db;
		if (d < best) {
			best = d;
			bestIndex = (uint8)i;
		}
	}
	return bestIndex;
}


// SVG-only themes (featherpad ships nothing but scalable/): rasterize via
// rsvg-convert when it is installed. Kept external on purpose — pulling an
// SVG renderer into the tree for a stamping tool would be out of proportion.
static bool
rasterizeSvg(const std::string& iconName, uint32 size, Image& img)
{
	char svgPath[512];
	snprintf(svgPath, sizeof(svgPath),
		"/usr/share/icons/hicolor/scalable/apps/%s.svg", iconName.c_str());
	FILE* probe = fopen(svgPath, "r");
	if (probe == NULL)
		return false;
	fclose(probe);

	char tmpPath[64];
	snprintf(tmpPath, sizeof(tmpPath), "/tmp/appstamp-%d-%u.png",
		(int)getpid(), size);
	char cmd[1200];
	snprintf(cmd, sizeof(cmd),
		"rsvg-convert -w %u -h %u -o %s '%s' 2>/dev/null", size, size,
		tmpPath, svgPath);
	int rc = system(cmd);
	if (rc != 0) {
		fprintf(stderr, "  found %s but rsvg-convert is unavailable —"
			" install librsvg2-bin\n", svgPath);
		return false;
	}
	bool ok = loadPng(tmpPath, img);
	unlink(tmpPath);
	return ok;
}


static std::vector<uint8>
quantizeToCMAP8(const Image& img)
{
	std::vector<uint8> out(img.width * img.height);
	const uint8* p = img.rgba.data();
	for (size_t i = 0; i < out.size(); i++, p += 4) {
		out[i] = p[3] < 128
			? B_TRANSPARENT_MAGIC_CMAP8
			: nearestPaletteIndex(p[0], p[1], p[2]);
	}
	return out;
}


// ---- stamping --------------------------------------------------------------

static status_t
writeIconAttr(BNode& node, const char* attr, type_code type, uint32 size,
	const Image& source, bool dryRun)
{
	Image scaled = (source.width == size && source.height == size)
		? source : scaleTo(source, size);
	std::vector<uint8> cmap = quantizeToCMAP8(scaled);

	if (dryRun) {
		printf("  would write %s (%lu bytes)\n", attr, (unsigned long)cmap.size());
		return B_OK;
	}
	ssize_t written = node.WriteAttr(attr, type, 0, cmap.data(), cmap.size());
	if (written < 0)
		return (status_t)written;
	printf("  %s: %ux%u B_CMAP8\n", attr, size, size);
	return B_OK;
}


struct Options {
	std::string signature;	// override, else derived from the leaf name
	std::string name;		// override, else the .desktop Name=
	std::string hvifPath;	// optional BEOS:ICON source
	bool dryRun = false;
	bool force = false;		// in --all: restamp binaries already stamped
};


// Stamp one binary from an already-resolved .desktop entry. Returns 0 on
// success. Never fatal on a single missing piece: a binary with no icon is
// still worth its name and signature.
static int
stampBinary(const std::string& binary, const DesktopEntry& entry,
	const Options& opts)
{
	BNode node(binary.c_str());
	if (node.InitCheck() != B_OK) {
		fprintf(stderr, "appstamp: cannot open %s\n", binary.c_str());
		return 1;
	}

	const std::string leaf = leafName(binary);
	std::string signature = opts.signature.empty()
		? "application/x-vnd.vos-" + leaf : opts.signature;
	std::string name = !opts.name.empty() ? opts.name
		: (entry.name.empty() ? leaf : entry.name);

	// Raster icons: theme PNGs first, SVG rasterization as fallback.
	if (!entry.icon.empty()) {
		std::string png32, png16;
		Image img32, img16;
		bool have32 = false, have16 = false;

		if (findThemePng(entry.icon, 32, png32) && loadPng(png32, img32)) {
			have32 = true;
			printf("  icon source: %s (%ux%u)\n", png32.c_str(),
				img32.width, img32.height);
			// A separate 16px file usually exists and looks better than a
			// downscale of the large art; fall back to scaling img32.
			have16 = findThemePng(entry.icon, 16, png16) && png16 != png32
				&& loadPng(png16, img16);
		} else if (rasterizeSvg(entry.icon, 32, img32)) {
			have32 = true;
			printf("  icon source: scalable SVG via rsvg-convert\n");
			have16 = rasterizeSvg(entry.icon, 16, img16);
		}

		if (have32) {
			writeIconAttr(node, kLargeAttr, B_LARGE_ICON_TYPE, 32, img32,
				opts.dryRun);
			writeIconAttr(node, kMiniAttr, B_MINI_ICON_TYPE, 16,
				have16 ? img16 : img32, opts.dryRun);
		} else {
			fprintf(stderr, "  no usable icon for '%s'\n",
				entry.icon.c_str());
		}
	}

	// Optional crisp vector icon.
	if (!opts.hvifPath.empty()) {
		FILE* f = fopen(opts.hvifPath.c_str(), "rb");
		if (f == NULL) {
			fprintf(stderr, "appstamp: cannot read %s\n",
				opts.hvifPath.c_str());
			return 1;
		}
		std::vector<uint8> hvif;
		uint8 buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
			hvif.insert(hvif.end(), buf, buf + n);
		fclose(f);
		if (hvif.size() > kMaxAttrPayload) {
			fprintf(stderr, "appstamp: %s is %zu bytes; attribute values are"
				" capped near 4K (single ext4 xattr, no chunking)\n",
				opts.hvifPath.c_str(), hvif.size());
			return 1;
		}
		if (opts.dryRun)
			printf("  would write %s (%zu bytes)\n", kVectorAttr, hvif.size());
		else if (node.WriteAttr(kVectorAttr, B_VECTOR_ICON_TYPE, 0,
				hvif.data(), hvif.size()) >= 0)
			printf("  %s: HVIF, %zu bytes\n", kVectorAttr, hvif.size());
	}

	// Via BNode, not BAppFileInfo: the latter needs a B_READ_WRITE BFile,
	// which fails with ETXTBSY while the app runs. Wire format matches
	// BAppFileInfo (trailing NUL included). SYS:NAME is
	// <sig minus "application/">:<context>:<label>; with no catalog installed
	// BCatalog returns the label verbatim.
	std::string sigLeaf = signature;
	if (sigLeaf.rfind("application/", 0) == 0)
		sigLeaf = sigLeaf.substr(strlen("application/"));
	std::string catalogEntry = sigLeaf + ":System name:" + name;

	if (opts.dryRun) {
		printf("  would set signature %s\n", signature.c_str());
		printf("  would set SYS:NAME %s\n", catalogEntry.c_str());
		return 0;
	}

	if (node.WriteAttr("BEOS:APP_SIG", B_MIME_STRING_TYPE, 0,
			signature.c_str(), signature.size() + 1) >= 0)
		printf("  BEOS:APP_SIG: %s\n", signature.c_str());
	else
		fprintf(stderr, "  failed to set signature\n");

	if (node.WriteAttr("SYS:NAME", B_STRING_TYPE, 0, catalogEntry.c_str(),
			catalogEntry.size() + 1) >= 0)
		printf("  SYS:NAME: %s\n", catalogEntry.c_str());

	return 0;
}


// Resolve the executable a .desktop Exec= line runs. Absolute path verbatim,
// otherwise the first PATH-like location. Field codes (%f, %u) live past the
// first word, which is all execBasename keeps.
static std::string
resolveExec(const std::string& execLine)
{
	std::string first = execLine.substr(0, execLine.find(' '));
	if (first.empty())
		return std::string();
	// Shell/wrapper launchers (`sh -c '…'`, `env FOO=bar app`) hide the real
	// binary past interpreter arguments we cannot parse reliably; the leaf is
	// the interpreter, not the app. Refuse rather than stamp /usr/bin/sh.
	std::string leaf = leafName(first);
	if (leaf == "sh" || leaf == "bash" || leaf == "dash" || leaf == "env")
		return std::string();
	if (first[0] == '/')
		return first;
	static const char* kBinDirs[] = {"/usr/bin", "/usr/local/bin", "/bin"};
	for (const char* dir : kBinDirs) {
		std::string path = std::string(dir) + "/" + leafName(first);
		if (access(path.c_str(), X_OK) == 0)
			return path;
	}
	return std::string();
}


// Already stamped? Cheap idempotence for --all: a binary carrying BEOS:APP_SIG
// is skipped unless --force. Not mtime-aware — a package that ships a new icon
// under the same binary needs --force — but it keeps repeated sweeps (every
// package install, via the trigger) close to free.
static bool
alreadyStamped(const std::string& binary)
{
	BNode node(binary.c_str());
	if (node.InitCheck() != B_OK)
		return false;
	attr_info info;
	return node.GetAttrInfo("BEOS:APP_SIG", &info) == B_OK;
}


// Stamp every application described under /usr/share/applications. One process
// for the whole set: the desktop directories and icon theme are scanned once,
// and a broken entry drops through to the next instead of aborting.
static int
stampAll(const Options& opts)
{
	static const char* kDirs[] = {
		"/usr/share/applications",
		"/usr/local/share/applications",
	};
	int stamped = 0, skipped = 0, failed = 0;

	for (const char* dirPath : kDirs) {
		DIR* dir = opendir(dirPath);
		if (dir == NULL)
			continue;
		struct dirent* ent;
		while ((ent = readdir(dir)) != NULL) {
			std::string fname(ent->d_name);
			if (fname.size() < 9
				|| fname.compare(fname.size() - 8, 8, ".desktop") != 0)
				continue;

			DesktopEntry entry;
			// Empty wantedLeaf: accept the file, then resolve its own Exec.
			if (!parseDesktopFile(std::string(dirPath) + "/" + fname, entry,
					""))
				continue;
			// Windowed applications only: skip hidden launchers, terminal
			// utilities (fortune's `Exec=sh -c …` would otherwise stamp
			// /usr/bin/sh) and non-Application types (links, directories).
			if (entry.noDisplay || entry.terminal || entry.consoleOnly
					|| !entry.isApplication)
				continue;
			if (entry.exec.empty())
				continue;
			std::string binary = resolveExec(entry.exec);
			if (binary.empty())
				continue;

			if (!opts.force && alreadyStamped(binary)) {
				skipped++;
				continue;
			}
			printf("%s: from %s\n", binary.c_str(), entry.path.c_str());
			if (stampBinary(binary, entry, opts) == 0)
				stamped++;
			else
				failed++;
		}
		closedir(dir);
	}

	printf("appstamp --all: %d stamped, %d already done, %d failed\n",
		stamped, skipped, failed);
	// A sweep that could not stamp anything but tried is still a success for
	// a package trigger; only signal failure if something actively broke.
	return failed > 0 ? 1 : 0;
}


static void
usage(FILE* out)
{
	fputs("usage: appstamp [options] <binary>\n"
		"       appstamp --all [options]\n"
		"Stamp BeOS attributes (icon, signature, catalog name) onto a Linux\n"
		"binary from its freedesktop .desktop metadata.\n\n"
		"  --all               stamp every app under /usr/share/applications\n"
		"  --force             with --all, restamp even if already stamped\n"
		"  --desktop <file>    use this .desktop instead of searching\n"
		"  --signature <mime>  override the MIME signature\n"
		"  --name <text>       override the display name\n"
		"  --hvif <file>       also stamp BEOS:ICON from an HVIF file\n"
		"  --dry-run           report without writing\n"
		"  --remove            remove previously stamped attributes\n", out);
}


int
main(int argc, char** argv)
{
	std::string binary, desktopPath;
	Options opts;
	bool remove = false;
	bool all = false;

	for (int i = 1; i < argc; i++) {
		std::string arg(argv[i]);
		if (arg == "--desktop" && i + 1 < argc)
			desktopPath = argv[++i];
		else if (arg == "--signature" && i + 1 < argc)
			opts.signature = argv[++i];
		else if (arg == "--name" && i + 1 < argc)
			opts.name = argv[++i];
		else if (arg == "--hvif" && i + 1 < argc)
			opts.hvifPath = argv[++i];
		else if (arg == "--dry-run")
			opts.dryRun = true;
		else if (arg == "--force")
			opts.force = true;
		else if (arg == "--all")
			all = true;
		else if (arg == "--remove")
			remove = true;
		else if (arg == "--help" || arg == "-h") {
			usage(stdout);
			return 0;
		} else if (!arg.empty() && arg[0] == '-') {
			fprintf(stderr, "appstamp: unknown option %s\n", arg.c_str());
			usage(stderr);
			return 1;
		} else
			binary = arg;
	}

	if (all)
		return stampAll(opts);

	if (binary.empty()) {
		usage(stderr);
		return 1;
	}

	if (remove) {
		BNode node(binary.c_str());
		if (node.InitCheck() != B_OK) {
			fprintf(stderr, "appstamp: cannot open %s\n", binary.c_str());
			return 1;
		}
		static const char* kAll[] = {kMiniAttr, kLargeAttr, kVectorAttr,
			"BEOS:APP_SIG", "SYS:NAME"};
		for (const char* attr : kAll) {
			if (node.RemoveAttr(attr) == B_OK)
				printf("  removed %s\n", attr);
		}
		return 0;
	}

	// Metadata: explicit .desktop (no Exec match required — the caller chose
	// the file), or search /usr/share/applications by the binary's leaf name.
	DesktopEntry entry;
	bool haveDesktop = false;
	if (!desktopPath.empty()) {
		haveDesktop = parseDesktopFile(desktopPath, entry, "");
		if (!haveDesktop) {
			fprintf(stderr, "appstamp: cannot read %s\n", desktopPath.c_str());
			return 1;
		}
	} else {
		haveDesktop = findDesktopFor(binary, entry);
	}

	if (haveDesktop)
		printf("%s: using %s\n", binary.c_str(), entry.path.c_str());
	else if (opts.name.empty() && opts.hvifPath.empty()) {
		fprintf(stderr, "appstamp: no .desktop entry found for %s\n"
			"(searched /usr/share/applications; use --desktop, or --name/"
			"--hvif to stamp explicitly)\n", binary.c_str());
		return 1;
	}

	return stampBinary(binary, entry, opts);
}
