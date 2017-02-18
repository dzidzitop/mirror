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

using afc::operator"" _s;
using afc::logger::logDebug;

mirror::FileDB::FileDB(const char * const dbPathInUtf8)
{
	constexpr auto createFileTableQuery = u8"create table if not exists files "
			"(file text not null, dir text not null, size integer not null, last_modified integer not null, md5 blob not null,"
			"primary key (file, dir))"_s;
	constexpr auto createDirIndexQuery = u8"create index if not exists dir_idx on files (dir)"_s;
	constexpr auto addFileQuery = u8"insert or replace into files (file, dir, size, last_modified, md5) values (?, ?, ?, ?, ?)"_s;
	constexpr auto getFileQuery = u8"select * from files where file = ? and dir = ?"_s;
	constexpr auto getDirFilesQuery = u8"select file, size, last_modified, md5 from files where dir = ?"_s;

	int result;

	logDebug("Opening connection to the DB "_s, dbPathInUtf8);
	result = sqlite3_open(afc::convertToUtf8(dbPathInUtf8, afc::systemCharset().c_str()).c_str(), &m_conn);
	logDebug("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_openConn;
	}

	logDebug("Creating the file table (if missing): "_s, createFileTableQuery);
	result = sqlite3_exec(m_conn, createFileTableQuery.value(), nullptr, nullptr, nullptr);
	logDebug("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_initFileTable;
	}

	logDebug("Creating the directory index (if missing): "_s, createDirIndexQuery);
	result = sqlite3_exec(m_conn, createDirIndexQuery.value(), nullptr, nullptr, nullptr);
	logDebug("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_initDirIndex;
	}

	logDebug("Preparing statement to add a file: "_s, addFileQuery);
	result = sqlite3_prepare_v2(m_conn, addFileQuery.value(), addFileQuery.size(), &m_addFileStmt, nullptr);
	logDebug("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_addFileStmt;
	}

	logDebug("Preparing statement to get a file: "_s, getFileQuery);
	result = sqlite3_prepare_v2(m_conn, getFileQuery.value(), getFileQuery.size(), &m_getFileStmt, nullptr);
	logDebug("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_getFileStmt;
	}

	logDebug("Preparing statement to get files from a directory: "_s, getDirFilesQuery);
	result = sqlite3_prepare_v2(m_conn, getDirFilesQuery.value(), getDirFilesQuery.size(), &m_getDirFilesStmt, nullptr);
	logDebug("Result code: "_s, result);

	if (result != SQLITE_OK) {
		goto error_getDirFilesStmt;
	}

	return;

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

void mirror::FileDB::addFile(const char * const fileName, const std::size_t fileNameSize,
		const char * const dirName, const std::size_t dirNameSize, const FileRecord &data)
{
	assert(m_conn != nullptr);

	int result;

	logDebug("Binding statement param 1..."_s);
	result = sqlite3_bind_text(m_addFileStmt, 1, fileName, fileNameSize, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logDebug("Binding statement param 2..."_s);
	result = sqlite3_bind_text(m_addFileStmt, 2, dirName, dirNameSize, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logDebug("Binding statement param 3..."_s);
	result = sqlite3_bind_int64(m_addFileStmt, 3, static_cast<sqlite_int64>(data.fileSize));
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logDebug("Binding statement param 4..."_s);
	result = sqlite3_bind_int64(m_addFileStmt, 4, static_cast<sqlite_int64>(data.lastModifiedTS.millis() / 1000));
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logDebug("Binding statement param 5..."_s);
	result = sqlite3_bind_blob(m_addFileStmt, 5, data.md5Digest, MD5_DIGEST_LENGTH, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logDebug("Executing statement..."_s);
	result = sqlite3_step(m_addFileStmt);
	if (result != SQLITE_DONE) {
		goto handle_error;
	}

	logDebug("Reseting statement..."_s);
	result = sqlite3_reset(m_addFileStmt);
	if (result != SQLITE_OK) {
		goto handle_reset_error;
	}

	return;

handle_error:
	// Attempting to reset the statement without overwriting the error code.
	logDebug("Reseting statement..."_s);
	// TODO handle sqlite3_reset error code.
	sqlite3_reset(m_addFileStmt);
handle_reset_error:
	throw sqlite3_errstr(result);
}

void mirror::FileDB::getFiles(const char * const dirName, const std::size_t dirNameSize, mirror::DirFileMap &dest)
{
	assert(m_conn != nullptr);

	int result;

	logDebug("Binding statement param 1..."_s);
	result = sqlite3_bind_text(m_getDirFilesStmt, 1, dirName, dirNameSize, SQLITE_STATIC);
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	// TODO make this code exception-safe.
	logDebug("Executing statement..."_s);
	for (;;) {
		result = sqlite3_step(m_getDirFilesStmt);
		if (result == SQLITE_ROW) {
			const char * const fileName = reinterpret_cast<const char *>(sqlite3_column_text(m_getDirFilesStmt, 0));
			FileRecord &fileRec = dest[afc::String(fileName)];
			fileRec.fileSize = sqlite3_column_int64(m_getDirFilesStmt, 1);
			fileRec.lastModifiedTS.setMillis(sqlite3_column_int64(m_getDirFilesStmt, 2) * 1000);
			const unsigned char * const md5 = reinterpret_cast<const unsigned char *>(sqlite3_column_blob(m_getDirFilesStmt, 3));
			std::copy_n(md5, MD5_DIGEST_LENGTH, fileRec.md5Digest);

			// TODO log md5, log time in a readable format
			logDebug("File found: {'"_s, fileName, "', "_s, fileRec.fileSize, ", "_s, fileRec.lastModifiedTS.millis() / 1000, "}..."_s);
		} else if (result == SQLITE_DONE) {
			logDebug("Reading result set done."_s);
			break;
		} else {
			goto handle_error;
		}
	}

	logDebug("Reseting statement..."_s);
	result = sqlite3_reset(m_getDirFilesStmt);
	if (result != SQLITE_OK) {
		goto handle_reset_error;
	}

	return;

handle_error:
	// Attempting to reset the statement without overwriting the error code.
	logDebug("Reseting statement..."_s);
	// TODO handle sqlite3_reset error code.
	sqlite3_reset(m_getDirFilesStmt);
handle_reset_error:
	throw sqlite3_errstr(result);
}
