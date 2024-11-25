/*!
 *  @file FrameData.h
 *  @author Paul
 *  @date 2024-11-25
 *
 *  Frame data implementation
 */
#pragma once

#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <string_view>
#include <type_traits>

#ifndef __FUNCSIG__
#define __FUNCSIG__ __PRETTY_FUNCTION__
#endif

namespace Memory
{
	inline void* AlignedAlloc(std::size_t InSize, std::size_t InAlign)
	{
#ifdef _MSC_VER
		return _aligned_malloc(InSize, InAlign);
#else
		return aligned_alloc(InAlign, InSize);
#endif
	}
}

namespace Details
{
	template <typename T>
	constexpr T Align(T Val, uint64_t Alignment)
	{
		static_assert(std::is_integral<T>() || std::is_pointer<T>(), "Align expects an integer or pointer type");

		return (T)(((uint64_t)Val + Alignment - 1) & ~(Alignment - 1));
	}

	template<typename T>
	static constexpr uint64_t ComputeHash()
	{
		constexpr std::string_view TypeStr = __FUNCSIG__;
		uint64_t Hash = 0xCBF29CE484222325;
		const uint64_t Prime = 0x100000001B3;

		for (size_t Index = 0; Index < TypeStr.size(); ++Index)
		{
			Hash = Hash ^ uint8_t(TypeStr.at(Index));
			Hash *= Prime;
		}

		return Hash;
	}
}

template<typename T>
struct TTypeIndex
{
	static constexpr uint64_t Value = Details::ComputeHash<T>();
};

class FFrameData
{
public:
	template<class T>
	static constexpr bool Supports()
	{
		return std::is_trivially_destructible<T>();
	}

	template<class T>
	class TIterator
	{
	public:
		using Reference = T&;
		using Pointer = T*;


		TIterator() = default;
		TIterator(void* const* InOffset) : Offset(InOffset) {}

		Reference operator*() const { return *reinterpret_cast<Pointer>(*Offset); }
		Pointer operator->() const { return reinterpret_cast<Pointer>(*Offset); }

		TIterator& operator++() { ++Offset; return *this; }
		friend bool operator== (const TIterator& a, const TIterator& b) { return a.Offset == b.Offset; };
		friend bool operator!= (const TIterator& a, const TIterator& b) { return a.Offset != b.Offset; };

	private:
		void* const* Offset;
	};

	template<class T>
	class TIteratorRange
	{
	public:

		TIteratorRange() = default;
		TIteratorRange(void* const* InBegin, void* const* InEnd)
			: Begin(InBegin)
			, End(InEnd)
		{
		}

		TIterator<T> begin() const { return TIterator<T>(Begin); }
		TIterator<T> end() const { return TIterator<T>(End); }

	private:
		TIterator<T> Begin;
		TIterator<T> End;
	};


public:
	static constexpr uint32_t ChunkAlignment = 64;

public:
	explicit FFrameData(uint32_t InChunkSize)
	{
		ChunkSize = InChunkSize;
	}

	FFrameData(const FFrameData&) = delete;
	FFrameData& operator=(const FFrameData&) = delete;

	FFrameData(FFrameData&& InOther)
		: Chunks(std::move(InOther.Chunks))
		, TypeMap(std::move(InOther.TypeMap))
		, Head(InOther.Head)
		, ChunkHead(InOther.ChunkHead)
		, ChunkSize(InOther.ChunkSize)
	{
		InOther.Chunks.clear();
	}

	~FFrameData()
	{
		Clear(0);
	}

public:

	template<class T>
	void Push(const T& InValue)
	{
		static_assert(Supports<T>(), "Unsupported type T");
		static_assert(alignof(T) <= ChunkAlignment, "T won't fit");

		void* Ptr = Allocate(sizeof(T), alignof(T));
		new (Ptr) T{ InValue };

		constexpr uint64_t Index = TTypeIndex<T>::Value;
		if (auto Search = TypeMap.find(Index); Search != TypeMap.end())
		{
			Search->second.push_back(Ptr);
		}
		else
		{
			TypeMap.insert({ Index, { Ptr } });
		}
	}

	template<class T>
	[[nodiscard]] TIteratorRange<const T> Data() const
	{
		if (auto Search = TypeMap.find(TTypeIndex<T>::Value); Search != TypeMap.end())
		{
			return TIteratorRange<const T>(Search->second.data(), Search->second.data() + Search->second.size());

		}

		return TIteratorRange<const T>();
	}

	void Clear(size_t InSlack = 0)
	{
		Head = 0;
		ChunkHead = 0;
		TypeMap.clear();

		if (InSlack < Chunks.size())
		{
			// remove N last chunks
			for (size_t i = InSlack; i < Chunks.size(); ++i)
				std::free(Chunks[i]);

			Chunks.resize(InSlack);
		}
		else
		{
			const size_t BaseCount = Chunks.size();
			Chunks.resize(InSlack);

			// allocate N new chunks
			for (size_t i = BaseCount; i < InSlack; ++i)
				Chunks[i] = Memory::AlignedAlloc(ChunkSize, ChunkAlignment);
		}
	}

private:
	[[nodiscard]] void* Allocate(size_t InSize, size_t InAlign)
	{
		if (Chunks.size() == 0)
		{
			// Push base chunk
			PushChunk();
		}

		uint32_t Offset = Details::Align(Head, InAlign);
		if (Offset + InSize > ChunkSize)
		{
			// Chunk is full
			Head = 0;
			Offset = 0; // Aligned

			if (++ChunkHead >= Chunks.size())
			{
				// No more available chunks. Push a new one
				PushChunk();
			}

		}

		// Update Head
		Head = Offset + InSize;

		uint8_t* ChunkPtr = (uint8_t*)Chunks[ChunkHead];
		return ChunkPtr + Offset;
	}

	inline void PushChunk()
	{
		Chunks.push_back(Memory::AlignedAlloc(ChunkSize, ChunkAlignment));
	}

private:
	// Mapping type index -> adresses in chunks
	std::unordered_map<uint64_t, std::vector<void*>> TypeMap;
	// Memory chunks
	std::vector<void*> Chunks;
	uint32_t ChunkSize = 0;
	uint32_t Head = 0;
	uint32_t ChunkHead = 0;
};