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
#include "utils.hpp"
#include <afc/number.h>
#include <algorithm>
#include <fcntl.h>
#include <openssl/md5.h>
#include <stdexcept>
#include <sys/stat.h>

using afc::operator"" _s;
using afc::logger::logDebug;

namespace
{
	[[noreturn]]
	void throwUnexpectedError(const int errorCode)
	{
		constexpr auto msgHead = "Unexpected error has occurred: "_s;
		char msgBuf[msgHead.size() + afc::maxPrintedSize<int, 10>() + 2]; // + 2  for '.' and '\0'.

		char *p = std::copy(msgHead.begin(), msgHead.end(), msgBuf);
		p = afc::printNumber<10>(errorCode, p);
		p = std::copy_n(".", 2, p); // '.' and '\0' is copied.
		assert(&msgBuf[0] + sizeof(msgBuf) > p);

		throw std::runtime_error(msgBuf);
	}
}

[[noreturn]]
void mirror::_helper::handleOpenFileError(const int errorCode)
{
	const char *msg;
	switch (errno) {
	case EACCES:
		msg = "Search permission is denied on a component of the path prefix, or the file exists "
				"and the permissions specified by mode are denied, or the file does not exist "
				"and write permission is denied for the parent directory of the file to be created.";
		goto throw_static_msg;
	case EINTR:
		msg = "A signal was caught during fopen().";
		goto throw_static_msg;
	case EISDIR:
		msg = "The named file is a directory and mode requires write access.";
		goto throw_static_msg;
	case ELOOP:
		msg = "A loop exists in symbolic links encountered during resolution of the path argument OR "
				"more than {SYMLOOP_MAX} symbolic links were encountered during resolution of the path argument.";
		goto throw_static_msg;
	case EMFILE:
		msg = "{OPEN_MAX} file descriptors are currently open in the calling process OR "
				"{FOPEN_MAX} streams are currently open in the calling process OR "
				"{STREAM_MAX} streams are currently open in the calling process.";
		goto throw_static_msg;
	case ENAMETOOLONG:
		msg = "The length of the filename argument exceeds {PATH_MAX} or a pathname component is "
				"longer than {NAME_MAX} OR pathname resolution of a symbolic link produced an intermediate "
				"result whose length exceeds {PATH_MAX}.";
		goto throw_static_msg;
	case ENFILE:
		msg = "The maximum allowable number of files is currently open in the system.";
		goto throw_static_msg;
	case ENOENT:
		msg = "A component of filename does not name an existing file or filename is an empty string.";
		goto throw_static_msg;
	case ENOSPC:
		msg = "The directory or file system that would contain the new file cannot be expanded, the file "
				"does not exist, and the file was to be created.";
		goto throw_static_msg;
	case ENOTDIR:
		msg = "A component of the path prefix is not a directory.";
		goto throw_static_msg;
	case ENXIO:
		msg = "The named file is a character special or block special file, and the device associated with "
				"this special file does not exist.";
		goto throw_static_msg;
	case EOVERFLOW:
		msg = "The named file is a regular file and the size of the file cannot be represented correctly in "
				"an object of type off_t.";
		goto throw_static_msg;
	case EROFS:
		msg = "The named file resides on a read-only file system and mode requires write access.";
		goto throw_static_msg;
	case EINVAL:
		msg = "The value of the mode argument is not valid.";
		goto throw_static_msg;
	case ENOMEM:
		msg = "Insufficient storage space is available.";
		break;
	case ETXTBSY:
		msg = "The file is a pure procedure (shared text) file that is being executed and mode requires "
				"write access.";
		goto throw_static_msg;
	default:
		throwUnexpectedError(errorCode);
	}

	assert(false); // should never go here.
throw_static_msg:
	throw std::runtime_error(msg);
}

[[noreturn]]
void mirror::_helper::handleReadFileError(const int errorCode)
{
	const char *msg;
	switch (errorCode) {
	case EAGAIN:
		msg = "The O_NONBLOCK flag is set for the file descriptor underlying stream and the thread would be "
				"delayed in the fgetc() operation.";
		goto throw_static_msg;
	case EBADF:
		msg = "The file descriptor underlying stream is not a valid file descriptor open for reading.";
		goto throw_static_msg;
	case EINTR:
		msg = "The read operation was terminated due to the receipt of a signal, and no data was transferred.";
		goto throw_static_msg;
	case EIO:
		msg = "A physical I/O error has occurred, or the process is in a background process group attempting "
				"to read from its controlling terminal, and either the process is ignoring or blocking "
				"the SIGTTIN signal or the process group is orphaned. This error may also be generated "
				"for implementation-defined reasons.";
		goto throw_static_msg;
	case EOVERFLOW:
		msg = "The file is a regular file and an attempt was made to read at or beyond the offset maximum "
				"associated with the corresponding stream.";
		goto throw_static_msg;
	case ENOMEM:
		msg = "Insufficient storage space is available.";
		goto throw_static_msg;
	case ENXIO:
		msg = "A request was made of a nonexistent device, or the request was outside the capabilities "
				"of the device.";
		goto throw_static_msg;
	default:
		throwUnexpectedError(errorCode);
	}

	assert(false); // should never go here.
throw_static_msg:
	throw std::runtime_error(msg);
}

