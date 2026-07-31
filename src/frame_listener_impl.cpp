/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

/** @file frame_listener_impl.cpp Implementation classes for frame listeners. */

#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/threading.h>
#include <libfreenect2/timing.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <vector>

namespace libfreenect2
{

Frame::Frame(size_t width_arg, size_t height_arg, size_t bytes_per_pixel_arg,
             unsigned char *data_arg) :
  width(width_arg),
  height(height_arg),
  bytes_per_pixel(bytes_per_pixel_arg),
  data(data_arg),
  timestamp(0),
  arrival_timestamp_us(0),
  sequence(0),
  exposure(0.f),
  gain(0.f),
  gamma(0.f),
  status(0),
  format(Frame::Invalid),
  rawdata(NULL)
{
  if (data_arg)
    return;
  const size_t alignment = 64;
  size_t space = width * height * bytes_per_pixel + alignment;
  rawdata = new unsigned char[space];
  uintptr_t ptr = reinterpret_cast<uintptr_t>(rawdata);
  uintptr_t aligned = (ptr - 1u + alignment) & -alignment;
  data = reinterpret_cast<unsigned char *>(aligned);
}

Frame::~Frame()
{
  delete[] rawdata;
}

FrameListener::~FrameListener() {}

/** Implementation class for synchronizing different types of frames. */
class SyncMultiFrameListenerImpl
{
public:
  libfreenect2::mutex mutex_;
  libfreenect2::condition_variable condition_;
  FrameMap next_frame_;

  const unsigned int subscribed_frame_types_;
  unsigned int ready_frame_types_;
  bool current_frame_released_;

  SyncMultiFrameListenerImpl(unsigned int frame_types) :
    subscribed_frame_types_(frame_types),
    ready_frame_types_(0),
    current_frame_released_(true)
  {
  }

  bool hasNewFrame() const
  {
    return ready_frame_types_ == subscribed_frame_types_;
  }
};

SyncMultiFrameListener::SyncMultiFrameListener(unsigned int frame_types) :
    impl_(new SyncMultiFrameListenerImpl(frame_types))
{
}

SyncMultiFrameListener::~SyncMultiFrameListener()
{
  release(impl_->next_frame_);
  delete impl_;
}

bool SyncMultiFrameListener::hasNewFrame() const
{
  libfreenect2::unique_lock l(impl_->mutex_);

  return impl_->hasNewFrame();
}

bool SyncMultiFrameListener::waitForNewFrame(FrameMap &frame, int milliseconds)
{
#ifdef LIBFREENECT2_THREADING_STDLIB
  libfreenect2::unique_lock l(impl_->mutex_);

  auto predicate = [this]{ return impl_->hasNewFrame(); };

  if(impl_->condition_.wait_for(l, std::chrono::milliseconds(milliseconds), predicate))
  {
    frame = impl_->next_frame_;
    impl_->next_frame_.clear();
    impl_->ready_frame_types_ = 0;

    return true;
  }
  else
  {
    return false;
  }
#else
  (void)milliseconds;
  waitForNewFrame(frame);
  return true;
#endif // LIBFREENECT2_THREADING_STDLIB
}

void SyncMultiFrameListener::waitForNewFrame(FrameMap &frame)
{
  libfreenect2::unique_lock l(impl_->mutex_);

  while(!impl_->hasNewFrame())
  {
    WAIT_CONDITION(impl_->condition_, impl_->mutex_, l)
  }

  frame = impl_->next_frame_;
  impl_->next_frame_.clear();
  impl_->ready_frame_types_ = 0;
  impl_->current_frame_released_ = false;
}

void SyncMultiFrameListener::release(FrameMap &frame)
{
  for(FrameMap::iterator it = frame.begin(); it != frame.end(); ++it)
  {
    delete it->second;
    it->second = 0;
  }

  frame.clear();

  {
    libfreenect2::lock_guard l(impl_->mutex_);
    impl_->current_frame_released_ = true;
  }
}

bool SyncMultiFrameListener::onNewFrame(Frame::Type type, Frame *frame)
{
  if((impl_->subscribed_frame_types_ & type) == 0) return false;

  {
    libfreenect2::lock_guard l(impl_->mutex_);

    if (!impl_->current_frame_released_)
      return false;

    FrameMap::iterator it = impl_->next_frame_.find(type);

    if(it != impl_->next_frame_.end())
    {
      // replace frame
      delete it->second;
      it->second = frame;
    }
    else
    {
      impl_->next_frame_[type] = frame;
    }

    impl_->ready_frame_types_ |= type;
  }

  impl_->condition_.notify_one();

  return true;
}

TimestampAlignedFrameListener::Statistics::Statistics()
    : delivered(0), dropped(0), last_delta_ticks(0), maximum_delta_ticks(0)
{
}

/** Implementation class for timestamp-aligning bounded frame queues. */
class TimestampAlignedFrameListenerImpl
{
public:
  typedef std::deque<Frame*> FrameQueue;
  typedef std::map<Frame::Type, FrameQueue> FrameQueues;

