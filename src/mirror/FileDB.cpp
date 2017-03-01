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
#include "FileDB.hpp"
#include <afc/StringRef.hpp>
#include <afc/logger.hpp>
#include "encoding.hpp"
#include <cstring>
#include <utility>

using afc::operator"" _s;
using afc::logger::logTrace;

mirror::FileDB::FileDB(const char * const dbPathInUtf8)
{
	constexpr auto createFileTableQuery = u8"create table if not exists files "
			"(file text not null, dir text not null, size integer not null, last_modified integer not null, md5 blob not null,"
			"primary key (file, dir))"_s;
	constexpr auto createDirIndexQuery = u8"create index if not exists dir_idx on files (dir)"_s;
	constexpr auto addFileQuery = u8"insert or replace into files (file, dir, size, last_modified, md5) values (?, ?, ?, ?, ?)"_s;
	constexpr auto getFileQuery = u8"select * from files where file = ? and dir = ?"_s;
	constexpr auto getDirFilesQuery = u8"select file, size, last_modified, md5 from files where dir = ?"_s;
	constexpr auto getDirsQuery = u8"select distinct dir from files"_s;

	int result;

	logTrace("Opening connection to the DB "_s, dbPathInUtf8);
	result = sqlite3_open(afc::convertToUtf8(dbPathInUtf8, afc::systemCharset().c_str()).c_str(), &m_conn);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_openConn;
	}

	logTrace("Creating the file table (if missing): "_s, createFileTableQuery);
	result = sqlite3_exec(m_conn, createFileTableQuery.value(), nullptr, nullptr, nullptr);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_initFileTable;
	}

	logTrace("Creating the directory index (if missing): "_s, createDirIndexQuery);
	result = sqlite3_exec(m_conn, createDirIndexQuery.value(), nullptr, nullptr, nullptr);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_initDirIndex;
	}

	logTrace("Preparing statement to add a file: "_s, addFileQuery);
	result = sqlite3_prepare_v2(m_conn, addFileQuery.value(), addFileQuery.size(), &m_addFileStmt, nullptr);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_addFileStmt;
	}

	logTrace("Preparing statement to get a file: "_s, getFileQuery);
	result = sqlite3_prepare_v2(m_conn, getFileQuery.value(), getFileQuery.size(), &m_getFileStmt, nullptr);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_getFileStmt;
	}

	logTrace("Preparing statement to get files from a directory: "_s, getDirFilesQuery);
	result = sqlite3_prepare_v2(m_conn, getDirFilesQuery.value(), getDirFilesQuery.size(), &m_getDirFilesStmt, nullptr);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_getDirFilesStmt;
	}

	logTrace("Preparing statement to get all dirs: "_s, getDirFilesQuery);
	result = sqlite3_prepare_v2(m_conn, getDirsQuery.value(), getDirsQuery.size(), &m_getDirsStmt, nullptr);
	logTrace("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_getDirsStmt;
	}

	return;

error_getDirsStmt:
	sqlite3_finalize(m_getDirFilesStmt);
error_getDirFilesStmt:
	sqlite3_finalize(m_getFileStmt);
error_getFileStmt:
	sqlite3_finalize(m_addFileStmt);
error_addFileStmt:
error_initFileTable:
error_initDirIndex:
	sqlite3_close(m_conn);
error_openConn:
	// TODO handle error.
	throw sqlite3_errstr(result);
}

