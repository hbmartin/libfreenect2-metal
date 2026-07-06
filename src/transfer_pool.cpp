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

/** @file transfer_pool.cpp Data transfer implementation. */

#include <libfreenect2/usb/transfer_pool.h>
#include <libfreenect2/logging.h>

#define WRITE_LIBUSB_ERROR(__RESULT) libusb_error_name(__RESULT) << " " << libusb_strerror((libusb_error)__RESULT)

namespace libfreenect2
{
namespace usb
{

TransferPool::TransferPool(libusb_device_handle* device_handle, unsigned char device_endpoint) :
    callback_(0),
    device_handle_(device_handle),
    device_endpoint_(device_endpoint),
    buffer_(0),
    buffer_size_(0),
    enable_submit_(false),
    stalled_transfers_(0),
    stall_logged_(false),
    disconnect_logged_(false)
{
}

TransferPool::~TransferPool()
{
  deallocate();
}

void TransferPool::enableSubmission()
{
  enable_submit_ = true;
}

void TransferPool::disableSubmission()
{
  enable_submit_ = false;
}

bool TransferPool::enabled()
{
  return enable_submit_;
}

void TransferPool::deallocate()
{
  for(TransferQueue::iterator it = transfers_.begin(); it != transfers_.end(); ++it)
  {
    libusb_free_transfer(it->transfer);
  }
  transfers_.clear();

  if(buffer_ != 0)
  {
    delete[] buffer_;
    buffer_ = 0;
    buffer_size_ = 0;
  }
}

bool TransferPool::submit()
{
  if(!enable_submit_)
  {
    LOG_WARNING << "transfer submission disabled!";
    return false;
  }

  stalled_transfers_ = 0;
  stall_logged_ = false;
  disconnect_logged_ = false;

  size_t failcount = 0;
  for(size_t i = 0; i < transfers_.size(); ++i)
  {
    libusb_transfer *transfer = transfers_[i].transfer;
    transfers_[i].setStopped(false);

    int r = libusb_submit_transfer(transfer);

    if(r != LIBUSB_SUCCESS)
    {
      LOG_ERROR << "failed to submit transfer: " << WRITE_LIBUSB_ERROR(r);
      transfers_[i].setStopped(true);
      // count it as stalled so logStallIfComplete() still fires if the
      // remaining transfers die later
      stalled_transfers_++;
      failcount++;
    }
  }

  if (failcount == transfers_.size())
  {
    LOG_ERROR << "all submissions failed. Try debugging with environment variable: LIBUSB_DEBUG=3.";
    return false;
  }

  return true;
}

void TransferPool::cancel()
{
  for(TransferQueue::iterator it = transfers_.begin(); it != transfers_.end(); ++it)
  {
    int r = libusb_cancel_transfer(it->transfer);

    if(r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_NOT_FOUND)
    {
      LOG_ERROR << "failed to cancel transfer: " << WRITE_LIBUSB_ERROR(r);
    }
  }

  for(;;)
  {
    libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(100));
    size_t stopped_transfers = 0;
    for(TransferQueue::iterator it = transfers_.begin(); it != transfers_.end(); ++it)
      stopped_transfers += it->getStopped();
    if (stopped_transfers == transfers_.size())
      break;
    LOG_INFO << "waiting for transfer cancellation";
    libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(1000));
  }
}

void TransferPool::setCallback(DataCallback *callback)
{
  callback_ = callback;
}

void TransferPool::allocateTransfers(size_t num_transfers, size_t transfer_size)
{
  buffer_size_ = num_transfers * transfer_size;
  buffer_ = new unsigned char[buffer_size_];
  transfers_.reserve(num_transfers);

  unsigned char *ptr = buffer_;

  for(size_t i = 0; i < num_transfers; ++i)
  {
    libusb_transfer *transfer = allocateTransfer();
    fillTransfer(transfer);

    transfers_.push_back(TransferPool::Transfer(transfer, this));

    transfer->dev_handle = device_handle_;
    transfer->endpoint = device_endpoint_;
    transfer->buffer = ptr;
    transfer->length = transfer_size;
    transfer->timeout = 1000;
    transfer->callback = (libusb_transfer_cb_fn) &TransferPool::onTransferCompleteStatic;
    transfer->user_data = &transfers_.back();

    ptr += transfer_size;
  }
}

void TransferPool::onTransferCompleteStatic(libusb_transfer* transfer)
{
  TransferPool::Transfer *t = reinterpret_cast<TransferPool::Transfer*>(transfer->user_data);
  t->pool->onTransferComplete(t);
}

