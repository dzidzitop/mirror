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
#include <afc/logger.hpp>
#include <afc/number.h>
#include <afc/StringRef.hpp>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <dirent.h>
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
		void scanFiles(const char * const rootDir, const char * const relDir, EventHandler &eventHandler);

		void fillFileRecord(const char * const filePath, mirror::FileRecord &dest);
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
			afc::String dir(relDir);
			dbDirs.erase(dir);

			ctxs.emplace();
			dbRef.getFiles(dir.data(), dir.size(), ctxs.top());
		}

		void dirEnd(const char * const relDir)
		{
			for (auto &e : ctxs.top()) {
				// TODO avoid unnecessary memory allocations.
				std::string relativePath(relDir);
				if (relDir[0] != '\0') {
					relativePath += '/';
				}
				relativePath.append(e.first.begin(), e.first.end());

				logError("File not found in the file system: '"_s, relativePath.c_str(), "'!"_s);
			}
			ctxs.pop();
		}

		void file(const char * const rootDir, const char * const relDir, const char * const fileName)
		{
			logDebug("Checking the file '"_s, fileName, "'..."_s);

			// TODO avoid unnecessary memory allocations.
			const afc::String fileNameStr(fileName);
			const std::string absolutePath = (std::string(rootDir) + '/') + fileName;
			std::string relativePath(relDir);
			if (relDir[0] != '\0') {
				relativePath += '/';
			}
			relativePath += fileName;

			const auto dbEntry = ctxs.top().find(fileNameStr);

			if (dbEntry == ctxs.top().end()) {
				logError("New file found in the file system: '"_s, relativePath.c_str(), "'!"_s);
				return;
			}

			const mirror::FileRecord &expectedFileRecord = dbEntry->second;
			mirror::FileRecord fileRecord;

			mirror::_helper::fillFileRecord(absolutePath.c_str(), fileRecord);

			if (expectedFileRecord.fileSize != fileRecord.fileSize) {
				logError("File size mismatch for the file '"_s, relativePath.c_str(), "'! DB size: "_s,
						expectedFileRecord.fileSize, ", file system size: "_s, fileRecord.fileSize, '.');
			}
			if (expectedFileRecord.lastModifiedTS.millis() != fileRecord.lastModifiedTS.millis()) {
				// TODO support readable date format.
				logError("File last modified timestamp mismatch for the file '"_s, relativePath.c_str(), "'! DB timestamp: "_s,
						expectedFileRecord.lastModifiedTS.millis(), ", file system timestamp: "_s,
						fileRecord.lastModifiedTS.millis(), '.');
			}
			if (!std::equal(fileRecord.md5Digest, fileRecord.md5Digest + MD5_DIGEST_LENGTH,
					expectedFileRecord.md5Digest)) {
				// TODO use single array.
				// TODO introduce hex-encoded representation of array data for logDebug/logError.
				char expectedMD5[2 * MD5_DIGEST_LENGTH];
				char fileMD5[2 * MD5_DIGEST_LENGTH];
				char *p = expectedMD5;
				std::for_each(expectedFileRecord.md5Digest, expectedFileRecord.md5Digest + MD5_DIGEST_LENGTH,
						[&expectedMD5, &p](const unsigned char b) { p = afc::octetToHex(b, p); });
				p = fileMD5;
				std::for_each(fileRecord.md5Digest, fileRecord.md5Digest + MD5_DIGEST_LENGTH,
						[&expectedMD5, &p](const unsigned char b) { p = afc::octetToHex(b, p); });

				// TODO support readable date format.
				logError("File MD5 digest mismatch for the file '"_s, relativePath.c_str(), "'! DB MD5: "_s,
						afc::String(expectedMD5, 2 * MD5_DIGEST_LENGTH), ", file system MD5: "_s,
						afc::String(fileMD5, 2 * MD5_DIGEST_LENGTH), '.');
			}

			ctxs.top().erase(dbEntry);
		}

		mirror::DirSet dbDirs;
		std::stack<mirror::DirFileMap> ctxs;
		mirror::FileDB &dbRef;
	} eventHandler(db);

	mirror::_helper::scanFiles(rootDir, "", eventHandler);

	// TODO pass errors to the caller.
	for (auto missingDir : eventHandler.dbDirs) {
		logDebug("DB dir not found in the file system: '"_s, missingDir, "'..."_s);
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
void mirror::_helper::scanFiles(const char * const rootDir, const char * const relDir, EventHandler &eventHandler)
{
	logDebug("Scanning '"_s, rootDir, "'..."_s);
	DIR *dir = opendir(rootDir);

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

	eventHandler.dirStart(relDir);

	dirent *file;
	// TODO handle errors.
	while ((file = readdir(dir)) != nullptr) {
		const char *name;
		switch (file->d_type) {
		case DT_REG: {
			eventHandler.file(rootDir, relDir, file->d_name);
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
				std::string innerRelDir(relDir);
				if (relDir[0] != '\0') {
					innerRelDir += '/';
				}
				innerRelDir += name;
				// TODO avoid unnecessary memory allocations.
				scanFiles(((std::string(rootDir) + '/') + name).c_str(), innerRelDir.c_str(), eventHandler);
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

	eventHandler.dirEnd(relDir);
}

#endif // MIRROR_UTILS_HPP_
