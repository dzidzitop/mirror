/* mirror - a tool to make mirrors of files or directories and to check consistency of the existing mirrors.
Copyright (C) 2017-2019 Dźmitry Laŭčuk

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
#include <fcntl.h>
#include "FileDB.hpp"
#include <stack>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>

namespace mirror
{
	void createDB(const char *rootDir, std::size_t rootDirSize, mirror::FileDB &db);

	template<typename MismatchHandler>
	void checkFileSystem(const char *rootDir, std::size_t rootDirSize, mirror::FileDB &db,
			MismatchHandler &mismatchHandler);

	bool copyFile(int srcDirFd, int destDirFd, const char *relPath);
	bool copyDir(int srcDirFd, const char *srcDir, std::size_t srcDirSize,
			int destDirFd, const char *destDir, std::size_t destDirSize,
			const char *relPath, std::size_t relPathSize);

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

		template<typename ChunkOp>
		void processFile(int fd, const char * const path, ChunkOp &chunkOp);

		template<typename EventHandler>
		inline DIR *startDirScanning(afc::FastStringBuffer<char> &path, std::size_t relPathOffset,
				const int fd, EventHandler &eventHandler)
		{
			logDebug("Scanning '"_s, path, "'..."_s);

			DIR * const dir = fdopendir(fd);

			if (dir == nullptr) {
				switch (errno) {
				case EACCES:
					// TODO make the behaviour configurable (ignorable).
					logDebug("No access to '"_s, path, '\'');
					throw errno;
				case ENOENT:
					logError("Directory not found: '"_s, path, '\'');
					throw errno;
				default:
					// TODO handle error
					throw errno;
				}
			}

			eventHandler.dirStart(path, relPathOffset);

			path.reserveForOne();
			path.append('/');

			return dir;
		}

		template<typename EventHandler>
		void scanFiles(afc::FastStringBuffer<char> &path, EventHandler &eventHandler);

		template<typename EventHandler>
		void scanFiles(afc::FastStringBuffer<char> &path, int dirFd, EventHandler &eventHandler);

		template<typename EventHandler>
		inline void scanFiles(const char * const rootDir, const std::size_t rootDirSize, EventHandler &eventHandler)
		{
			std::size_t normalisedSize = rootDirSize;
			if (rootDir[rootDirSize - 1] == '/') {
				--normalisedSize;
			}
			afc::FastStringBuffer<char> dirBuf(normalisedSize);
			dirBuf.append(rootDir, normalisedSize);
			scanFiles(dirBuf, eventHandler);
		}

		void fillRegularFileRecord(const struct stat &fileStat, const int fd, const char * const filePath,
				mirror::FileRecord &dest);
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

	struct VerifyDirMismatchHandler
	{
		void fileNotFound(const mirror::FileType type, const char * const path, const std::size_t pathSize)
		{
			using afc::operator"" _s;
			afc::logger::logError(type, " not found in the file system: '"_s,
					std::make_pair(path, path + pathSize), "'!"_s);
		}

		void newFileFound(const mirror::FileType type, const char * const path, const std::size_t pathSize)
		{
			using afc::operator"" _s;
			afc::logger::logError("New "_s, type == mirror::FileType::file ? "file"_s : "dir"_s,
					" found in the file system: '"_s, std::make_pair(path, path + pathSize), "'!"_s);
		}

		bool checkFileMismatch(const char * const path, const std::size_t pathSize,
				const mirror::FileRecord expectedFileRecord, const mirror::FileRecord actualFileRecord)
		{
			using afc::operator"" _s;
			using CRC64View = afc::logger::HexEncodedN<sizeof(mirror::FileRecord::crc64)>;
			using afc::logger::logError;

			bool fullMatch = true;

			if (expectedFileRecord.type != actualFileRecord.type) {
				logError("File type mismatch for the file '"_s, std::make_pair(path, path + pathSize),
						"'! DB file type: '"_s, expectedFileRecord.type, "', file system file type: '"_s,
						actualFileRecord.type, "'."_s);
				fullMatch = false;
			}
			else if (actualFileRecord.type == mirror::FileType::file)
			{
				const bool sizeMismatch = expectedFileRecord.fileSize != actualFileRecord.fileSize;
				const bool lastModMismatch =
						expectedFileRecord.lastModifiedTS.millis() != actualFileRecord.lastModifiedTS.millis();
				const bool digestMismatch = !std::equal(actualFileRecord.crc64,
						actualFileRecord.crc64 + sizeof(actualFileRecord.crc64), expectedFileRecord.crc64);

				fullMatch = !sizeMismatch && !lastModMismatch && !digestMismatch;

				if (!fullMatch) {
					logError("Mismatch for the file '"_s, std::make_pair(path, path + pathSize), "':"_s);
					if (sizeMismatch) {
						logError("\tDB size: "_s, expectedFileRecord.fileSize,
								"\n\tFS size: "_s, actualFileRecord.fileSize);
					}
					if (lastModMismatch) {
						logError("\tDB last modified timestamp: "_s,
							afc::ISODateTimeView(expectedFileRecord.lastModifiedTS),
							"\n\tFS last modified timestamp: "_s,
							afc::ISODateTimeView(actualFileRecord.lastModifiedTS));
					}
					if (digestMismatch) {
						logError("\tDB CRC64 digest: '"_s, CRC64View(expectedFileRecord.crc64),
								"'\n\tFS CRC64 digest: '"_s, CRC64View(actualFileRecord.crc64), '\'');
					}
				}
			}

			return fullMatch;
		}
	};

	struct MergeDirMismatchHandler
	{
		MergeDirMismatchHandler(const char * const srcDirRef, const std::size_t srcDirSize,
				const char * const destDirRef, const std::size_t destDirSize) :
						srcDirRef(srcDirRef), srcDirSize(srcDirSize),
						destDirRef(destDirRef), destDirSize(destDirSize)
		{
			// TODO avoid copying relpath into a buffer
			srcDirFd = open(std::string(srcDirRef, srcDirSize).c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
			if (srcDirFd == -1) {
				// TODO handle error
			}
			// TODO avoid copying relpath into a buffer
			destDirFd = open(std::string(destDirRef, destDirSize).c_str(), O_DIRECTORY | O_NOFOLLOW);
			if (destDirFd == -1) {
				closeDir(srcDirRef, srcDirSize, srcDirFd);
				// TODO handle error
			}
		}

		~MergeDirMismatchHandler()
		{
			closeDir(destDirRef, destDirSize, destDirFd);
			closeDir(srcDirRef, srcDirSize, srcDirFd);
		}

		void fileNotFound(const mirror::FileType type, const char * const path, const std::size_t pathSize)
		{
			using afc::operator"" _s;

			// TODO Add verbose info logging
			switch (type) {
			case mirror::FileType::file:
				afc::logger::logError(type, " not found in the destination file system: '"_s,
										std::make_pair(path, path + pathSize), "'!"_s);
				afc::logger::logDebug("Copying '", std::make_pair(path, path + pathSize), "'..."_s);
				// TODO avoid copying relpath into a buffer
				mirror::copyFile(srcDirFd, destDirFd, std::string(path, pathSize).c_str());
				return;
			case mirror::FileType::dir:
				afc::logger::logError(type, " not found in the destination file system: '"_s,
										std::make_pair(path, path + pathSize), "'!"_s);
				afc::logger::logDebug("Copying directory '", std::make_pair(path, path + pathSize), "'..."_s);
				mirror::copyDir(srcDirFd, srcDirRef, srcDirSize, destDirFd, destDirRef, destDirSize, path, pathSize);
				return;
			default:
				assert(false);
			}
		}

		void newFileFound(const mirror::FileType type, const char * const path, const std::size_t pathSize)
		{
			using afc::operator"" _s;

			// TODO think of adding an option to wipe new files out.
			afc::logger::logError("New "_s, type == mirror::FileType::file ? "file"_s : "dir"_s,
					" found in the destination file system: '"_s, std::make_pair(path, path + pathSize), "'!"_s);
		}

		bool checkFileMismatch(const char * const path, const std::size_t pathSize,
				const mirror::FileRecord expectedFileRecord, const mirror::FileRecord actualFileRecord)
		{
			// TODO implement me
			return true;
		}
	private:
		static void closeDir(const char * const path, const std::size_t pathSize, const int fd)
		{
			using afc::operator"" _s;

			if (close(fd) != 0)
			{
				// TODO report the cause of the error (errno).
				afc::logger::logError("Unable to close the directory '"_s,
						std::make_pair(path, path + pathSize), "'!"_s);
			}
		}

		const char *srcDirRef;
		std::size_t srcDirSize;
		const char *destDirRef;
		std::size_t destDirSize;
		int srcDirFd;
		int destDirFd;
	};

	// TODO make logging readable (especially make paths absolute and relative to src and dest parent dirs)
	struct CopyDirHandler
	{
		// TODO don't use srcDirFd
		CopyDirHandler(const int dirToCopyFd, const int destDirFd, const char * const relPath,
				const std::size_t relPathSize) : srcFd(dirToCopyFd), destFd(-1), destDirFd(destDirFd),
						destPath(relPath), destPathSize(relPathSize) {}

		~CopyDirHandler() = default;

		CopyDirHandler(const CopyDirHandler &) = delete;
		CopyDirHandler(CopyDirHandler &&) = delete;
		CopyDirHandler &operator=(const CopyDirHandler &) = delete;
		CopyDirHandler &operator=(CopyDirHandler &&) = delete;

		// TODO calculate active dir fd to avoid recalc of the dest dir repeatedly.
		void dirStart(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset)
		{
			using afc::logger::logDebug;
			using afc::operator"" _s;

			// Ensuring also that the string is terminated with '\0'.
			const char * const relDir = path.c_str() + relDirOffset;
			const char * const relDirEnd = path.end();
			logDebug("Entering '"_s, std::pair<const char *, const char *>(relDir, relDirEnd), "'..."_s);
			logDebug("Making dest directory '"_s, std::pair<const char *, const char *>(relDir, relDirEnd), "'..."_s);

			if (relDir == relDirEnd) {
				assert(destFd == -1);

				// TODO copy permission and owner/group (and timestamp?)
				if (mkdirat(destDirFd, destPath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
				{
					// TODO handle error.
					throw errno;
				}
				destFd = openat(destDirFd, destPath, O_DIRECTORY);
			} else {
				// TODO copy permission and owner/group (and timestamp?)
				if (mkdirat(destFd, relDir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
					// TODO handle error.
					throw errno;
				}
			}
		}

		void dirEnd(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset)
		{
			using afc::logger::logDebug;
			using afc::operator"" _s;

			const char * const relDir = path.begin() + relDirOffset;
			logDebug("Exiting '"_s, std::pair<const char *, const char *>(relDir, path.end()), "'..."_s);
			// TODO close created fd.
		}

		// TODO support symbolic links.
		bool file(const struct stat &fileStat, const int fd, const afc::FastStringBuffer<char> &path,
				const std::size_t relPathOffset, const std::size_t fileNameOffset)
		{
			using afc::logger::logDebug;
			using afc::operator"" _s;

			assert(S_ISREG(fileStat.st_mode) || S_ISDIR(fileStat.st_mode));

			if (S_ISDIR(fileStat.st_mode)) {
				// Directories are not copied here.
				return true;
			}

			// Ensuring also that the string is terminated with '\0'.
			const char * const relPath = path.c_str() + relPathOffset;

			logDebug("Copying the file '"_s, std::make_pair(relPath, path.end()), "'..."_s);

			return mirror::copyFile(srcFd, destFd, relPath);
		}

		int srcFd;
		int destFd;
		int destDirFd;
		const char *destPath;
		std::size_t destPathSize;
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
void mirror::checkFileSystem(const char * const rootDir, const std::size_t rootDirSize, mirror::FileDB &db,
		MismatchHandler &mismatchHandler)
{
	using afc::operator"" _s;
	using afc::logger::logDebug;

	struct EventHandler
	{
		EventHandler(mirror::FileDB &db, MismatchHandler &mismatchHandler)
				: dbDirs(), ctxs(), dbRef(db), handler(mismatchHandler) { db.getDirs(dbDirs); }

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
					path.reserve(path.size() + buf.size);
					path.append(buf.value, buf.size);

					const char * const relPath = path.data() + relDirOffset;
					handler.fileNotFound(e.second.type, relPath, path.end() - relPath);

					path.resize(path.size() - buf.size);
				}

				path.resize(pathSize);
			}
			ctxs.pop();
		}

		bool file(const struct stat &fileStat, const int fd, const afc::FastStringBuffer<char> &path,
				const std::size_t relPathOffset, const std::size_t fileNameOffset)
		{
			assert(S_ISREG(fileStat.st_mode) || S_ISDIR(fileStat.st_mode));

			const char * const relPath = path.begin() + relPathOffset;

			logDebug("Checking the file '"_s, std::make_pair(relPath, path.end()), "'..."_s);

			const char * const fileName = path.begin() + fileNameOffset;
			const std::size_t fileNameSize = path.size() - fileNameOffset;

			const TextHolder buf = mirror::convertToUtf8(fileName, fileNameSize);
			const auto dbEntry = ctxs.top().find(PathKey(buf.value, buf.size, true));

			if (dbEntry == ctxs.top().end()) {
				const FileType type = S_ISDIR(fileStat.st_mode) ? FileType::dir : FileType::file;
				handler.newFileFound(type, relPath, path.end() - relPath);
				return false;
			}

			const mirror::FileRecord &expectedFileRecord = dbEntry->second;
			mirror::FileRecord fileRecord;

			if (S_ISREG(fileStat.st_mode)) {
				mirror::_helper::fillRegularFileRecord(fileStat, fd, path.c_str(), fileRecord);
			} else {
				fileRecord.type = FileType::dir;
			}

			const bool fullMatch = handler.checkFileMismatch(
					relPath, path.end() - relPath, expectedFileRecord, fileRecord);

			ctxs.top().erase(dbEntry);

			return fullMatch;
		}

		mirror::DirSet dbDirs;
		std::stack<mirror::DirFileMap> ctxs;
		mirror::FileDB &dbRef;
		MismatchHandler &handler;
	} eventHandler(db, mismatchHandler);

	mirror::_helper::scanFiles(rootDir, rootDirSize, eventHandler);

	// TODO pass errors to the caller.
	for (const PathKey &missingDir : eventHandler.dbDirs) {
		logDebug("DB dir not found in the file system: '"_s,
				Utf8ToSystemView(missingDir.data, missingDir.size), "'..."_s);
	}

	assert(eventHandler.ctxs.empty());
}

template<typename ChunkOp>
inline void mirror::_helper::processFile(const int fd, const char * const path, ChunkOp &chunkOp)
{
	unsigned char buf[4096];
	for (;;) {
		const ssize_t n = read(fd, buf, 4096);
		if (n == 0) {
			break;
		} else if (n == -1) {
			handleReadFileError(errno);
		} else {
			chunkOp(buf, n);
		}
	}
}

// TODO think of using char[PATH_MAX] for path instead of dynamic buffer
template<typename EventHandler>
void mirror::_helper::scanFiles(afc::FastStringBuffer<char> &path, EventHandler &eventHandler)
{
	int dirFd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY);
	if (dirFd == -1) {
		// TODO handle error
		throw errno;
	}

	scanFiles(path, dirFd, eventHandler); // dirFd is closed here.
}

// TODO think of using char[PATH_MAX] for path instead of dynamic buffer
template<typename EventHandler>
void mirror::_helper::scanFiles(afc::FastStringBuffer<char> &path, const int fd, EventHandler &eventHandler)
{
	struct Ctx
	{
		Ctx(DIR * const dir, const int fd, const std::size_t dirNameSize) : dir(dir), fd(fd), dirNameSize(dirNameSize) {}

		DIR *dir;
		int fd;
		std::size_t dirNameSize;
	};

	std::stack<Ctx> ctxs;

	int dirFd = fd;
	DIR *dir = startDirScanning(path, path.size(), dirFd, eventHandler);
	std::size_t dirNameSize;

	// Must follow the first invocation of startDirScanning() to skip slash this function appends to path.
	const std::size_t relPathOffset = path.size();

	dirStart: for (;;) {
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

			const int fd = openat(dirFd, name, O_RDONLY);
			if (fd == -1) {
				mirror::_helper::handleOpenFileError(errno);
			}
			const std::size_t nameSize = std::strlen(name);
			path.reserve(path.size() + nameSize);
			path.append(name, nameSize);

			struct stat fileStat;
			const int result = fstat(fd, &fileStat);
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

			if (S_ISREG(fileStat.st_mode) || S_ISDIR(fileStat.st_mode)) {
				// TODO handle error
				const bool success = eventHandler.file(fileStat, fd, path, relPathOffset, path.size() - nameSize);

				if (S_ISDIR(fileStat.st_mode)) {
					if (success) { // If the dir is invalid for some reason then there's no need to go deeper.
						ctxs.emplace(dir, dirFd, dirNameSize);

						dir = startDirScanning(path, relPathOffset, fd, eventHandler);
						dirFd = fd;
						dirNameSize = nameSize;

						goto dirStart;
					}
				}
			} else {
				// TODO support non-regular and non-directory files.
				logDebug("The file '"_s, name, "' is neither a directory or a regular file. Skipping it..."_s);
			}

			if (close(fd) != 0) { // directory file descriptors are closed by closedir().
				// TODO handle error.
				throw errno;
			}

			// Rolling back the dir path buffer to the current dir with slash.
			path.resize(path.size() - nameSize);
		}

	end:
		// TODO call closedir even if an error occurs.
		closedir(dir); // closes the file descriptor, too.

		eventHandler.dirEnd(path, relPathOffset);

		if (ctxs.empty()) {
			break;
		}

		// Removing the trailing slash.
		path.resize(path.size() - dirNameSize - 1);

		dir = ctxs.top().dir;
		dirFd = ctxs.top().fd;
		dirNameSize = ctxs.top().dirNameSize;
		ctxs.pop();
	}
}

#endif // MIRROR_UTILS_HPP_
