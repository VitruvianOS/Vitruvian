/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AptCacheAdapter.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <apt-pkg/cachefile.h>
#include <apt-pkg/debversion.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/error.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/policy.h>


static const char* kDpkg = "/usr/bin/dpkg";
static const char* kAptGet = "/usr/bin/apt-get";
static const char* kZcat = "/bin/zcat";

static const char* kAptHistoryLog = "/var/log/apt/history.log";
static const char* kAptTermLog = "/var/log/apt/term.log";


apt_raw_package::apt_raw_package()
	:
	installedSize(0),
	downloadSize(0),
	state(V_PACKAGE_STATE_UNKNOWN),
	channel(V_CHANNEL_UNKNOWN)
{
}


// ReadOnlyOpen() is deliberate: never take the dpkg/apt lock, so this
// cannot collide with a concurrent apt-get or vos-apt-helper.
class AptCacheAdapter::CacheHandle {
public:
	CacheHandle()
		:
		fRecords(NULL),
		fOk(false)
	{
		if (!pkgInitConfig(*_config) || !pkgInitSystem(*_config, _system)) {
			_error->DumpErrors();
			return;
		}
		if (!fFile.ReadOnlyOpen(NULL)) {
			_error->DumpErrors();
			return;
		}
		fRecords = new pkgRecords(*fFile.GetPkgCache());
		fOk = true;
	}

	~CacheHandle()
	{
		delete fRecords;
	}

	bool Ok() const { return fOk; }
	pkgCache* Cache() { return fFile.GetPkgCache(); }
	pkgDepCache* DepCache() { return fFile.GetDepCache(); }
	pkgRecords* Records() { return fRecords; }

private:
	pkgCacheFile	fFile;
	pkgRecords*		fRecords;
	bool			fOk;
};


AptCacheAdapter::AptCacheAdapter()
	:
	fCache(NULL)
{
}


AptCacheAdapter::~AptCacheAdapter()
{
	delete fCache;
}


bool
AptCacheAdapter::_EnsureCacheOpen()
{
	if (fCache == NULL)
		fCache = new CacheHandle();
	return fCache->Ok();
}


void
AptCacheAdapter::InvalidateCache()
{
	delete fCache;
	fCache = NULL;
}


// DepCompareOp's order is NOT "<, <=, =, >=, >"; see pkgcache.h.
static v_dependency_operator
map_compare_op(unsigned char compareOp)
{
	switch (compareOp & 0xF) {
		case pkgCache::Dep::LessEq:		return V_DEPENDENCY_OP_LE;
		case pkgCache::Dep::GreaterEq:		return V_DEPENDENCY_OP_GE;
		case pkgCache::Dep::Less:			return V_DEPENDENCY_OP_LT;
		case pkgCache::Dep::Greater:		return V_DEPENDENCY_OP_GT;
		case pkgCache::Dep::Equals:			return V_DEPENDENCY_OP_EQ;
		case pkgCache::Dep::NotEquals:		return V_DEPENDENCY_OP_NE;
		default:							return V_DEPENDENCY_OP_NONE;
	}
}


static const char*
op_text(v_dependency_operator op)
{
	switch (op) {
		case V_DEPENDENCY_OP_LT:	return "<<";
		case V_DEPENDENCY_OP_LE:	return "<=";
		case V_DEPENDENCY_OP_EQ:	return "=";
		case V_DEPENDENCY_OP_GE:	return ">=";
		case V_DEPENDENCY_OP_GT:	return ">>";
		case V_DEPENDENCY_OP_NE:	return "!=";
		default:					return NULL;
	}
}


// Round-trips control-file text (GlobOr() marks the "a | b" groups) so
// no libapt type escapes this file and ParseList() stays the one parser.
static void
format_dependencies(pkgCache::VerIterator ver, BString* depends,
	BString* recommends, BString* breaks, BString* conflicts)
{
	for (pkgCache::DepIterator dep = ver.DependsList(); !dep.end(); ) {
		pkgCache::DepIterator start, end;
		dep.GlobOr(start, end);

		BString* bucket = NULL;
		switch (start->Type) {
			case pkgCache::Dep::Depends:
			case pkgCache::Dep::PreDepends:
				bucket = depends;
				break;
			case pkgCache::Dep::Recommends:
				bucket = recommends;
				break;
			case pkgCache::Dep::DpkgBreaks:
				bucket = breaks;
				break;
			case pkgCache::Dep::Conflicts:
				bucket = conflicts;
				break;
			default:
				// Suggests/Replaces/Obsoletes/Enhances are outside this kit.
				continue;
		}

		BString group;
		for (pkgCache::DepIterator cur = start; ; ++cur) {
			if (group.Length() > 0)
				group << " | ";
			group << cur.TargetPkg().Name();

			const char* version = cur.TargetVer();
			const char* opText = op_text(map_compare_op(cur->CompareOp));
			if (version != NULL && opText != NULL)
				group << " (" << opText << " " << version << ")";

			if (cur == end)
				break;
		}

		if (bucket->Length() > 0)
			*bucket << ", ";
		*bucket << group;
	}
}


