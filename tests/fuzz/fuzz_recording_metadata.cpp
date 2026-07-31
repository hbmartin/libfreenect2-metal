/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include <libfreenect2/recording_journal.h>
#include <libfreenect2/recording_manifest.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  const std::string input(reinterpret_cast<const char*>(data), size);
  libfreenect2::recording::ManifestV1 manifest;
  (void)libfreenect2::recording::parseManifestV1(input, manifest);
  libfreenect2::recording::JournalEntry entry;
  (void)libfreenect2::recording::parseJournalEntry(input, entry);
  return 0;
}
