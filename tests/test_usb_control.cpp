/** @file test_usb_control.cpp Exercises interface claims without a USB device. */

#include <array>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/protocol/usb_control.h>

namespace
{

using libfreenect2::protocol::UsbControl;

struct InterfaceState
{
  std::vector<int> ownership = {0};
  size_t queries = 0;
  int claim_result = LIBUSB_SUCCESS;
};

std::array<InterfaceState, 2> interfaces;
std::vector<int> claims;

class UsbInterfaceClaim : public testing::Test
{
protected:
  void SetUp() override
  {
    interfaces = {};
    claims.clear();
  }

  UsbControl control{nullptr};
};

} // namespace

// These declarations come from libusb.h through target-local compile definitions.
extern "C" int LIBUSB_CALL test_libusb_kernel_driver_active(libusb_device_handle* handle,
                                                            int interface_id)
{
  EXPECT_EQ(handle, nullptr);
  if (interface_id < 0 || interface_id > 1)
  {
    ADD_FAILURE() << "unexpected interface " << interface_id;
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  InterfaceState& state = interfaces[interface_id];
  const size_t index = state.queries++;
  return index < state.ownership.size() ? state.ownership[index] : state.ownership.back();
}

extern "C" int LIBUSB_CALL test_libusb_claim_interface(libusb_device_handle* handle,
                                                       int interface_id)
{
  EXPECT_EQ(handle, nullptr);
  if (interface_id < 0 || interface_id > 1)
  {
    ADD_FAILURE() << "unexpected interface " << interface_id;
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  claims.push_back(interface_id);
  return interfaces[interface_id].claim_result;
}

TEST_F(UsbInterfaceClaim, ClaimsBothUnownedInterfacesInOrder)
{
  EXPECT_EQ(control.claimInterfaces(), UsbControl::Success);
  EXPECT_EQ(claims, (std::vector<int>{0, 1}));
  EXPECT_EQ(interfaces[0].queries, 1u);
  EXPECT_EQ(interfaces[1].queries, 1u);
}

TEST_F(UsbInterfaceClaim, RefusesPersistentControlOwnershipWithoutClaiming)
{
  interfaces[0].ownership = {1};

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_TRUE(claims.empty());
  EXPECT_EQ(interfaces[1].queries, 0u);
#ifdef __APPLE__
  EXPECT_EQ(interfaces[0].queries, 11u);
#else
  EXPECT_EQ(interfaces[0].queries, 1u);
#endif
}

TEST_F(UsbInterfaceClaim, RefusesPersistentIrOwnership)
{
  interfaces[1].ownership = {1};

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_EQ(claims, (std::vector<int>{0}));
#ifdef __APPLE__
  EXPECT_EQ(interfaces[1].queries, 11u);
#else
  EXPECT_EQ(interfaces[1].queries, 1u);
#endif
}

TEST_F(UsbInterfaceClaim, ClaimsWhenOwnershipQueryIsUnsupported)
{
  interfaces[0].ownership = {LIBUSB_ERROR_NOT_SUPPORTED};
  interfaces[1].ownership = {LIBUSB_ERROR_NOT_SUPPORTED};

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Success);
  EXPECT_EQ(claims, (std::vector<int>{0, 1}));
}

TEST_F(UsbInterfaceClaim, ReportsDisappearanceAfterOwnershipQueryFails)
{
  interfaces[0].ownership = {LIBUSB_ERROR_NOT_FOUND};
  interfaces[0].claim_result = LIBUSB_ERROR_NO_DEVICE;

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_EQ(claims, (std::vector<int>{0}));
  EXPECT_EQ(interfaces[0].queries, 1u);
  EXPECT_EQ(interfaces[1].queries, 0u);
}

class UsbControlClaimError : public UsbInterfaceClaim, public testing::WithParamInterface<int>
{
};

TEST_P(UsbControlClaimError, PreservesClaimFailure)
{
  interfaces[0].claim_result = GetParam();

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_EQ(claims, (std::vector<int>{0}));
  EXPECT_EQ(interfaces[1].queries, 0u);
}

INSTANTIATE_TEST_SUITE_P(ClaimErrors, UsbControlClaimError,
                         testing::Values(LIBUSB_ERROR_BUSY, LIBUSB_ERROR_ACCESS,
                                         LIBUSB_ERROR_NO_DEVICE));

TEST_F(UsbInterfaceClaim, PreservesIrClaimFailure)
{
  interfaces[1].claim_result = LIBUSB_ERROR_BUSY;

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_EQ(claims, (std::vector<int>{0, 1}));
}

#ifdef __APPLE__
TEST_F(UsbInterfaceClaim, WaitsForTransientOwnershipOnBothInterfaces)
{
  interfaces[0].ownership = {1, 1, 0};
  interfaces[1].ownership = {1, 1, 0};

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Success);
  EXPECT_EQ(claims, (std::vector<int>{0, 1}));
  EXPECT_EQ(interfaces[0].queries, 3u);
  EXPECT_EQ(interfaces[1].queries, 3u);
}

TEST_F(UsbInterfaceClaim, CanClaimOnTheFinalReadinessRetry)
{
  interfaces[0].ownership = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Success);
  EXPECT_EQ(claims, (std::vector<int>{0, 1}));
  EXPECT_EQ(interfaces[0].queries, 11u);
}

TEST_F(UsbInterfaceClaim, DoesNotHideAClaimFailureAfterReadiness)
{
  interfaces[0].ownership = {1, 0};
  interfaces[0].claim_result = LIBUSB_ERROR_BUSY;

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_EQ(claims, (std::vector<int>{0}));
  EXPECT_EQ(interfaces[0].queries, 2u);
  EXPECT_EQ(interfaces[1].queries, 0u);
}
#else
TEST_F(UsbInterfaceClaim, RefusesOwnershipWithoutMacOsRetry)
{
  interfaces[0].ownership = {1, 0};

  EXPECT_EQ(control.claimInterfaces(), UsbControl::Error);
  EXPECT_TRUE(claims.empty());
  EXPECT_EQ(interfaces[0].queries, 1u);
}
#endif
