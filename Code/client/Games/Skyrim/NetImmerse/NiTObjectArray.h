#pragma once

#include <cstddef>
#include <cstdint>

#include <Games/Memory.h>

template <typename T> struct NiTObjectArray
{
    T* _data;
    uint16_t _capacity;
    uint16_t _freeIdx;
    uint16_t _size;
    uint16_t _growthSize;

    NiTObjectArray()
        : _data{nullptr}
        , _capacity{0}
        , _freeIdx{0}
        , _size{0}
        , _growthSize{0}
    {
    }

    virtual ~NiTObjectArray() = default;

    T& operator[](uint32_t aIndex) { return _data[aIndex]; }
    const T& operator[](uint32_t aIndex) const { return _data[aIndex]; }

    [[nodiscard]] T* Data() noexcept { return _data; }
    [[nodiscard]] const T* Data() const noexcept { return _data; }
    [[nodiscard]] uint16_t Capacity() const noexcept { return _capacity; }
    [[nodiscard]] uint16_t Size() const noexcept { return _size; }

    // Range for loop compatibility
    struct Iterator
    {
        Iterator(T* apEntry)
            : m_pEntry(apEntry)
        {
        }
        Iterator operator++()
        {
            ++m_pEntry;
            return *this;
        }
        bool operator!=(const Iterator& acRhs) const { return m_pEntry != acRhs.m_pEntry; }
        const T& operator*() const { return *m_pEntry; }

    private:
        T* m_pEntry;
    };

    Iterator begin() { return Iterator(_data); }

    Iterator end() { return Iterator(_data + _capacity); }

    inline bool Empty() const noexcept { return _capacity == 0; }
};

static_assert(sizeof(NiTObjectArray<uintptr_t>) == 24);
static_assert(offsetof(NiTObjectArray<uintptr_t>, _data) == 0x8);
static_assert(offsetof(NiTObjectArray<uintptr_t>, _capacity) == 0x10);
static_assert(offsetof(NiTObjectArray<uintptr_t>, _freeIdx) == 0x12);
static_assert(offsetof(NiTObjectArray<uintptr_t>, _size) == 0x14);
static_assert(offsetof(NiTObjectArray<uintptr_t>, _growthSize) == 0x16);
