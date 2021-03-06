set(LeapSerial_SRCS
  AESStream.h
  AESStream.cpp
  Allocation.h
  Allocation.cpp
  Archive.h
  Archive.cpp
  ArchiveFlatbuffer.h
  ArchiveFlatbuffer.cpp
  ArchiveLeapSerial.h
  ArchiveLeapSerial.cpp
  ArchiveJSON.h
  ArchiveJSON.cpp
  ArchiveProtobuf.h
  base.h
  BoundedStream.h
  BoundedStream.cpp
  BufferedInputStream.h
  BufferedInputStream.cpp
  BufferedStream.h
  BufferedStream.cpp
  CompressionStream.h
  CompressionStream.cpp
  Descriptor.h
  Descriptor.cpp
  descriptors.h
  descriptors.cpp
  field_descriptor.h
  field_serializer.h
  field_serializer_t.h
  FilterStreamBase.h
  FilterStreamBase.cpp
  ForwardingStream.h
  IArchiveProtobuf.h
  IArchiveProtobuf.cpp
  OArchiveProtobuf.h
  OArchiveProtobuf.cpp
  IArray.h
  IDictionary.h
  IInputStream.h
  IOutputStream.h
  LeapSerial.h
  MemoryStream.h
  MemoryStream.cpp
  ArchiveLeapSerialV0.h
  ArchiveLeapSerialV0.cpp
  optional.h
  ProtobufType.h
  ProtobufUtil.cpp
  ProtobufUtil.hpp
  SchemaWriterProtobuf.h
  SchemaWriterProtobuf.cpp
  serial_traits.h
  serialization_error.h
  serialization_error.cpp
  StreamAdapter.h
  StreamAdapter.cpp
  Utility.hpp
  Utility.cpp
)

add_pch(LeapSerial_SRCS "stdafx.h" "stdafx.cpp")
add_library(LeapSerial ${LeapSerial_SRCS})
target_link_libraries(LeapSerial aes zlib bz2)
target_include_directories(
  LeapSerial
  INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
  "$<INSTALL_INTERFACE:include>"
)

add_subdirectory(test)

# Install public header files
install_headers(TARGET LeapSerial DESTINATION include/LeapSerial COMPONENT LeapSerial)

install(TARGETS LeapSerial EXPORT LeapSerialTargets
  DESTINATION lib
  COMPONENT LeapSerial
  CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES}
)
