/* mirror - a tool to make mirrors of files or directories and to check consistency of the existing mirrors.
Copyright (C) 2017 Dźmitry Laŭčuk

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>. */
#ifndef MIRROR_UTILS_HPP_
#define MIRROR_UTILS_HPP_

#include <afc/builtin.hpp>
#include <afc/dateutil.hpp>
#include <afc/FastStringBuffer.hpp>
#include <afc/logger.hpp>
#include <afc/number.h>
#include <afc/StringRef.hpp>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <dirent.h>
#include "encoding.hpp"
#include "FileDB.hpp"
#include <stack>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>

namespace mirror
{
	void createDB(const char *rootDir, std::size_t rootDirSize, mirror::FileDB &db);

	template<typename MismatchHandler>
	void verifyDir(const char *rootDir, std::size_t rootDirSize, mirror::FileDB &db, MismatchHandler &mismatchHandler);

	namespace _helper
	{
		using afc::operator"" _s;
		using afc::logger::logDebug;
		using afc::logger::logError;

		[[noreturn]]
		void handleOpenFileError(int errorCode);
		[[noreturn]]
		void handleReadFileError(int errorCode);
		[[noreturn]]
		void handleReadDirError(int errorCode);

		std::FILE *openFile(const char * const path, const char * const mode);

		template<typename ChunkOp>
		void processFile(const char * const path, ChunkOp &chunkOp);

		template<typename EventHandler>
		void scanFiles(afc::FastStringBuffer<char> &path, std::size_t relDirOffset, EventHandler &eventHandler);

		template<typename EventHandler>
		inline void scanFiles(const char * const rootDir, const std::size_t rootDirSize, EventHandler &eventHandler)
		{
			std::size_t normalisedSize = rootDirSize;
			if (rootDir[rootDirSize - 1] == '/') {
				--normalisedSize;
			}
			afc::FastStringBuffer<char> dirBuf(normalisedSize);
			dirBuf.append(rootDir, normalisedSize);
			scanFiles(dirBuf, normalisedSize, eventHandler);
		}

		void fillFileRecord(const struct stat &fileStat, const char * const filePath, mirror::FileRecord &dest);
	}

	struct RelPathView
	{
		RelPathView(const char * const relDir, std::size_t relDirSize, const char * const fileName,
				const std::size_t fileNameSize) noexcept : dir(relDir), dirSize(relDirSize),
						file(fileName), fileSize(fileNameSize) {}

		const char *dir;
		std::size_t dirSize;
		const char *file;
		std::size_t fileSize;
	};
}

namespace afc
{
	namespace logger
	{
		template<>
		inline bool logPrint<const mirror::RelPathView &>(const mirror::RelPathView &val, std::FILE * const dest)
		{
			bool result = true;
			if (val.dirSize > 0) {
				result = logText(val.dir, val.dirSize, dest) && logText("/", 1, dest);
			}
			if (result) {
				result = logText(val.file, val.fileSize, dest);
			}
			return result;
		}
	}
}

template<typename MismatchHandler>
void mirror::verifyDir(const char * const rootDir, const std::size_t rootDirSize, mirror::FileDB &db,
		MismatchHandler &mismatchHandler)
{
	using afc::operator"" _s;
	using afc::logger::logDebug;
	using afc::logger::logError;

	// TODO track removed empty dirs.
	struct EventHandler
	{
		EventHandler(mirror::FileDB &db) : dbDirs(), ctxs(), dbRef(db) { db.getDirs(dbDirs); }

		void dirStart(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset)
		{
			const char * const relDir = path.begin() + relDirOffset;
			logDebug("Entering '"_s, std::pair<const char *, const char *>(relDir, path.end()), "'..."_s);

			const TextHolder relDirU8 = mirror::convertToUtf8(relDir, path.size() - relDirOffset);

			dbDirs.erase(PathKey(relDirU8.value, relDirU8.size, true));

			ctxs.emplace();
			dbRef.getFiles(relDirU8.value, relDirU8.size, ctxs.top());
		}

		void dirEnd(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset)
		{
			const auto &ctx = ctxs.top();
			if (!ctx.empty()) {
				const std::size_t pathSize = path.size();

				if (pathSize == 0) {
					path.reserveForOne();
					path.append('/');
				}

				for (auto &e : ctx) {
					const TextHolder buf = mirror::convertFromUtf8(e.first.data, e.first.size);
					path.append(buf.value, buf.size);

					logError("File not found in the file system: '"_s,
							std::pair<const char *, const char *>(path.data() + relDirOffset, path.end()), "'!"_s);

					path.resize(path.size() - buf.size);
				}

				path.resize(pathSize);
			}
			ctxs.pop();
		}

		void file(const struct stat &fileStat, const afc::FastStringBuffer<char> &path, const std::size_t relPathOffset,
				const std::size_t fileNameOffset)
		{
			using MD5View = afc::logger::HexEncodedN<MD5_DIGEST_LENGTH>;

			const char * const relPath = path.begin() + relPathOffset;

			logDebug("Checking the file '"_s, std::make_pair(relPath, path.end()), "'..."_s);

			const char * const fileName = path.begin() + fileNameOffset;
			const std::size_t fileNameSize = path.size() - fileNameOffset;

			const TextHolder buf = mirror::convertToUtf8(fileName, fileNameSize);
			const auto dbEntry = ctxs.top().find(PathKey(buf.value, buf.size, true));

			if (dbEntry == ctxs.top().end()) {
				logError("New file found in the file system: '"_s, std::make_pair(relPath, path.end()), "'!"_s);
				return;
			}

			const mirror::FileRecord &expectedFileRecord = dbEntry->second;
			mirror::FileRecord fileRecord;

			mirror::_helper::fillFileRecord(fileStat, path.c_str(), fileRecord);

			if (expectedFileRecord.type != fileRecord.type) {
				logError("File type mismatch for the file '"_s, std::make_pair(relPath, path.end()),
						"'! DB file type: "_s, expectedFileRecord.type, ", file system file type: "_s, fileRecord.type,
						'.');
			}
			if (expectedFileRecord.fileSize != fileRecord.fileSize) {
				logError("File size mismatch for the file '"_s, std::make_pair(relPath, path.end()), "'! DB size: "_s,
						expectedFileRecord.fileSize, ", file system size: "_s, fileRecord.fileSize, '.');
			}
			if (expectedFileRecord.lastModifiedTS.millis() != fileRecord.lastModifiedTS.millis()) {
				logError("File last modified timestamp mismatch for the file '"_s, std::make_pair(relPath, path.end()),
						"'! DB timestamp: "_s, afc::ISODateTimeView(expectedFileRecord.lastModifiedTS),
						", file system timestamp: "_s, afc::ISODateTimeView(fileRecord.lastModifiedTS), '.');
			}
			if (!std::equal(fileRecord.md5Digest, fileRecord.md5Digest + MD5_DIGEST_LENGTH,
					expectedFileRecord.md5Digest)) {
				logError("File MD5 digest mismatch for the file '"_s, std::make_pair(relPath, path.end()),
						"'! DB MD5: '"_s, MD5View(expectedFileRecord.md5Digest),
						"', file system MD5: '"_s, MD5View(fileRecord.md5Digest), "'."_s);
			}

			ctxs.top().erase(dbEntry);
		}

		mirror::DirSet dbDirs;
		std::stack<mirror::DirFileMap> ctxs;
		mirror::FileDB &dbRef;
	} eventHandler(db);

	mirror::_helper::scanFiles(rootDir, rootDirSize, eventHandler);

	// TODO pass errors to the caller.
	for (const PathKey &missingDir : eventHandler.dbDirs) {
		logDebug("DB dir not found in the file system: '"_s,
				Utf8ToSystemView(missingDir.data, missingDir.size), "'..."_s);
	}

	assert(eventHandler.ctxs.empty());
}

