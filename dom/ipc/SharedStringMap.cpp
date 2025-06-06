/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedStringMap.h"

#include "MemMapSnapshot.h"
#include "ScriptPreloader-inl.h"

#include "mozilla/BinarySearch.h"
#include "mozilla/Try.h"

using namespace mozilla::loader;

namespace mozilla {

using namespace ipc;

namespace dom::ipc {

static constexpr uint32_t kSharedStringMapMagic = 0x9e3779b9;

static inline size_t GetAlignmentOffset(size_t aOffset, size_t aAlign) {
  auto mod = aOffset % aAlign;
  return mod ? aAlign - mod : 0;
}

SharedStringMap::SharedStringMap(const ReadOnlySharedMemoryHandle& aMapHandle) {
  mHandle = aMapHandle.Clone();
  MOZ_RELEASE_ASSERT(mHandle.IsValid());
  auto mapping = aMapHandle.Map();
  MOZ_RELEASE_ASSERT(mapping.IsValid());

  // We return literal nsStrings and nsCStrings pointing to the mapped data,
  // which means that we may still have references to the mapped data even
  // after this instance is destroyed. That means that we need to keep the
  // mapping alive until process shutdown, in order to be safe.
  mMappedMemory = std::move(mapping).Release();

  MOZ_RELEASE_ASSERT(GetHeader().mMagic == kSharedStringMapMagic);
}

SharedStringMap::SharedStringMap(SharedStringMapBuilder&& aBuilder) {
  ReadOnlySharedMemoryMappingWithHandle mappingWithHandle;
  auto result = aBuilder.Finalize();
  MOZ_RELEASE_ASSERT(result.isOk());
  mHandle = result.unwrap();
  auto mapping = mHandle.Map();
  MOZ_RELEASE_ASSERT(mapping.IsValid());

  mMappedMemory = std::move(mapping).Release();

  MOZ_RELEASE_ASSERT(GetHeader().mMagic == kSharedStringMapMagic);
}

mozilla::ipc::ReadOnlySharedMemoryHandle SharedStringMap::CloneHandle() const {
  return mHandle.Clone();
}

bool SharedStringMap::Has(const nsCString& aKey) {
  size_t index;
  return Find(aKey, &index);
}

bool SharedStringMap::Get(const nsCString& aKey, nsAString& aValue) {
  const auto& entries = Entries();

  size_t index;
  if (!Find(aKey, &index)) {
    return false;
  }

  aValue.Assign(ValueTable().Get(entries[index].mValue));
  return true;
}

bool SharedStringMap::Find(const nsCString& aKey, size_t* aIndex) {
  const auto& keys = KeyTable();

  return BinarySearchIf(
      Entries(), 0, EntryCount(),
      [&](const Entry& aEntry) { return Compare(aKey, keys.Get(aEntry.mKey)); },
      aIndex);
}

void SharedStringMapBuilder::Add(const nsCString& aKey,
                                 const nsString& aValue) {
  mEntries.InsertOrUpdate(aKey,
                          Entry{mKeyTable.Add(aKey), mValueTable.Add(aValue)});
}

Result<ReadOnlySharedMemoryHandle, nsresult>
SharedStringMapBuilder::Finalize() {
  using Header = SharedStringMap::Header;

  MOZ_ASSERT(mEntries.Count() == mKeyTable.Count());

  auto keys = ToTArray<nsTArray<nsCString>>(mEntries.Keys());
  keys.Sort();

  Header header = {kSharedStringMapMagic, uint32_t(keys.Length())};

  size_t offset = sizeof(header);
  offset += GetAlignmentOffset(offset, alignof(Header));

  offset += keys.Length() * sizeof(SharedStringMap::Entry);

  header.mKeyStringsOffset = offset;
  header.mKeyStringsSize = mKeyTable.Size();

  offset += header.mKeyStringsSize;
  offset +=
      GetAlignmentOffset(offset, alignof(decltype(mValueTable)::ElemType));

  header.mValueStringsOffset = offset;
  header.mValueStringsSize = mValueTable.Size();

  offset += header.mValueStringsSize;

  MemMapSnapshot mem;
  MOZ_TRY(mem.Init(offset));

  auto headerPtr = mem.Get<Header>();
  headerPtr[0] = header;

  auto* entry = reinterpret_cast<Entry*>(&headerPtr[1]);
  for (auto& key : keys) {
    *entry++ = mEntries.Get(key);
  }

  auto ptr = mem.Get<uint8_t>();

  mKeyTable.Write({&ptr[header.mKeyStringsOffset], header.mKeyStringsSize});

  mValueTable.Write(
      {&ptr[header.mValueStringsOffset], header.mValueStringsSize});

  mKeyTable.Clear();
  mValueTable.Clear();
  mEntries.Clear();

  return mem.Finalize();
}

}  // namespace dom::ipc
}  // namespace mozilla
