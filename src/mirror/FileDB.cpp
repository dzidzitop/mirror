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
#include "FileDB.hpp"
#include <afc/dateutil.hpp>
#include <afc/logger.hpp>
#include <afc/StringRef.hpp>
#include <cassert>
#include "encoding.hpp"
#include <utility>

using afc::operator"" _s;
using afc::logger::logTrace;

mirror::FileDB::FileDB(const char * const dbPathInUtf8)
{
	constexpr auto createFileTableQuery = u8"create table if not exists files "
			"(file text not null, dir text not null, type integer not null, size integer, last_modified integer,"
			"crc64 blob, primary key (file, dir))"_s;
	constexpr auto createDirIndexQuery = u8"create index if not exists dir_idx on files (dir)"_s;
	constexpr auto addFileQuery = u8"insert or replace into files (file, dir, type, size, last_modified, crc64) values (?, ?, ?, ?, ?, ?)"_s;
	constexpr auto getFileQuery = u8"select * from files where file = ? and dir = ?"_s;
	constexpr auto getDirFilesQuery = u8"select file, type, size, last_modified, crc64 from files where dir = ?"_s;
	constexpr auto getDirsQuery = u8"select distinct dir from files"_s;

	int result;

	logTrace("Opening connection to the DB "_s, dbPathInUtf8);
	result = sqlite3_open(dbPathInUtf8, &m_conn);
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
	assert(data.type == FileType::file || data.type == FileType::dir);

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
	result = sqlite3_bind_int(m_addFileStmt, 3, static_cast<int>(data.type));
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 4..."_s);
	switch (data.type) {
	case FileType::file:
		result = sqlite3_bind_int64(m_addFileStmt, 4, static_cast<sqlite_int64>(data.fileSize));
		break;
	case FileType::dir:
		result = sqlite3_bind_null(m_addFileStmt, 4);
		break;
	default:
		assert(false);
	}
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 5..."_s);
	switch (data.type) {
	case FileType::file:
		result = sqlite3_bind_int64(m_addFileStmt, 5, static_cast<sqlite_int64>(data.lastModifiedTS.millis() / 1000));
		break;
	case FileType::dir:
		result = sqlite3_bind_null(m_addFileStmt, 5);
		break;
	default:
		assert(false);
	}
	if (result != SQLITE_OK) {
		goto handle_error;
	}

	logTrace("Binding statement param 6..."_s);
	switch (data.type) {
	case FileType::file:
		result = sqlite3_bind_blob(m_addFileStmt, 6, data.crc64, sizeof(data.crc64), SQLITE_STATIC);
		break;
	case FileType::dir:
		result = sqlite3_bind_null(m_addFileStmt, 6);
		break;
	default:
		assert(false);
	}
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
	using CRC64View = afc::logger::HexEncodedN<sizeof(mirror::FileRecord::crc64)>;

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
			assert(sqlite3_column_int(m_getDirFilesStmt, 1) == FileType::file ||
					sqlite3_column_int(m_getDirFilesStmt, 1) == FileType::dir);

			const char * const fileNameU8 = reinterpret_cast<const char *>(sqlite3_column_text(m_getDirFilesStmt, 0));
			std::size_t fileNameU8Size = sqlite3_column_bytes(m_getDirFilesStmt, 0);
			FileRecord &fileRec = dest[PathKey(fileNameU8, fileNameU8Size)];
			fileRec.type = static_cast<FileType>(sqlite3_column_int(m_getDirFilesStmt, 1));

			switch (fileRec.type) {
			case FileType::file: {
				fileRec.fileSize = sqlite3_column_int64(m_getDirFilesStmt, 2);
				fileRec.lastModifiedTS.setMillis(sqlite3_column_int64(m_getDirFilesStmt, 3) * 1000);

				const unsigned char *crc64 = reinterpret_cast<const unsigned char *>(sqlite3_column_blob(m_getDirFilesStmt, 4));
				assert(sqlite3_column_bytes(m_getDirFilesStmt, 4) == sizeof(mirror::FileRecord::crc64));
				std::copy_n(crc64, sizeof(mirror::FileRecord::crc64), fileRec.crc64);

				logTrace("File found: {'"_s, Utf8ToSystemView(fileNameU8, fileNameU8Size), "', "_s,
						fileRec.fileSize, ", "_s, afc::ISODateTimeView(fileRec.lastModifiedTS), ", "_s,
						CRC64View(fileRec.crc64), "}..."_s);
				break;
			}
			case FileType::dir:
				logTrace("Dir found: {'"_s, Utf8ToSystemView(fileNameU8, fileNameU8Size), "'}..."_s);
				break;
			default:
				assert(false);
			}
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

			logTrace("Dir found: '"_s, Utf8ToSystemView(key.data, key.size), "'..."_s);

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