void TransferPool::onTransferComplete(TransferPool::Transfer* t)
{
  if(t->transfer->status == LIBUSB_TRANSFER_CANCELLED)
  {
    t->setStopped(true);
    return;
  }

  if(t->transfer->status == LIBUSB_TRANSFER_NO_DEVICE)
  {
    // The device is gone; resubmitting can never succeed. Do not process the
    // transfer: libusb (and the macOS backend) does not rewrite
    // iso_packet_desc[] when a transfer completes with NO_DEVICE, so the
    // descriptors still carry the previous round's COMPLETED statuses and the
    // buffer its already-delivered bytes — processing here would re-emit stale
    // duplicate packets to the stream parser at disconnect.
    if(!disconnect_logged_)
    {
      disconnect_logged_ = true;
      LOG_ERROR << "usb transfer failed: device disconnected";
    }
    t->setStopped(true);
    stalled_transfers_++;
    logStallIfComplete();
    return;
  }

  if(t->transfer->status != LIBUSB_TRANSFER_COMPLETED)
  {
    // transient errors (timeout, io, overflow) happen under bus pressure;
    // the transfer is resubmitted below, so only note it at debug level
    LOG_DEBUG << "usb transfer status " << t->transfer->status << ", resubmitting";
  }

  // process data
  processTransfer(t->transfer);

  if(!enable_submit_)
  {
    t->setStopped(true);
    return;
  }

  // resubmit self; an immediate retry cannot succeed where this attempt
  // failed (the conditions that clear a transient submit error require
  // event-loop progress, which is blocked while this callback runs)
  int r = libusb_submit_transfer(t->transfer);

  if(r != LIBUSB_SUCCESS)
  {
    LOG_ERROR << "failed to submit transfer: " << WRITE_LIBUSB_ERROR(r);
    t->setStopped(true);
    stalled_transfers_++;
    logStallIfComplete();
  }
}

void TransferPool::logStallIfComplete()
{
  if(!stall_logged_ && enable_submit_ && stalled_transfers_ >= transfers_.size())
  {
    stall_logged_ = true;
    LOG_ERROR << "all usb transfers on endpoint 0x" << std::hex
              << int(device_endpoint_) << std::dec
              << " have stopped; the stream is stalled. Stop and close the "
                 "device, then reopen it (replug or reset the Kinect if "
                 "reopening fails). Common causes: USB3 bandwidth "
                 "or power limits, flaky cables/adapters, or VM USB "
                 "passthrough.";
  }
}

BulkTransferPool::BulkTransferPool(libusb_device_handle* device_handle, unsigned char device_endpoint) :
    TransferPool(device_handle, device_endpoint)
{
}

BulkTransferPool::~BulkTransferPool()
{
}

void BulkTransferPool::allocate(size_t num_transfers, size_t transfer_size)
{
  allocateTransfers(num_transfers, transfer_size);
}

libusb_transfer* BulkTransferPool::allocateTransfer()
{
  return libusb_alloc_transfer(0);
}

void BulkTransferPool::fillTransfer(libusb_transfer* transfer)
{
  transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
}

void BulkTransferPool::processTransfer(libusb_transfer* transfer)
{
  if(transfer->status != LIBUSB_TRANSFER_COMPLETED) return;

  if(callback_)
    callback_->onDataReceived(transfer->buffer, transfer->actual_length);
}

IsoTransferPool::IsoTransferPool(libusb_device_handle* device_handle, unsigned char device_endpoint) :
    TransferPool(device_handle, device_endpoint),
    num_packets_(0),
    packet_size_(0)
{
}

IsoTransferPool::~IsoTransferPool()
{
}

void IsoTransferPool::allocate(size_t num_transfers, size_t num_packets, size_t packet_size)
{
  num_packets_ = num_packets;
  packet_size_ = packet_size;

  allocateTransfers(num_transfers, num_packets_ * packet_size_);
}

libusb_transfer* IsoTransferPool::allocateTransfer()
{
  return libusb_alloc_transfer(num_packets_);
}

void IsoTransferPool::fillTransfer(libusb_transfer* transfer)
{
  transfer->type = LIBUSB_TRANSFER_TYPE_ISOCHRONOUS;
  transfer->num_iso_packets = num_packets_;

  libusb_set_iso_packet_lengths(transfer, packet_size_);
}

void IsoTransferPool::processTransfer(libusb_transfer* transfer)
{
  unsigned char *ptr = transfer->buffer;

  for(size_t i = 0; i < num_packets_; ++i)
  {
    if(transfer->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED) continue;

    if(callback_)
      callback_->onDataReceived(ptr, transfer->iso_packet_desc[i].actual_length);

    ptr += transfer->iso_packet_desc[i].length;
  }
}

} /* namespace usb */
} /* namespace libfreenect2 */

