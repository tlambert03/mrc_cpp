#include <gtest/gtest.h>

#include <stdexcept>

#include "dvfile.h"

TEST(DVFileTest, ReadHeader) {
  const int istream_no = 1;
  const char* input_filename = "example.dv";
  IW_MRC_HEADER header;

  ASSERT_EQ(IMOpen(istream_no, input_filename, "ro"), 0) << "Failed to open input file.";

  int nxyz[3];
  int mxyz[3];
  int pixeltype;
  float min, max, mean;

  IMRdHdr(istream_no, nxyz, mxyz, &pixeltype, &min, &max, &mean);

  EXPECT_EQ(nxyz[0], 32);
  EXPECT_EQ(nxyz[1], 32);
  EXPECT_EQ(nxyz[2], 18);
  EXPECT_EQ(mxyz[0], 1);
  EXPECT_EQ(mxyz[1], 1);
  EXPECT_EQ(mxyz[2], 1);
  EXPECT_EQ(pixeltype, 6);
  EXPECT_FLOAT_EQ(min, 215);
  EXPECT_FLOAT_EQ(max, 1743);
  EXPECT_FLOAT_EQ(mean, 775.83331f);

  IMGetHdr(istream_no, &header);

  // Replace with the actual expected values
  EXPECT_EQ(header.nx, 32);
  EXPECT_EQ(header.ny, 32);
  EXPECT_EQ(header.nz, 18);
  EXPECT_EQ(header.num_planes(), 3);
  EXPECT_EQ(header.num_waves, 3);
  EXPECT_EQ(header.num_times, 2);

  IMClose(istream_no);
}

TEST(DVFileTest, ReadSection) {
  const int istream_no = 1;
  const char* input_filename = "example.dv";
  IW_MRC_HEADER hdr;

  ASSERT_EQ(IMOpen(istream_no, input_filename, "ro"), 0) << "Failed to open input file.";
  IMGetHdr(istream_no, &hdr);

  std::vector<uint16_t> buffer(hdr.nx * hdr.ny, 0);
  IMPosnZWT(istream_no, 0, 0, 0);
  IMRdSec(istream_no, buffer.data());
  EXPECT_EQ(buffer[0], 326);
  EXPECT_EQ(buffer[1], 326);
  EXPECT_EQ(buffer[2], 284);

  IMRdSec(istream_no, buffer.data());
  EXPECT_EQ(buffer[0], 522);
  EXPECT_EQ(buffer[1], 522);
  EXPECT_EQ(buffer[2], 516);

  IMRdSec(istream_no, buffer.data());
  EXPECT_EQ(buffer[0], 4066);
  EXPECT_EQ(buffer[1], 4066);
  EXPECT_EQ(buffer[2], 4311);

  IMClose(istream_no);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
