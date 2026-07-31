/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/recording_utils.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace libfreenect2
{
namespace recording
{
namespace
{

bool isSeparator(char character)
{
  return character == '/' || character == '\\';
}

std::string parentPath(const std::string& path)
{
  const std::string::size_type separator = path.find_last_of("/\\");
  if (separator == std::string::npos)
    return std::string();
  if (separator == 0)
    return path.substr(0, 1);
#if defined(_WIN32)
  if (separator == 2 && path.size() > 2 && path[1] == ':')
    return path.substr(0, 3);
#endif
  return path.substr(0, separator);
}

void setErrnoError(const std::string& operation, const std::string& path, std::string* error)
{
  if (error == 0)
    return;
  std::ostringstream message;
  message << operation << " '" << path << "': " << std::strerror(errno);
  *error = message.str();
}

} // namespace

std::string joinPath(const std::string& directory, const std::string& child)
{
  if (directory.empty())
    return child;
  if (child.empty())
    return directory;
  if (isSeparator(directory[directory.size() - 1]))
    return directory + child;
#if defined(_WIN32)
  return directory + "\\" + child;
#else
  return directory + "/" + child;
#endif
}

bool directoryExists(const std::string& path)
{
#if defined(_WIN32)
  struct _stat64 status;
  return _stat64(path.c_str(), &status) == 0 && (status.st_mode & _S_IFDIR) != 0;
#else
  struct stat status;
  return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

bool createDirectories(const std::string& path, std::string* error)
{
  if (path.empty())
  {
    if (error != 0)
      *error = "directory path is empty";
    return false;
  }
  if (directoryExists(path))
    return true;

  const std::string parent = parentPath(path);
  if (!parent.empty() && parent != path && !directoryExists(parent) &&
      !createDirectories(parent, error))
    return false;

#if defined(_WIN32)
  const int result = _mkdir(path.c_str());
#else
  const int result = mkdir(path.c_str(), 0777);
#endif
  if (result == 0)
    return syncDirectory(parent.empty() ? "." : parent, error);
  if (errno == EEXIST && directoryExists(path))
    return true;
  setErrnoError("failed to create directory", path, error);
  return false;
}

bool isSafeRelativePath(const std::string& path)
{
  if (path.empty() || isSeparator(path[0]) || path.find('\0') != std::string::npos)
    return false;
  if (path.find(':') != std::string::npos)
    return false;

  std::string component;
  for (size_t index = 0; index <= path.size(); ++index)
  {
    if (index == path.size() || isSeparator(path[index]))
    {
      if (component.empty() || component == "." || component == "..")
        return false;
      component.clear();
    }
    else
    {
      component.push_back(path[index]);
    }
  }
  return true;
}

bool syncFile(const std::string& path, std::string* error)
{
#if defined(_WIN32)
  HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE)
  {
    if (error != 0)
    {
      std::ostringstream message;
      message << "failed to open file for synchronization '" << path << "': Windows error "
              << GetLastError();
      *error = message.str();
    }
    return false;
  }
  const bool success = FlushFileBuffers(handle) != 0;
  const DWORD flush_error = success ? ERROR_SUCCESS : GetLastError();
  CloseHandle(handle);
  if (!success && error != 0)
  {
    std::ostringstream message;
    message << "failed to synchronize file '" << path << "': Windows error " << flush_error;
    *error = message.str();
  }
  return success;
#else
  const int descriptor = open(path.c_str(), O_RDONLY);
  if (descriptor < 0)
  {
    setErrnoError("failed to open file for synchronization", path, error);
    return false;
  }
  int result = 0;
  do
  {
    result = fsync(descriptor);
  } while (result != 0 && errno == EINTR);
  const int saved_errno = errno;
  close(descriptor);
  if (result == 0)
    return true;
  errno = saved_errno;
  setErrnoError("failed to synchronize file", path, error);
  return false;
#endif
}

bool syncDirectory(const std::string& path, std::string* error)
{
#if defined(_WIN32)
  // MoveFileEx with MOVEFILE_WRITE_THROUGH provides the publication barrier
  // used by atomicRename on Windows. Directory handles cannot be flushed on
  // every supported Windows filesystem.
  (void)path;
  (void)error;
  return true;
#else
#if defined(O_DIRECTORY)
  const int descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY);
#else
  const int descriptor = open(path.c_str(), O_RDONLY);
#endif
  if (descriptor < 0)
  {
    setErrnoError("failed to open directory for synchronization", path, error);
    return false;
  }
  int result = 0;
  do
  {
    result = fsync(descriptor);
  } while (result != 0 && errno == EINTR);
  const int saved_errno = errno;
  close(descriptor);
  if (result == 0)
    return true;
  errno = saved_errno;
  setErrnoError("failed to synchronize directory", path, error);
  return false;
#endif
}

bool atomicRename(const std::string& source, const std::string& destination, std::string* error)
{
#if defined(_WIN32)
  if (MoveFileExA(source.c_str(), destination.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
    return true;
  if (error != 0)
  {
    std::ostringstream message;
    message << "failed to publish '" << destination << "': Windows error " << GetLastError();
    *error = message.str();
  }
  return false;
#else
  if (std::rename(source.c_str(), destination.c_str()) != 0)
  {
    setErrnoError("failed to publish", destination, error);
    return false;
  }
  return syncDirectory(parentPath(destination).empty() ? "." : parentPath(destination), error);
#endif
}

bool writeFileAtomically(const std::string& path, const unsigned char* data, size_t size,
                         std::string* error)
{
  if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
  {
    if (error != 0)
      *error = "file is too large to write atomically '" + path + "'";
    return false;
  }

  const std::string part_path = path + ".part";
  std::ofstream output(part_path.c_str(), std::ios::binary | std::ios::trunc);
  if (!output)
  {
    setErrnoError("failed to open temporary file", part_path, error);
    return false;
  }
  if (size != 0)
    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  output.flush();
  bool written = output.good();
  output.close();
  written = written && !output.fail();
  if (!written)
  {
    if (error != 0)
      *error = "failed to write temporary file '" + part_path + "'";
    std::remove(part_path.c_str());
    return false;
  }
  if (!syncFile(part_path, error))
  {
    std::remove(part_path.c_str());
    return false;
  }
  if (!atomicRename(part_path, path, error))
  {
    std::remove(part_path.c_str());
    return false;
  }
  return true;
}

bool writeFileAtomically(const std::string& path, const std::string& data, std::string* error)
{
  return writeFileAtomically(path, reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                             error);
}

bool readFile(const std::string& path, std::vector<unsigned char>& data, std::string* error)
{
  std::ifstream input(path.c_str(), std::ios::binary);
  if (!input)
  {
    setErrnoError("failed to open file", path, error);
    return false;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff end = input.tellg();
  if (end < 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max() ||
      end > std::numeric_limits<std::streamsize>::max())
  {
    if (error != 0)
      *error = "file size is not representable for '" + path + "'";
    return false;
  }
  data.resize(static_cast<size_t>(end));
  input.seekg(0, std::ios::beg);
  if (!data.empty())
    input.read(reinterpret_cast<char*>(&data[0]), static_cast<std::streamsize>(data.size()));
  if (!input && !data.empty())
  {
    if (error != 0)
      *error = "failed to read file '" + path + "'";
    data.clear();
    return false;
  }
  return true;
}

bool regularFileSize(const std::string& path, size_t& size)
{
#if defined(_WIN32)
  struct _stat64 status;
  if (_stat64(path.c_str(), &status) != 0 || (status.st_mode & _S_IFREG) == 0 || status.st_size < 0)
    return false;
#else
  struct stat status;
  if (stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0)
    return false;
#endif
  if (static_cast<uint64_t>(status.st_size) > std::numeric_limits<size_t>::max())
    return false;
  size = static_cast<size_t>(status.st_size);
  return true;
}

} // namespace recording
} // namespace libfreenect2
