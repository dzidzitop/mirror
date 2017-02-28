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
#ifndef MIRROR_ENCODING_HPP_
#define MIRROR_ENCODING_HPP_

#include <afc/utils.h>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace mirror
{
	// TODO think of caching C string directry so save a few CPU ticks.
	static afc::String systemEncoding;

	struct TextDeleter
	{
		void operator()(const char * const ptr) { std::free(const_cast<char *>(ptr)); }
	};

	using TextGuard = std::unique_ptr<const char, TextDeleter>;

	typedef TextGuard (*convert)(const char *src, std::size_t srcSize,
			const char *&dest, std::size_t &destSize);

	static convert convertToUtf8 = nullptr;
	static convert convertFromUtf8 = nullptr;

	inline TextGuard nopConverter(const char * const src, std::size_t srcSize,
			const char *&dest, std::size_t &destSize)
	{
		assert(std::strcmp("UTF-8", systemEncoding) == 0);
		dest = src == nullptr ? "" : src;
		destSize = srcSize;
		return TextGuard(nullptr);
	}

	inline TextGuard trueConvertToUtf8(const char * const src, std::size_t srcSize,
			const char *&dest, std::size_t &destSize)
	{
		afc::U8String result(afc::convertToUtf8(src, srcSize, systemEncoding.c_str()));
		dest = result.c_str();
		destSize = result.size();
		return TextGuard(result.detach());
	}

	inline TextGuard trueConvertFromUtf8(const char * const src, std::size_t srcSize,
			const char *&dest, std::size_t &destSize)
	{
		afc::String result(afc::convertFromUtf8(src, srcSize, systemEncoding.c_str()));
		dest = result.c_str();
		destSize = result.size();
		return TextGuard(result.detach());
	}

	inline void initConverters()
	{
		systemEncoding = afc::systemCharset();
		if (std::strcmp("UTF-8", systemEncoding.c_str()) == 0) {
			convertToUtf8 = nopConverter;
			convertFromUtf8 = nopConverter;
		} else {
			convertToUtf8 = trueConvertToUtf8;
			convertFromUtf8 = trueConvertFromUtf8;
		}
	}
}

#endif // MIRROR_ENCODING_HPP_
