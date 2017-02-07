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
#ifndef MIRROR_FILEDB_H_
#define MIRROR_FILEDB_H_

#include <cstddef>
#include <afc/md5.hpp>
#include <openssl/md5.h>
#include <afc/dateutil.hpp>
#include <afc/string_util.hpp>
#include <afc/utils.h>
#include <sqlite3.h>
#include <sys/types.h>

namespace mirror
{

struct FileRecord
{
	unsigned char md5Digest[MD5_DIGEST_LENGTH];
	afc::Timestamp lastModifiedTS;
	off_t fileSize;
};

class FileDB
{
private:
	FileDB(const FileDB &) = delete;
	FileDB &operator=(const FileDB &) = delete;
	FileDB &operator=(FileDB &&) = delete;

	FileDB(sqlite3 *db) : m_db(db) {}
public:
	FileDB(FileDB &&src) : m_db(src.m_db) { src.m_db = nullptr; }

	~FileDB()
	{
		assert(m_db == nullptr);
	}

	static FileDB open(const char * const fileName, const bool create = false)
	{
		sqlite3 *db;
		int result = sqlite3_open(afc::convertToUtf8(fileName, afc::systemCharset().c_str()).c_str(), &db);

		if (result == SQLITE_OK) {
			result = sqlite3_exec(db, u8"create table if not exists files "
							"(path primary key asc, md5 blob not null, last_modified integer not null);",
					nullptr, nullptr, nullptr);
		}

		if (result == SQLITE_OK) {
			return FileDB(db);
		}

		// TODO handle error.
		throw result;
	}

	void close()
	{
		const int result = sqlite3_close(m_db);

		m_db = nullptr;

		if (result != SQLITE_OK) {
			// TODO handle error.
			throw result;
		}
	}

	void addFileRecord(const char *fileName, const std::size_t fileNameSize, const FileRecord &data);
	void getFileRecord(const char *fileName, const std::size_t fileNameSize, FileRecord &dest);
private:
	sqlite3 *m_db;
};

}

#endif // MIRROR_FILEDB_H_