static void
format_provides(pkgCache::VerIterator ver, BString* provides)
{
	for (pkgCache::PrvIterator prv = ver.ProvidesList(); !prv.end(); ++prv) {
		if (provides->Length() > 0)
			*provides << ", ";
		*provides << prv.Name();
		if (prv.ProvideVersion() != NULL)
			*provides << " (= " << prv.ProvideVersion() << ")";
	}
}


// Must mirror AptBackend::_ResolveChannel()'s substring rules exactly.
static v_package_channel
resolve_channel(pkgCache::VerIterator ver)
{
	if (ver.end())
		return V_CHANNEL_UNKNOWN;

	bool sawTrixie = false;
	for (pkgCache::VerFileIterator vf = ver.FileList(); !vf.end(); ++vf) {
		const char* codename = vf.File().Codename();
		if (codename == NULL)
			continue;
		BString name(codename);
		if (name.FindFirst("trixie-nightly") >= 0)
			return V_CHANNEL_NIGHTLY;
		if (name.FindFirst("trixie-testing") >= 0)
			return V_CHANNEL_TESTING;
		if (name.FindFirst("trixie") >= 0)
			sawTrixie = true;
	}
	if (sawTrixie)
		return V_CHANNEL_STABLE;
	return V_CHANNEL_DEBIAN;
}


status_t
AptCacheAdapter::GetPackageList(BObjectList<apt_raw_package, true>* packages)
{
	if (packages == NULL)
		return B_BAD_VALUE;
	if (!_EnsureCacheOpen())
		return B_ERROR;

	pkgCache* cache = fCache->Cache();
	pkgDepCache* depCache = fCache->DepCache();
	pkgRecords* records = fCache->Records();

	for (pkgCache::PkgIterator pkg = cache->PkgBegin(); !pkg.end(); ++pkg) {
		pkgCache::VerIterator curVer = pkg.CurrentVer();
		pkgDepCache::StateCache& state = (*depCache)[pkg];
		pkgCache::VerIterator candVer = state.CandidateVerIter(*cache);

		// Purely virtual packages have no versions of their own.
		if (curVer.end() && candVer.end())
			continue;

		apt_raw_package* raw = new apt_raw_package();
		raw->name = pkg.Name();
		raw->architecture = pkg.Arch() != NULL ? pkg.Arch() : "";

		if (!curVer.end()) {
			raw->installedVersion = curVer.VerStr();
			raw->state = state.Upgradable()
				? V_PACKAGE_STATE_UPGRADABLE : V_PACKAGE_STATE_INSTALLED;
		} else {
			raw->state = V_PACKAGE_STATE_AVAILABLE;
		}

		pkgCache::VerIterator best = !candVer.end() ? candVer : curVer;
		if (!best.end()) {
			raw->candidateVersion = candVer.end() ? "" : candVer.VerStr();
			raw->section = best.Section() != NULL ? best.Section() : "";
			raw->installedSize = (off_t)best->InstalledSize;
			raw->downloadSize = (off_t)best->Size;
			raw->channel = resolve_channel(best);

			pkgCache::VerFileIterator vf = best.FileList();
			if (!vf.end()) {
				pkgRecords::Parser& parser = records->Lookup(vf);
				raw->summary = parser.ShortDesc("").c_str();
			}
		}

		packages->AddItem(raw);
	}

	return B_OK;
}


