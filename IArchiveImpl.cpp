#include "stdafx.h"
#include "IArchiveImpl.h"
#include "field_serializer.h"
#include "serial_traits.h"
#include "LeapArchive.h"
#include <iostream>
#include <sstream>

using namespace leap;

IArchiveImpl::IArchiveImpl(std::istream& is, void* pRootObj) :
  is(is)
{
  // This sentry addition means we never have to test objId against zero
  objMap[0] = {nullptr, nullptr};

  // Object ID #1 is the root object
  objMap[1] = {pRootObj, nullptr};
}

IArchiveImpl::~IArchiveImpl(void) {
  ClearObjectTable();
}

void* IArchiveImpl::ReadObjectReference(const create_delete& cd,  const leap::field_serializer &sz) {
    // We expect to find an ID in the intput stream
  uint32_t objId;
  ReadByteArray(&objId, sizeof(objId));
  
  // Now we just perform a lookup into our archive and store the result here
  return Lookup(cd, sz, objId);
}

IArchive::ReleasedMemory IArchiveImpl::ReadObjectReferenceResponsible(ReleasedMemory(*pfnAlloc)(), const field_serializer& sz, bool isUnique) {
  // Object ID, then directed registration:
  uint32_t objId;
  ReadByteArray(&objId, sizeof(objId));
  
  // Verify no unique pointer aliases:
  if (isUnique && IsReleased(objId))
    throw std::runtime_error("Attempted to map the same object into two distinct unique pointers");

  return Release(pfnAlloc, sz, objId);
}

void* IArchiveImpl::Lookup(const create_delete& cd, const field_serializer& serializer, uint32_t objId) {
  auto q = objMap.find(objId);
  if (q != objMap.end())
    return q->second.pObject;

  // Not yet initialized, allocate and queue up
  auto& entry = objMap[objId];
  entry.pObject = cd.pfnAlloc();
  entry.pfnFree = cd.pfnFree;
  work.push(deserialization_task(&serializer, objId, entry.pObject));
  return entry.pObject;
}

void IArchiveImpl::ReadDescriptor(const descriptor& descriptor, void* pObj, uint64_t ncb) {
  uint64_t countLimit = Count() + ncb;
  for (const auto& field_descriptor : descriptor.field_descriptors)
    field_descriptor.serializer.deserialize(
    *this,
    static_cast<char*>(pObj)+field_descriptor.offset,
    0
    );

  if (!ncb)
    // Impossible for there to be more fields, we don't have a sizer
    return;

  // Identified fields, read them in
  while (Count() < countLimit) {
    // Ident/type field first
    uint64_t ident = ReadInteger(sizeof(uint64_t));
    uint64_t ncbChild;
    switch ((LeapArchive::serial_type)(ident & 7)) {
    case LeapArchive::serial_type::string:
      ncbChild = ReadInteger(sizeof(uint64_t));
      break;
    case LeapArchive::serial_type::b64:
      ncbChild = 8;
      break;
    case LeapArchive::serial_type::b32:
      ncbChild = 4;
      break;
    case LeapArchive::serial_type::varint:
      ncbChild = 0;
      break;
    default:
      throw std::runtime_error("Unexpected type field encountered");
    }

    // See if we can find the descriptor for this field:
    auto q = descriptor.identified_descriptors.find(ident >> 3);
    if (q == descriptor.identified_descriptors.end())
      // Unrecognized field, need to skip
      if (static_cast<LeapArchive::serial_type>(ident & 7) == LeapArchive::serial_type::varint)
        // Just read a varint in that we discard right away
        ReadInteger(sizeof(uint64_t));
      else
        // Skip the requisite number of bytes
        Skip(static_cast<size_t>(ncbChild));
    else
      // Hand off to child class
      q->second.serializer.deserialize(
      *this,
      static_cast<char*>(pObj)+q->second.offset,
      static_cast<size_t>(ncbChild)
      );
  }

  if (Count() > countLimit) {
    std::ostringstream os;
    os << "Deserialization error, read " << (Count() - countLimit + ncb) << " bytes and expected to read " << ncb << " bytes";
    throw std::runtime_error(os.str());
  }
}

void IArchiveImpl::ReadArray(const leap::field_serializer &sz, uint64_t n, std::function<void*()> enumerator) {
  // Read the number of entries first:
  uint32_t nEntries;
  ReadByteArray(&nEntries, sizeof(nEntries));
  
  if (nEntries != n)
    // We expected to get N entries, but got back some other value.  Something is wrong.
    throw std::runtime_error("Attempted to deserialize a non-matching number of entries into a fixed-size space");
  
    // Now loop until we get the desired number of entries from the stream
  for (size_t i = 0; i < n; i++)
    sz.deserialize(*this, enumerator(), 0);
}

