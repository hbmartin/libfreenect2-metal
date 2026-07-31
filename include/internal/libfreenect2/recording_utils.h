/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RECORDING_UTILS_H_
#define LIBFREENECT2_RECORDING_UTILS_H_

#include <cstddef>
#include <string>
#include <vector>

namespace libfreenect2
{
namespace recording
{

std::string joinPath(const std::string& directory, const std::string& child);
bool directoryExists(const std::string& path);
bool createDirectories(const std::string& path, std::string* error = 0);
bool isSafeRelativePath(const std::string& path);

bool atomicRename(const std::string& source, const std::string& destination,
                  std::string* error = 0);
bool writeFileAtomically(const std::string& path, const unsigned char* data, size_t size,
                         std::string* error = 0);
bool writeFileAtomically(const std::string& path, const std::string& data, std::string* error = 0);

bool readFile(const std::string& path, std::vector<unsigned char>& data, std::string* error = 0);
bool regularFileSize(const std::string& path, size_t& size);

} // namespace recording
} // namespace libfreenect2

#endif // LIBFREENECT2_RECORDING_UTILS_H_