inline std::FILE *mirror::_helper::openFile(const char * const path, const char * const mode)
{
	std::FILE * const f = std::fopen(path, "r");
	if (likely(f != nullptr)) {
		return f;
	}

	handleOpenFileError(errno);
}

template<typename ChunkOp>
inline void mirror::_helper::processFile(const char * const path, ChunkOp &chunkOp)
{
	std::FILE * const f = openFile(path, "r");

	try {
		char buf[4096];
		for (;;) {
			const std::size_t n = fread(buf, 1, 4096, f);
			if (n == 4096) {
				chunkOp(buf, n);
			} else if (std::feof(f)) {
				chunkOp(buf, n);
				break;
			} else {
				handleReadFileError(errno);
			}
		}
	}
	catch (...) {
		if (std::fclose(f) != 0) {
			logError("Unable to close the file '"_s, path, "'.");
		}
		throw;
	}

	if (std::fclose(f) != 0) {
		// TODO handle error.
		throw errno;
	}
}

// TODO unite path and relDirOffset and eventHandler into a single structure to save some stack space.
template<typename EventHandler>
void mirror::_helper::scanFiles(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset,
		EventHandler &eventHandler)
{
	logDebug("Scanning '"_s, path, "'..."_s);
	DIR * const dir = opendir(path.c_str());

	if (dir == nullptr) {
		switch (errno) {
		case EACCES:
			// TODO make the behaviour configurable.
			logDebug("No access to '"_s, path, '\'');
			return;
		default:
			// TODO handle error
			throw errno;
		}
	}

	eventHandler.dirStart(path, relDirOffset);

	path.reserveForOne();
	path.append('/');

	dirent file;
	dirent *readdirEnd;
	int status;
	while ((status = readdir_r(dir, &file, &readdirEnd)) == 0) {
		if (readdirEnd == nullptr) {
			goto end;
		}

		register const char * const name = file.d_name;

		if (name[0] == '.') {
			if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')) {
				// Either the current dir or the parent dir. Skipping it.
				continue;
			}
		}

		const std::size_t nameSize = std::strlen(name);
		path.reserve(path.size() + nameSize);
		path.append(name, nameSize);

		struct stat fileStat;
		const int result = lstat(path.c_str(), &fileStat);
		if (result != 0) {
			switch (errno) {
			case EACCES:
				// TODO make the behaviour configurable.
				logDebug("No access to '"_s, path, '\'');
				continue;
			default:
				// TODO handle error
				logDebug(errno);
				throw errno;
			}
		}

		if (S_ISREG(fileStat.st_mode)) {
			eventHandler.file(fileStat, path, relDirOffset + (path.data()[relDirOffset] == '/' ? 1 : 0),
					path.size() - nameSize);
		} else if (S_ISDIR(fileStat.st_mode)) {
			// TODO pass event of this dir to eventHandler.
			scanFiles(path, relDirOffset + (path.data()[relDirOffset] == '/' ? 1 : 0), eventHandler);
		} else {
			// TODO support non-regular and non-directory files.
			logDebug("The file '"_s, name, "' is neither a directory or a regular file. Skipping it..."_s);
		}

		// Rolling back the dir path buffer to the current dir with slash.
		path.resize(path.size() - nameSize);
	}



end:
	// TODO call closedir even if an error occurs.
	closedir(dir);

	// Removing the trailing slash.
	path.resize(path.size() - 1);

	eventHandler.dirEnd(path, relDirOffset);
}

#endif // MIRROR_UTILS_HPP_
