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

#include <afc/logger.hpp>
#include <afc/utils.h>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace mirror
{
	// TODO think of caching C string directry so save a few CPU ticks.
	static afc::String systemEncoding;

	struct TextHolder
	{
		TextHolder(const char * const text, const std::size_t n, const bool isOwner) noexcept
				: value(text), size(n), owner(isOwner) {}
		TextHolder(const TextHolder &) = delete;
		TextHolder(TextHolder &&o) : value(o.value), size(o.size), owner(o.owner) { o.owner = false; }

		TextHolder &operator=(const TextHolder &) = delete;
		TextHolder &operator=(TextHolder &&o)
		{
			value = o.value;
			size = o.size;
			owner = o.owner;
			o.owner = false;
			return *this;
		}

		~TextHolder() { if (owner) { std::free(const_cast<char *>(value)); } }

		const char *value;
		std::size_t size;
		bool owner;
	};

	// TODO implement conversion to mirror::PathKey to minimise copying/moving data.
	typedef TextHolder (*convert)(const char *src, std::size_t srcSize);

	extern convert convertToUtf8;
	extern convert convertFromUtf8;

	inline TextHolder nopConverter(const char * const src, std::size_t srcSize)
	{
		assert(std::strcmp("UTF-8", systemEncoding.c_str()) == 0);
		return TextHolder(src, srcSize, false);
	}

	inline TextHolder trueConvertToUtf8(const char * const src, std::size_t srcSize)
	{
		afc::U8String result(afc::convertToUtf8(src, srcSize, systemEncoding.c_str()));
		const std::size_t size = result.size();

		return TextHolder(result.detach(), size, true);
	}

	inline TextHolder trueConvertFromUtf8(const char * const src, std::size_t srcSize)
	{
		afc::String result(afc::convertFromUtf8(src, srcSize, systemEncoding.c_str()));
		const std::size_t size = result.size();

		return TextHolder(result.detach(), size, true);
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

	struct Utf8ToSystemView
	{
		Utf8ToSystemView(const char * const strU8, std::size_t n) noexcept : text(strU8), size(n) {}

		const char *text;
		std::size_t size;
	};
}

namespace afc
{
	namespace logger
	{
		template<>
		inline bool logPrint<const mirror::Utf8ToSystemView &>(const mirror::Utf8ToSystemView &val,
				std::FILE * const dest)
		{
			const mirror::TextHolder data = mirror::convertFromUtf8(val.text, val.size);

			return logText(data.value, data.size, dest);
		}
	}
}

#endif // MIRROR_ENCODING_HPP_