void IArchiveImpl::ReadString(void *pBuf, size_t charCount, size_t charSize) {
  // Read the number of entries first:
  uint32_t nEntries;
  ReadByteArray(&nEntries, sizeof(nEntries));
  
  if (nEntries != charCount)
    // We expected to get N entries, but got back some other value.  Something is wrong.
    throw std::runtime_error("Attempted to deserialize a non-matching number of entries into a fixed-size space");
  
  ReadByteArray(pBuf, charCount * charSize);
}

void IArchiveImpl::ReadDictionary(const field_serializer& keyDesc, void* key,
                                  const field_serializer& valueDesc, void* value,
                                  std::function<void(const void* key, const void* value)> insertionFn)
{
  // Read the number of entries first:
  uint32_t nEntries;
  ReadByteArray(&nEntries, sizeof(nEntries));
  
  // Now read in all values:
  while (nEntries--) {
    keyDesc.deserialize(*this, key, 0);
    valueDesc.deserialize(*this, value, 0);
    insertionFn(key,value);
  }

}

IArchive::ReleasedMemory IArchiveImpl::Release(ReleasedMemory(*pfnAlloc)(), const field_serializer& serializer, uint32_t objId) {
  auto q = objMap.find(objId);
  if (q != objMap.end()) {
    // Object already allocated, we just need to remove control back to ourselves
    q->second.pfnFree = nullptr;
    return {q->second.pObject, q->second.pContext};
  }

  // Not yet initialized, allocate and queue up
  auto& entry = objMap[objId];
  IArchive::ReleasedMemory retVal = pfnAlloc();
  entry.pObject = retVal.pObject;
  entry.pContext = retVal.pContext;
  entry.pfnFree = nullptr;
  work.push(deserialization_task(&serializer, objId, entry.pObject));
  return retVal;
}

bool IArchiveImpl::IsReleased(uint32_t objId) {
  auto q = objMap.find(objId);
  return q != objMap.end() && !q->second.pfnFree;
}

void IArchiveImpl::ReadByteArray(void* pBuf, uint64_t ncb) {
  if(!is.read((char*) pBuf, ncb))
    throw std::runtime_error("End of file reached prematurely");
  m_count += ncb;
}

void IArchiveImpl::Skip(uint64_t ncb) {
  is.ignore(ncb);
  //  m_count += ncb;
}

void IArchiveImpl::Transfer(internal::AllocationBase& alloc) {
  for (auto& cur : objMap)
    if (cur.second.pfnFree)
      // Transfer cleanup responsibility to the allocator
      alloc.garbageList.push_back(
        std::make_pair(
          cur.second.pObject,
          cur.second.pfnFree
        )
      );

  objMap.clear();
}

bool IArchiveImpl::ReadBool() {
  bool b;
  ReadByteArray(&b, 1);
  return b;
}

uint64_t IArchiveImpl::ReadInteger(size_t) {
  uint8_t ch;
  uint64_t retVal = 0;
  size_t ncb = 0;
    
  do {
    ReadByteArray(&ch, 1);
    retVal |= uint64_t(ch & 0x7F) << (ncb * 7);;
    ++ncb;
  }
  while (ch & 0x80);

  return retVal;
}

size_t IArchiveImpl::ClearObjectTable(void) {
  size_t n = 0;
  for (auto& cur : objMap)
    if (cur.second.pfnFree) {
      // One more entry freed
      n++;

      // Transfer cleanup responsibility to the allocator
      cur.second.pfnFree(cur.second.pObject);
    }

  objMap.clear();
  return n;
}

void IArchiveImpl::Process(const deserialization_task& task) {
  objMap[1] = {task.pObject, nullptr};
  work.push(task);

  // Continue to work as long as there is work to be done
  for(; !work.empty(); work.pop()) {
    deserialization_task& task = work.front();

    // Identifier/type comes first
    auto id_type = ReadInteger(8);

    // Then we need the size (if it's available)
    size_t ncb;
    switch (static_cast<LeapArchive::serial_type>(id_type & 7)) {
    case LeapArchive::serial_type::b32:
      ncb = 4;
      break;
    case LeapArchive::serial_type::b64:
      ncb = 8;
      break;
    case LeapArchive::serial_type::string:
      // Size fits right here
      ncb = static_cast<size_t>(ReadInteger(sizeof(ncb)));
      break;
    case LeapArchive::serial_type::varint:
      // No idea how much, leave it to the consumer
      ncb = 0;
      break;
    case LeapArchive::serial_type::ignored:
      // Shold never happen, but if it does, then read no bytes
      ncb = 0;
      break;
    }

    task.serializer->deserialize(*this, task.pObject, ncb);
  }
}