  libfreenect2::mutex mutex_;
  libfreenect2::condition_variable condition_;
  FrameQueues queues_;
  std::vector<Frame::Type> subscribed_types_;
  FrameMap ready_frame_;
  TimestampAlignedFrameListener::Statistics statistics_;
  const unsigned int subscribed_frame_types_;
  const uint32_t max_delta_ticks_;
  const size_t queue_capacity_;
  uint32_t ready_delta_ticks_;

  TimestampAlignedFrameListenerImpl(unsigned int frame_types, uint32_t max_delta_ticks,
                                    size_t queue_capacity)
      : subscribed_frame_types_(frame_types), max_delta_ticks_(max_delta_ticks),
        queue_capacity_(std::max<size_t>(queue_capacity, 1)), ready_delta_ticks_(0)
  {
    const Frame::Type supported_types[] = {Frame::Color, Frame::Ir, Frame::Depth};
    for (size_t i = 0; i < sizeof(supported_types) / sizeof(supported_types[0]); ++i)
    {
      if ((frame_types & supported_types[i]) != 0)
      {
        subscribed_types_.push_back(supported_types[i]);
        queues_[supported_types[i]] = FrameQueue();
      }
    }
  }

  bool hasNewFrame() const { return !ready_frame_.empty(); }

  void searchCombinations(size_t type_index, std::vector<size_t>& candidate,
                          std::vector<size_t>& best, uint32_t& best_span) const
  {
    if (type_index == subscribed_types_.size())
    {
      std::vector<uint32_t> timestamps(subscribed_types_.size());
      for (size_t i = 0; i < subscribed_types_.size(); ++i)
      {
        const FrameQueue& queue = queues_.find(subscribed_types_[i])->second;
        timestamps[i] = queue[candidate[i]]->timestamp;
      }

      const uint32_t span = deviceTimestampSpan(timestamps.data(), timestamps.size());
      if (best.empty() || span < best_span)
      {
        best = candidate;
        best_span = span;
      }
      return;
    }

    const FrameQueue& queue = queues_.find(subscribed_types_[type_index])->second;
    for (size_t i = 0; i < queue.size(); ++i)
    {
      candidate[type_index] = i;
      searchCombinations(type_index + 1, candidate, best, best_span);
    }
  }

  bool prepareNextFrame()
  {
    if (hasNewFrame() || subscribed_types_.empty())
      return false;

    for (size_t i = 0; i < subscribed_types_.size(); ++i)
    {
      if (queues_[subscribed_types_[i]].empty())
        return false;
    }

    std::vector<size_t> candidate(subscribed_types_.size(), 0);
    std::vector<size_t> best;
    uint32_t best_span = std::numeric_limits<uint32_t>::max();
    searchCombinations(0, candidate, best, best_span);
    if (best.empty() || best_span > max_delta_ticks_)
      return false;

    for (size_t i = 0; i < subscribed_types_.size(); ++i)
    {
      const Frame::Type type = subscribed_types_[i];
      FrameQueue& queue = queues_[type];
      for (size_t skipped = 0; skipped < best[i]; ++skipped)
      {
        delete queue.front();
        queue.pop_front();
        ++statistics_.dropped;
      }
      ready_frame_[type] = queue.front();
      queue.pop_front();
    }

    ready_delta_ticks_ = best_span;
    return true;
  }