[[noreturn]]
void mirror::_helper::handleReadDirError(const int errorCode)
{
	const char *msg;
	switch (errorCode) {
	case EOVERFLOW:
		msg = "One of the values in the structure to be returned cannot be represented correctly.";
		goto throw_static_msg;
	case EBADF:
		msg = "The dirp argument does not refer to an open directory stream.";
		goto throw_static_msg;
	case ENOENT:
		msg = "The current position of the directory stream is invalid.";
		goto throw_static_msg;
	default:
		throwUnexpectedError(errorCode);
	}

	assert(false); // should never go here.
throw_static_msg:
	throw std::runtime_error(msg);
}

void mirror::_helper::fillRegularFileRecord(const struct stat &fileStat, const char * const filePath,
		mirror::FileRecord &dest)
{
	dest.type = FileType::file;
	dest.fileSize = fileStat.st_size;
	dest.lastModifiedTS.setMillis(static_cast<afc::Timestamp::time_type>(fileStat.st_mtime) * 1000);

	MD5_CTX md5Ctx;
	auto calcMD5 = [&md5Ctx] (const char buf[], const std::size_t n) noexcept { MD5_Update(&md5Ctx, buf, n); };

	MD5_Init(&md5Ctx);
	mirror::_helper::processFile(filePath, calcMD5);
	MD5_Final(dest.md5Digest, &md5Ctx);
}

void mirror::createDB(const char * const rootDir, const std::size_t rootDirSize, mirror::FileDB &db)
{
	struct EventHandler
	{
		EventHandler(mirror::FileDB &db) noexcept : m_db(db) {}

		void dirStart(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset) const noexcept {}
		void dirEnd(afc::FastStringBuffer<char> &path, const std::size_t relDirOffset) const noexcept {}

		bool file(const struct stat &fileStat, const afc::FastStringBuffer<char> &path, const std::size_t relDirOffset,
				const std::size_t fileNameOffset) const
		{
			const char * const relPath = path.begin() + relDirOffset;

			logDebug("Adding the file '"_s, std::make_pair(relPath, path.end()), "' to the DB..."_s);

			mirror::FileRecord fileRecord;

			assert(S_ISREG(fileStat.st_mode) || S_ISDIR(fileStat.st_mode));
			if (S_ISREG(fileStat.st_mode)) {
				mirror::_helper::fillRegularFileRecord(fileStat, path.c_str(), fileRecord);
			} else {
				fileRecord.type = FileType::dir;
			}

			const char * const fileName = path.begin() + fileNameOffset;
			const std::size_t fileNameSize = path.size() - fileNameOffset;
			const TextHolder fileNameU8 = mirror::convertToUtf8(fileName, fileNameSize);

			std::size_t relDirSize = fileNameOffset - relDirOffset;
			if (relDirSize > 0) {
				--relDirSize; // removing separator between dir and file.
			}
			// TODO do not convert relative dir again and again for every file in the directory.
			const TextHolder relDirU8 = mirror::convertToUtf8(relPath, relDirSize);

			m_db.addFile(fileNameU8.value, fileNameU8.size, relDirU8.value, relDirU8.size, fileRecord);

			return true;
		}
	private:
		mirror::FileDB &m_db;
	} eventHandler(db);

	db.beginTransaction();
	try {
		mirror::_helper::scanFiles(rootDir, rootDirSize, eventHandler);
	}
	catch (...) {
		db.rollback();
		throw;
	}
	db.commit();
}

bool mirror::copyFile(const int srcDirFd, const int destDirFd, const char * const relPath)
{
	// TODO support fsync
	// TODO support copying symlinks
	const int srcFd = openat(srcDirFd, relPath, O_NOFOLLOW | O_RDONLY);
	if (srcFd == -1) {
		// TODO log error.
		return false;
	}
	// TODO preserve timestamps, sticky flags, permissions, ownership
	const int destFd = openat(destDirFd, relPath, O_CREAT | O_EXCL | O_WRONLY);
	if (destFd == -1) {
		if (close(srcFd) == -1) {
			// TODO log error.
		}
		// TODO log error.
		return false;
	}

	bool success = true;

	constexpr std::size_t bufSize = 4096;
	unsigned char buf[bufSize];
	for (;;) {
		const ssize_t n = read(srcFd, buf, bufSize);
		if (n == -1) {
			// TODO handle error.
			success = false;
			goto end;
		} else {
			const ssize_t m = write(destFd, buf, n);
			if (m < n) {
				// TODO handle error
				success = false;
				goto end;
			}
		}
	}

end:
	if (close(srcFd) == -1) {
		// TODO log error.
		success = false;
	}
	if (close(destFd) == -1) {
		// TODO log error.
		success = false;
	}
	return success;
}
