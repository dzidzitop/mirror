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
#include <string>

namespace mirror
{
	void createDB(const char *rootDir, mirror::FileDB &db);

	template<typename MismatchHandler>
	void verifyDir(const char *rootDir, mirror::FileDB &db, MismatchHandler &mismatchHandler);

	namespace _helper
	{
		using afc::operator"" _s;
		using afc::logger::logDebug;
		using afc::logger::logError;

		[[noreturn]]
		void handleOpenFileError(int errorCode);
		[[noreturn]]
		void handleReadFileError(int errorCode);

		std::FILE *openFile(const char * const path, const char * const mode);

		template<typename ChunkOp>
		void processFile(const char * const path, ChunkOp &chunkOp);

		template<typename EventHandler>
		void scanFiles(afc::FastStringBuffer<char> &rootDir, const std::size_t relDirOffset, EventHandler &eventHandler);

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

		void fillFileRecord(const char * const filePath, mirror::FileRecord &dest);
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
void mirror::verifyDir(const char *rootDir, mirror::FileDB &db, MismatchHandler &mismatchHandler)
{
	using afc::operator"" _s;
	using afc::logger::logDebug;
	using afc::logger::logError;

	// TODO track removed empty dirs.
	struct EventHandler
	{
		EventHandler(mirror::FileDB &db) : dbDirs(), ctxs(), dbRef(db) { db.getDirs(dbDirs); }

		void dirStart(const char * const relDir)
		{
			// TODO do not convert relative dir again and again for every file in the directory.
			// TODO avoid strlen.
			const TextHolder relDirU8 = mirror::convertToUtf8(relDir, std::strlen(relDir));

			dbDirs.erase(PathKey(relDirU8.value, relDirU8.size, true));

			ctxs.emplace();
			// sqlite3 does not handle nullptr + size(0) as an empty string do relDirU8.data() does not work.
			dbRef.getFiles(relDirU8.value, relDirU8.size, ctxs.top());
		}

		void dirEnd(const char * const relDir)
		{
			for (auto &e : ctxs.top()) {
				// TODO avoid unnecessary memory allocations.
				std::string relativePath(relDir);
				if (relDir[0] != '\0') {
					relativePath += '/';
				}

				const TextHolder buf = mirror::convertFromUtf8(e.first.data, e.first.size);
				relativePath.append(buf.value, buf.size);

				logError("File not found in the file system: '"_s, relativePath.c_str(), "'!"_s);
			}
			ctxs.pop();
		}

		void file(const char * const rootDir, const char * const relDir, const char * const fileName)
		{
			using MD5View = afc::logger::HexEncodedN<MD5_DIGEST_LENGTH>;

			// TODO avoid strlen.
			const std::size_t fileNameSize = std::strlen(fileName);
			const RelPathView relPathView(relDir, std::strlen(relDir), fileName, fileNameSize);

			logDebug("Checking the file '"_s, fileName, "'..."_s);

			// TODO avoid unnecessary memory allocations.
			const std::string absolutePath = (std::string(rootDir) + '/') + fileName;

			const TextHolder buf = mirror::convertToUtf8(fileName, fileNameSize);
			const auto dbEntry = ctxs.top().find(PathKey(buf.value, buf.size, true));

			if (dbEntry == ctxs.top().end()) {
				logError("New file found in the file system: '"_s, relPathView, "'!"_s);
				return;
			}

			const mirror::FileRecord &expectedFileRecord = dbEntry->second;
			mirror::FileRecord fileRecord;

			mirror::_helper::fillFileRecord(absolutePath.c_str(), fileRecord);

			if (expectedFileRecord.fileSize != fileRecord.fileSize) {
				logError("File size mismatch for the file '"_s, relPathView, "'! DB size: "_s,
						expectedFileRecord.fileSize, ", file system size: "_s, fileRecord.fileSize, '.');
			}
			if (expectedFileRecord.lastModifiedTS.millis() != fileRecord.lastModifiedTS.millis()) {
				logError("File last modified timestamp mismatch for the file '"_s, relPathView, "'! DB timestamp: "_s,
						afc::ISODateTimeView(expectedFileRecord.lastModifiedTS), ", file system timestamp: "_s,
						afc::ISODateTimeView(fileRecord.lastModifiedTS), '.');
			}
			if (!std::equal(fileRecord.md5Digest, fileRecord.md5Digest + MD5_DIGEST_LENGTH,
					expectedFileRecord.md5Digest)) {
				logError("File MD5 digest mismatch for the file '"_s, relPathView,
						"'! DB MD5: '"_s, MD5View(expectedFileRecord.md5Digest),
						"', file system MD5: '"_s, MD5View(fileRecord.md5Digest), "'."_s);
			}

			ctxs.top().erase(dbEntry);
		}

		mirror::DirSet dbDirs;
		std::stack<mirror::DirFileMap> ctxs;
		mirror::FileDB &dbRef;
	} eventHandler(db);

	// TODO avoid strlen.
	mirror::_helper::scanFiles(rootDir, std::strlen(rootDir), eventHandler);

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

template<typename EventHandler>
void mirror::_helper::scanFiles(afc::FastStringBuffer<char> &rootDir, const std::size_t relDirOffset,
		EventHandler &eventHandler)
{
	logDebug("Scanning '"_s, rootDir, "'..."_s);
	DIR *dir = opendir(rootDir.c_str());

	if (dir == nullptr) {
		switch (errno) {
		case EACCES:
			// TODO make the behaviour configurable.
			logDebug("No access to '"_s, rootDir, '\'');
			return;
		default:
			// TODO handle error
			throw errno;
		}
	}

	assert(*rootDir.end() == '\0');
	eventHandler.dirStart(rootDir.data() + relDirOffset);

	dirent *file;
	// TODO handle errors.
	while ((file = readdir(dir)) != nullptr) {
		const char *name;
		switch (file->d_type) {
		case DT_REG: {
			eventHandler.file(rootDir.c_str(), rootDir.data() + relDirOffset, file->d_name);
			break;
		}
		case DT_DIR:
			name = file->d_name;
			if (name[0] == '.') {
				if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')) {
					// Either the current dir or the parent dir. Skipping it.
					continue;
				}
			}
			{
				const std::size_t nameSize = std::strlen(name);
				rootDir.reserve(rootDir.size() + nameSize + 1);

				rootDir.append('/');
				rootDir.append(name, nameSize);
				// TODO avoid unnecessary memory allocations.
				scanFiles(rootDir, relDirOffset + (rootDir.c_str()[relDirOffset] == '/' ? 1 : 0), eventHandler);

				// Rolling back the dir path buffer to the current dir.
				rootDir.resize(rootDir.size() - nameSize - 1);
			}
			break;
		default:
			// TODO support filesystems that do not support returning file type in d_type.
			logDebug("The file '"_s, file->d_name, "' is neither a directory or a regular file. Skipping it..."_s);
			break;
		}
	}

	// TODO call closedir even if an error occurs.
	closedir(dir);

	eventHandler.dirEnd(rootDir.c_str() + relDirOffset);
}

#endif // MIRROR_UTILS_HPP_