void mirror::FileDB::addFile(const char * const fileNameU8, const std::size_t fileNameSize,
		const char * const dirNameU8, const std::size_t dirNameSize, const FileRecord &data)
{
	assert(m_conn != nullptr);

	int result;

	logTrace("Binding statement param 1..."_s);
	result = sqlite3_bind_text(m_addFileStmt, 1, fileNameU8, fileNameSize, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 2..."_s);
	result = sqlite3_bind_text(m_addFileStmt, 2, dirNameU8, dirNameSize, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 3..."_s);
	result = sqlite3_bind_int64(m_addFileStmt, 3, static_cast<sqlite_int64>(data.fileSize));
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 4..."_s);
	result = sqlite3_bind_int64(m_addFileStmt, 4, static_cast<sqlite_int64>(data.lastModifiedTS.millis() / 1000));
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 5..."_s);
	result = sqlite3_bind_blob(m_addFileStmt, 5, data.md5Digest, MD5_DIGEST_LENGTH, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Executing statement..."_s);
	result = sqlite3_step(m_addFileStmt);
	if (result != SQLITE_DONE) {
		goto handle_error;
	}

	logTrace("Reseting statement..."_s);
	result = sqlite3_reset(m_addFileStmt);
	if (result != SQLITE_OK) {
		goto handle_reset_error;
	}

	return;

handle_error:
	// Attempting to reset the statement without overwriting the error code.
	logTrace("Reseting statement..."_s);
	// TODO handle sqlite3_reset error code.
	sqlite3_reset(m_addFileStmt);
handle_reset_error:
	throw sqlite3_errstr(result);
}

void mirror::FileDB::getFiles(const char * const dirNameU8, const std::size_t dirNameSize, mirror::DirFileMap &dest)
{
	assert(m_conn != nullptr);

	int result;

	logTrace("Binding statement param 1..."_s);
	result = sqlite3_bind_text(m_getDirFilesStmt, 1, dirNameU8, dirNameSize, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	// TODO make this code exception-safe.
	logTrace("Executing statement..."_s);
	for (;;) {
		result = sqlite3_step(m_getDirFilesStmt);
		if (result == SQLITE_ROW) {
			// TODO read file from the DB in UTF-8.
			const char * const fileNameU8 = reinterpret_cast<const char *>(sqlite3_column_text(m_getDirFilesStmt, 0));
			std::size_t fileNameU8Size = std::strlen(fileNameU8);
			FileRecord &fileRec = dest[PathKey(fileNameU8, fileNameU8Size)];
			fileRec.fileSize = sqlite3_column_int64(m_getDirFilesStmt, 1);
			fileRec.lastModifiedTS.setMillis(sqlite3_column_int64(m_getDirFilesStmt, 2) * 1000);
			const unsigned char * const md5 = reinterpret_cast<const unsigned char *>(sqlite3_column_blob(m_getDirFilesStmt, 3));
			std::copy_n(md5, MD5_DIGEST_LENGTH, fileRec.md5Digest);

			// TODO log md5, log time in a readable format
			logTrace("File found: {'"_s, Utf8ToSystemView(fileNameU8, fileNameU8Size), "', "_s,
					fileRec.fileSize, ", "_s, fileRec.lastModifiedTS.millis() / 1000, "}..."_s);
		} else if (result == SQLITE_DONE) {
			logTrace("Reading result set done."_s);
			break;
		} else {
			goto handle_error;
		}
	}

	logTrace("Reseting statement..."_s);
	result = sqlite3_reset(m_getDirFilesStmt);
	if (result != SQLITE_OK) {
		goto handle_reset_error;
	}

	return;

handle_error:
	// Attempting to reset the statement without overwriting the error code.
	logTrace("Reseting statement..."_s);
	// TODO handle sqlite3_reset error code.
	sqlite3_reset(m_getDirFilesStmt);
handle_reset_error:
	throw sqlite3_errstr(result);
}

void mirror::FileDB::getDirs(mirror::DirSet &dest)
{
	assert(m_conn != nullptr);

	int result;

	// TODO make this code exception-safe.
	logTrace("Executing statement..."_s);
	for (;;) {
		result = sqlite3_step(m_getDirsStmt);
		if (result == SQLITE_ROW) {
			const char * const dirNameU8 = reinterpret_cast<const char *>(sqlite3_column_text(m_getDirsStmt, 0));
			PathKey key(dirNameU8, false);

			// TODO log md5, log time in a readable format
			// TODO log in system encoding.
			logTrace("Dir found: '"_s, Utf8ToSystemView(key.data, key.size), "'...");

			dest.emplace(std::move(key));
		} else if (result == SQLITE_DONE) {
			logTrace("Reading result set done."_s);
			break;
		} else {
			goto handle_error;
		}
	}

	logTrace("Reseting statement..."_s);
	result = sqlite3_reset(m_getDirsStmt);
	if (result != SQLITE_OK) {
		goto handle_reset_error;
	}

	return;

handle_error:
	// Attempting to reset the statement without overwriting the error code.
	logTrace("Reseting statement..."_s);
	// TODO handle sqlite3_reset error code.
	sqlite3_reset(m_getDirsStmt);
handle_reset_error:
	throw sqlite3_errstr(result);
}