  bool takeReadyFrame(FrameMap& frame)
  {
    if (!hasNewFrame())
      return false;

    frame = ready_frame_;
    ready_frame_.clear();
    ++statistics_.delivered;
    statistics_.last_delta_ticks = ready_delta_ticks_;
    statistics_.maximum_delta_ticks = std::max(statistics_.maximum_delta_ticks, ready_delta_ticks_);
    return true;
  }
};

TimestampAlignedFrameListener::TimestampAlignedFrameListener(unsigned int frame_types,
                                                             uint32_t max_delta_ticks,
                                                             size_t queue_capacity)
    : impl_(new TimestampAlignedFrameListenerImpl(frame_types, max_delta_ticks, queue_capacity))
{
}

TimestampAlignedFrameListener::~TimestampAlignedFrameListener()
{
  release(impl_->ready_frame_);
  for (TimestampAlignedFrameListenerImpl::FrameQueues::iterator queue = impl_->queues_.begin();
       queue != impl_->queues_.end(); ++queue)
  {
    while (!queue->second.empty())
    {
      delete queue->second.front();
      queue->second.pop_front();
    }
  }
  delete impl_;
}

bool TimestampAlignedFrameListener::hasNewFrame() const
{
  libfreenect2::unique_lock l(impl_->mutex_);
  return impl_->hasNewFrame();
}

bool TimestampAlignedFrameListener::waitForNewFrame(FrameMap& frame, int milliseconds)
{
  libfreenect2::unique_lock l(impl_->mutex_);
  const bool received = impl_->condition_.wait_for(l, std::chrono::milliseconds(milliseconds),
                                                   [this] { return impl_->hasNewFrame(); });
  if (!received)
    return false;

  impl_->takeReadyFrame(frame);
  const bool another_frame_ready = impl_->prepareNextFrame();
  l.unlock();
  if (another_frame_ready)
    impl_->condition_.notify_one();
  return true;
}

void TimestampAlignedFrameListener::waitForNewFrame(FrameMap& frame)
{
  libfreenect2::unique_lock l(impl_->mutex_);
  impl_->condition_.wait(l, [this] { return impl_->hasNewFrame(); });
  impl_->takeReadyFrame(frame);
  const bool another_frame_ready = impl_->prepareNextFrame();
  l.unlock();
  if (another_frame_ready)
    impl_->condition_.notify_one();
}

void TimestampAlignedFrameListener::release(FrameMap& frame)
{
  for (FrameMap::iterator it = frame.begin(); it != frame.end(); ++it)
  {
    delete it->second;
    it->second = 0;
  }
  frame.clear();
}

TimestampAlignedFrameListener::Statistics TimestampAlignedFrameListener::getStatistics() const
{
  libfreenect2::unique_lock l(impl_->mutex_);
  return impl_->statistics_;
}

bool TimestampAlignedFrameListener::onNewFrame(Frame::Type type, Frame* frame)
{
  if ((type != Frame::Color && type != Frame::Ir && type != Frame::Depth) ||
      (impl_->subscribed_frame_types_ & type) == 0)
    return false;

  bool frame_ready = false;
  {
    libfreenect2::lock_guard l(impl_->mutex_);
    TimestampAlignedFrameListenerImpl::FrameQueue& queue = impl_->queues_[type];
    queue.push_back(frame);
    while (queue.size() > impl_->queue_capacity_)
    {
      delete queue.front();
      queue.pop_front();
      ++impl_->statistics_.dropped;
    }
    frame_ready = impl_->prepareNextFrame();
  }

  if (frame_ready)
    impl_->condition_.notify_one();
  return true;
}

} /* namespace libfreenect2 */