status_t
AptCacheAdapter::GetPackageDetails(const char* name, apt_raw_package* out)
{
	if (name == NULL || out == NULL)
		return B_BAD_VALUE;
	if (!_EnsureCacheOpen())
		return B_ERROR;

	pkgCache* cache = fCache->Cache();
	pkgDepCache* depCache = fCache->DepCache();
	pkgRecords* records = fCache->Records();

	pkgCache::PkgIterator pkg = cache->FindPkg(name);
	if (pkg.end())
		return B_ENTRY_NOT_FOUND;

	pkgCache::VerIterator curVer = pkg.CurrentVer();
	pkgDepCache::StateCache& state = (*depCache)[pkg];
	pkgCache::VerIterator candVer = state.CandidateVerIter(*cache);
	pkgCache::VerIterator best = !candVer.end() ? candVer : curVer;
	if (best.end())
		return B_ENTRY_NOT_FOUND;

	out->name = name;
	out->architecture = pkg.Arch() != NULL ? pkg.Arch() : "";
	if (!curVer.end())
		out->installedVersion = curVer.VerStr();
	if (!candVer.end())
		out->candidateVersion = candVer.VerStr();
	out->state = curVer.end() ? V_PACKAGE_STATE_AVAILABLE
		: (state.Upgradable() ? V_PACKAGE_STATE_UPGRADABLE
			: V_PACKAGE_STATE_INSTALLED);
	out->section = best.Section() != NULL ? best.Section() : "";
	out->installedSize = (off_t)best->InstalledSize;
	out->downloadSize = (off_t)best->Size;
	out->channel = resolve_channel(best);

	pkgCache::VerFileIterator vf = best.FileList();
	if (!vf.end()) {
		pkgRecords::Parser& parser = records->Lookup(vf);
		out->summary = parser.ShortDesc("").c_str();
		out->description = parser.LongDesc("").c_str();
	}

	format_dependencies(best, &out->depends, &out->recommends, &out->breaks,
		&out->conflicts);
	format_provides(best, &out->provides);

	return B_OK;
}


status_t
AptCacheAdapter::GetPackageContents(const char* name,
	BObjectList<BString, true>* paths)
{
	if (name == NULL || paths == NULL)
		return B_BAD_VALUE;

	// The list lives in /var/lib/dpkg/info, not the cache; dpkg -L's
	// non-zero exit for an uninstalled package is not an error.
	static const char* const kListArgv[] = { kDpkg, "-L", name, NULL };

	BObjectList<BString, true> lines(256);
	if (_RunQuery(kListArgv, &lines) != B_OK)
		return B_OK;

	for (int32 i = 0; i < lines.CountItems(); i++) {
		const BString& line = *lines.ItemAt(i);
		if (line.Length() > 0 && line[0] == '/')
			paths->AddItem(new BString(line));
	}
	return B_OK;
}


status_t
AptCacheAdapter::GetAptLog(BString* text)
{
	if (text == NULL)
		return B_BAD_VALUE;

	BString history;
	BString term;
	const bool haveHistory = _ReadFile(kAptHistoryLog, &history);
	const bool haveTerm = _ReadFile(kAptTermLog, &term);

	if (!haveHistory && !haveTerm) {
		text->SetTo("No apt activity recorded yet.");
		return B_OK;
	}

	text->SetTo("");
	if (haveHistory) {
		*text << "=== " << kAptHistoryLog << " (newest first) ===\n\n";
		_AppendRecordsReversed(history, text);
	}
	if (haveTerm) {
		if (text->Length() > 0)
			*text << "\n";
		*text << "=== " << kAptTermLog << " ===\n\n" << term;
	}
	return B_OK;
}


status_t
AptCacheAdapter::GetChangelog(const char* name, BString* out)
{
	if (name == NULL || out == NULL)
		return B_BAD_VALUE;

	// A package name never contains '/'; reject rather than let it steer
	// the paths below, the same discipline as the privileged helper side.
	if (strchr(name, '/') != NULL) {
		out->SetTo("No changelog available.");
		return B_OK;
	}

	if (_ReadLocalChangelog(name, out))
		return B_OK;

	// Second tier: the repository copy. apt-get changelog downloads
	// read-only as the calling user; no dpkg lock, no helper.
	const char* const argv[] = { kAptGet, "changelog", "--", name, NULL };
	BObjectList<BString, true> lines(128);
	if (_RunQuery(argv, &lines) != B_OK) {
		out->SetTo("No changelog available.");
		return B_OK;
	}

	out->SetTo("");
	for (int32 i = 0; i < lines.CountItems(); i++)
		*out << *lines.ItemAt(i) << "\n";
	_TruncateChangelog(out);
	return B_OK;
}


