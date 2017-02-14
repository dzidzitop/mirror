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
#include <afc/logger.hpp>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

using afc::operator"" _s;
using afc::logger::logDebug;

namespace
{
	template<typename FileOp>
	void scanFiles(const char * const rootDir, const char * const relDir, FileOp &fileOp)
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


		dirent *file;
		// TODO handle errors.
		while ((file = readdir(dir)) != nullptr) {
			const char *name;
			switch (file->d_type) {
			case DT_REG: {
				fileOp(rootDir, relDir, file->d_name);
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
					scanFiles(((std::string(rootDir) + '/') + name).c_str(), innerRelDir.c_str(), fileOp);
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
	}
}

void mirror::createDB(const char * const rootDir, mirror::FileDB &db)
{
	auto addFileOp = [&db] (const char * const rootDir, const char * const relDir, const char * const fileName)
	{
		logDebug("Adding the file '"_s, fileName, "' to the DB."_s);

		// TODO avoid unnecessary memory allocations.
		const std::string absolutePath = (std::string(rootDir) + '/') + fileName;
		const std::string relativePath = (std::string(relDir) + '/') + fileName;
		struct stat fileStat;
		const int result = lstat(absolutePath.c_str(), &fileStat);
		if (result != 0) {
			switch (errno) {
			case EACCES:
				// TODO make the behaviour configurable.
				logDebug("No access to '"_s, absolutePath.c_str(), '\'');
				return;
			default:
				// TODO handle error
				logDebug(errno);
				throw errno;
			}
		}
		// TODO check if this file is still a regular file.
		// TODO calculate MD5
		mirror::FileRecord fileRecord;
		fileRecord.fileSize = fileStat.st_size;
		fileRecord.lastModifiedTS.setMillis(static_cast<afc::Timestamp::time_type>(fileStat.st_mtime) * 1000);
		db.addFile(relativePath.c_str(), relativePath.size(), fileRecord);
	};

	scanFiles(rootDir, "", addFileOp);
}
