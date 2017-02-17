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
#include <afc/StringRef.hpp>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <dirent.h>
#include "FileDB.hpp"
#include <string>

namespace mirror
{
	void createDB(const char *rootDir, mirror::FileDB &db);

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

		template<typename FileOp>
		void scanFiles(const char * const rootDir, const char * const relDir, FileOp &fileOp);
	}
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

template<typename FileOp>
void mirror::_helper::scanFiles(const char * const rootDir, const char * const relDir, FileOp &fileOp)
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

#endif // MIRROR_UTILS_HPP_
