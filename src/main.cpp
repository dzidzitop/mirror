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
#include <afc/dateutil.hpp>
#include <afc/logger.hpp>
#include <afc/utils.h>
#include <cassert>
#include <clocale>
#include <cstring>
#include <exception>
#include <getopt.h>
#include <iostream>
#include "mirror/encoding.hpp"
#include "mirror/FileDB.hpp"
#include "mirror/utils.hpp"
#include "mirror/version.hpp"
#include <string>

using afc::operator"" _s;

namespace
{
// TODO resolve it dynamically using argv[0]?
const char * const programName = "mirror";
const int getopt_tagStartValue = 1000;

static const struct option options[] = {
	{"tool", required_argument, nullptr, 't'},
	{"help", no_argument, nullptr, 'h'},
	{"version", no_argument, nullptr, 'v'},
	{"db", required_argument, nullptr, 'd'},
	{0}
};

enum class tool
{
	undefined, createDB, verifyDir, mergeDir
};

void printUsage(bool success, const char * const programName = ::programName)
{
	using std::operator<<;

	if (!success) {
		std::cout << "Try '" << programName << " --help' for more information." << std::endl;
	} else {
		std::cout <<
"Usage: " << programName << " --tool=[TOOL TO USE] [OPTION]... SOURCE [DEST]\n\
\n\
Report " << programName << " bugs to dzidzitop@vfemail.net" << std::endl;
	}
}

void printVersion()
{
	using std::operator<<;

	afc::String author;
	try {
		const char16_t name[] = u"D\u017Amitry La\u016D\u010Duk";
		// TODO reuse system charset.
		author = afc::utf16leToString(name, sizeof(name) - 1, afc::systemCharset().c_str());
	}
	catch (...) {
		author = "Dzmitry Liauchuk"_s;
	}
	const char * const authorPtr = author.c_str();
	std::cout << mirror::PROGRAM_NAME << " " << mirror::PROGRAM_VERSION << "\n\
Copyright (C) 2017 " << authorPtr << ".\n\
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n\
\n\
Written by " << authorPtr << '.' << std::endl;
}

}

int main(const int argc, char * const argv[])
try {
	using std::operator<<;

	std::setlocale(LC_ALL, "");
	mirror::initConverters();

	tool t = tool::undefined;
	bool toolDefined = false;
	int c;
	int optionIndex = -1;
	const char *dbPath;
	bool dbDefined = false;
	while ((c = ::getopt_long(argc, argv, "h", options, &optionIndex)) != -1) {
		switch (c) {
		case 'd':
			dbPath = ::optarg;
			dbDefined = true;
			break;
		case 'h':
			printUsage(true);
			return 0;
		case 't':
			if (std::strcmp(::optarg, "create-db") == 0) {
				t = tool::createDB;
			} else if (std::strcmp(::optarg, "verify-dir") == 0) {
				t = tool::verifyDir;
			} else if (std::strcmp(::optarg, "merge-dir") == 0) {
				t = tool::mergeDir;
			} else {
				printUsage(false, mirror::PROGRAM_NAME);
				return 1;
			}
			toolDefined = true;
			break;
		case 'v':
			printVersion();
			return 0;
		case '?':
			// getopt_long takes care of informing the user about the error option
			printUsage(false);
			return 1;
		default:
			std::cerr << "Unhandled option: ";
			if (optionIndex == -1) {
				std::cerr << '-' << static_cast<char>(c);
			} else {
				std::cerr << "--" << options[optionIndex].name;
			}
			std::cerr << std::endl;
			return 1;
		}
		optionIndex = -1;
	}
	if (optind == argc) {
		std::cerr << "No SOURCE file/directory." << std::endl;
		printUsage(false);
		return 1;
	}
	if (optind < argc - 2) {
		std::cerr << "Only SOURCE and DEST files/directories can be specified." << std::endl;
		printUsage(false);
		return 1;
	}
	if (t == tool::verifyDir && optind < argc - 1) {
		std::cerr << "SOURCE files/directories must be specified for verify-dir." << std::endl;
		printUsage(false);
		return 1;
	}
	if (t == tool::mergeDir && optind < argc - 2) {
		std::cerr << "SOURCE and DEST files/directories must be specified for merge-dir." << std::endl;
		printUsage(false);
		return 1;
	}

	if (!toolDefined) {
		std::cerr << "No tool specified." << std::endl;
		printUsage(false);
		return 1;
	}
	if (!dbDefined) {
		std::cerr << "No DB specified." << std::endl;
		printUsage(false);
		return 1;
	}

	const char * const src = argv[optind];
	const char * const dest = argv[optind + 1];
	mirror::FileDB db = mirror::FileDB::open(dbPath, true);

		switch (t) {
		case tool::createDB:
			mirror::createDB(src, strlen(src), db);
			break;
		case tool::verifyDir: {
			mirror::VerifyDirMismatchHandler mismatchHandler;
			mirror::checkFileSystem(src, strlen(src), db, mismatchHandler);
			break;
		}
		case tool::mergeDir: {
			mirror::MergeDirMismatchHandler mismatchHandler(src, dest);
			mirror::checkFileSystem(dest, strlen(dest), db, mismatchHandler);
			break;
		}
		default:
			assert(false);
		}

	db.close();

	return 0;
}
catch (std::exception &ex) {
	using std::operator<<;

	std::cerr << ex.what() << std::endl;
	return 1;
}
catch (const char * const ex) {
	using std::operator<<;

	std::cerr << ex << std::endl;
	return 1;
}
