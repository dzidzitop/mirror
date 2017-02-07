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
#include <clocale>
#include <exception>
#include <iostream>
#include "mirror/FileDB.hpp"

using std::operator<<;

int main(const int argc, char * argv[])
try {
	std::setlocale(LC_ALL, "");

	mirror::FileDB fileDB = mirror::FileDB::open("test.db");
	fileDB.close();
}
catch (std::exception &ex) {
	std::cerr << ex.what() << std::endl;
	return 1;
}
catch (const char * const ex) {
	std::cerr << ex << std::endl;
	return 1;
}