bool
AptCacheAdapter::_ReadLocalChangelog(const char* name, BString* out)
{
	// No arch just means that filename candidate is skipped, not a failure.
	BString arch;
	if (_EnsureCacheOpen()) {
		pkgCache::PkgIterator pkg = fCache->Cache()->FindPkg(name);
		if (!pkg.end() && pkg.Arch() != NULL)
			arch = pkg.Arch();
	}

	BString candidates[3];
	candidates[0].SetToFormat("/usr/share/doc/%s/changelog.Debian.gz", name);
	if (arch.Length() > 0) {
		candidates[1].SetToFormat("/usr/share/doc/%s/changelog.Debian.%s.gz",
			name, arch.String());
	}
	candidates[2].SetToFormat("/usr/share/doc/%s/changelog.gz", name);

	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		if (candidates[i].Length() == 0
			|| access(candidates[i].String(), R_OK) != 0) {
			continue;
		}

		const char* const argv[] = { kZcat, candidates[i].String(), NULL };
		BObjectList<BString, true> lines(128);
		if (_RunQuery(argv, &lines) != B_OK)
			continue;

		out->SetTo("");
		for (int32 j = 0; j < lines.CountItems(); j++)
			*out << *lines.ItemAt(j) << "\n";
		_TruncateChangelog(out);
		return true;
	}
	return false;
}


void
AptCacheAdapter::_TruncateChangelog(BString* text)
{
	if (text->Length() <= kMaxChangelogBytes)
		return;

	BString tail;
	tail.SetToFormat("\n\n[%d more bytes not shown]",
		(int)(text->Length() - kMaxChangelogBytes));
	text->Truncate(kMaxChangelogBytes);
	*text << tail;
}


int
AptCacheAdapter::CompareVersions(const char* a, const char* b)
{
	if (a == NULL || b == NULL)
		return 0;
	// apt's own comparator, called not reimplemented; needs no init.
	return debVS.CmpVersion(a, b);
}


bool
AptCacheAdapter::_ReadFile(const char* path, BString* out)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	char buffer[4096];
	size_t bytesRead;
	while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
		out->Append(buffer, bytesRead);
	fclose(file);
	return true;
}


// Reverse by record (Start-Date:-delimited), never by line.
void
AptCacheAdapter::_AppendRecordsReversed(const BString& log, BString* out)
{
	BObjectList<BString, true> records(64);
	BString current;

	int32 pos = 0;
	while (pos < log.Length()) {
		int32 newline = log.FindFirst('\n', pos);
		if (newline < 0)
			newline = log.Length();
		BString line;
		log.CopyInto(line, pos, newline - pos);
		pos = newline + 1;

		if (line.StartsWith("Start-Date:") && current.Length() > 0) {
			records.AddItem(new BString(current));
			current.SetTo("");
		}
		current << line << "\n";
	}
	if (current.Length() > 0)
		records.AddItem(new BString(current));

	for (int32 i = records.CountItems() - 1; i >= 0; i--)
		*out << *records.ItemAt(i) << "\n";
}


static void
collect_line(const BString& line, void* cookie)
{
	((BObjectList<BString, true>*)cookie)->AddItem(new BString(line));
}


// The child env is built, not inherited; argv[0] must be absolute.
status_t
AptCacheAdapter::_RunQuery(const char* const argv[],
	BObjectList<BString, true>* lines)
{
	if (argv == NULL || argv[0] == NULL || lines == NULL)
		return B_BAD_VALUE;

	int pipeFds[2];
	if (pipe(pipeFds) != 0)
		return B_ERROR;

	pid_t child = fork();
	if (child < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return B_ERROR;
	}

	if (child == 0) {
		static char* const kChildEnv[] = {
			(char*)"LANG=C",
			(char*)"LC_ALL=C",
			(char*)"PATH=/usr/bin:/bin",
			NULL
		};
		close(pipeFds[0]);
		if (dup2(pipeFds[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(pipeFds[1]);
		int devNull = open("/dev/null", O_WRONLY);
		if (devNull >= 0) {
			dup2(devNull, STDERR_FILENO);
			close(devNull);
		}
		execve(argv[0], (char* const*)argv, kChildEnv);
		_exit(127);
	}

	close(pipeFds[1]);

	BString pending;
	char buffer[4096];
	ssize_t bytesRead;
	while ((bytesRead = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
		int32 start = 0;
		for (ssize_t i = 0; i < bytesRead; i++) {
			if (buffer[i] != '\n')
				continue;
			pending.Append(buffer + start, i - start);
			collect_line(pending, lines);
			pending.SetTo("");
			start = i + 1;
		}
		if (start < bytesRead)
			pending.Append(buffer + start, bytesRead - start);
	}
	if (pending.Length() > 0)
		collect_line(pending, lines);

	close(pipeFds[0]);

	int status = 0;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		;

	if (bytesRead < 0)
		return B_ERROR;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return B_ERROR;
	return B_OK;
}
