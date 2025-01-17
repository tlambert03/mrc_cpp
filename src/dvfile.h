#include <fstream>
#include <iostream>
#include <map>

enum class PixelType {
  UINT8 = 0,
  INT16 = 1,
  FLOAT32 = 2,
  COMPLEX_INT16 = 3,
  COMPLEX64 = 4,
  INT16_ALT = 5,
  UINT16 = 6,
  INT32 = 7
};

std::unordered_map<PixelType, size_t> pixelTypeSizes = {
    {PixelType::UINT8, sizeof(uint8_t)},       {PixelType::INT16, sizeof(int16_t)},
    {PixelType::FLOAT32, sizeof(float)},       {PixelType::COMPLEX_INT16, 2 * sizeof(int16_t)},
    {PixelType::COMPLEX64, 2 * sizeof(float)}, {PixelType::INT16_ALT, sizeof(int16_t)},
    {PixelType::UINT16, sizeof(uint16_t)},     {PixelType::INT32, sizeof(int32_t)}};

size_t getPixelTypeSize(PixelType pixelType) { return pixelTypeSizes[pixelType]; }

typedef struct IW_MRC_Header {
  int32_t nx, ny, nz;        // nz : nplanes*nwave*ntime
  int32_t mode;              // data type
  int32_t nxst, nyst, nzst;  // index of the first col/row/section
  int32_t mx, my, mz;        // number of intervals in x/y/z
  float xlen, ylen, zlen;    // pixel spacing for x/y/z
  float alpha, beta, gamma;  // cell angles
  int32_t mapc, mapr, maps;  // column/row/section axis
  float amin, amax, amean;   // min/max/mean intensity
  int32_t ispg, inbsym;      // space group number, number of bytes in extended header
  int16_t nDVID, nblank;     // ID value, unused
  int32_t ntst;              // starting time index (used for time series data)
  char ibyte[24];            // 24 bytes of blank space
  int16_t nint, nreal;       // number of integers/floats in extended header per section
  int16_t nres, nzfact;      // number of sub-resolution data sets, reduction quotient for z axis
  float min2, max2, min3, max3, min4, max4;  // min/max intensity for 2nd, 3rd, 4th wavelengths
  int16_t file_type, lens, n1, n2, v1, v2;   // file type, lens ID, n1, n2, v1, v2
  float min5, max5;                          // min/max intensity for 5th wavelength
  int16_t num_times;                         // number of time points
  int16_t interleaved;                       // (0 = ZTW, 1 = WZT, 2 = ZWT)
  float tilt_x, tilt_y, tilt_z;              // x/y/z axis tilt angles
  int16_t num_waves, iwav1, iwav2, iwav3, iwav4, iwav5;  // number & values of wavelengths
  float zorig, xorig, yorig;                             // z/x/y origin
  int32_t nlab;                                          // number of titles
  char label[800];

  std::string sequence_order() const {
    switch (interleaved) {
      case 0: return "CTZ";
      case 1: return "TZC";
      case 2: return "TCZ";
      default: return "CTZ";
    }
  }

  int num_planes() const { return nz / (num_waves ? num_waves : 1) / (num_times ? num_times : 1); }

  std::string image_type() const {
    switch (file_type) {
      case 0:
      case 100: return "NORMAL";
      case 1: return "TILT_SERIES";
      case 2: return "STEREO_TILT_SERIES";
      case 3: return "AVERAGED_IMAGES";
      case 4: return "AVERAGED_STEREO_PAIRS";
      case 5: return "EM_TILT_SERIES";
      case 20: return "MULTIPOSITION";
      case 8000: return "PUPIL_FUNCTION";
      default: return "UNKNOWN";
    }
  }

  void print() {
    std::cout << "Header:" << std::endl;
    std::cout << "  Dimensions: " << ny << "x" << nx << "x" << num_planes() << std::endl;
    std::cout << "  Number of wavelengths: " << num_waves << std::endl;
    std::cout << "  Number of time points: " << num_times << std::endl;
    std::cout << "  Pixel size: " << mode << std::endl;
    std::cout << "  bytes per pixel: " << getPixelTypeSize(static_cast<PixelType>(mode))
              << " bytes" << std::endl;
    std::cout << "  Pixel spacing: " << xlen << "x" << ylen << "x" << zlen << std::endl;
    std::cout << "  mxyz: " << mx << "x" << my << "x" << mz << std::endl;
    std::cout << "  Cell angles: " << alpha << "x" << beta << "x" << gamma << std::endl;
    std::cout << "  Min/Max/Mean: " << amin << "/" << amax << "/" << amean << std::endl;
    std::cout << "  Image type: " << image_type() << std::endl;
    std::cout << "  Sequence order: " << sequence_order() << std::endl;
  }

} IW_MRC_HEADER, *IW_MRC_HEADER_PTR;

class DVFile {
 private:
  std::unique_ptr<std::ifstream> _file;
  std::string _path;
  bool _big_endian;
  IW_MRC_Header hdr;
  bool closed = true;

  void _validateZWT(int z, int w, int t) {
    if (t >= hdr.num_times) {
      throw std::runtime_error("Time index out of range");
    }
    if (w >= hdr.num_waves) {
      throw std::runtime_error("Wavelength index out of range");
    }
    if (z >= hdr.num_planes()) {
      throw std::runtime_error("Section index out of range");
    }
  }

 public:
  DVFile(const std::string& path) {
    _path = path;
    _file = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!_file->is_open()) {
      throw std::runtime_error("Failed to open file");
    }

    // Determine byte order
    _file->seekg(24 * 4);
    char dvid[2];
    _file->read(dvid, 2);
    if (dvid[0] == (char)0xA0 && dvid[1] == (char)0xC0) {
      _big_endian = false;
    } else if (dvid[0] == (char)0xC0 && dvid[1] == (char)0xA0) {
      _big_endian = true;
    } else {
      throw std::runtime_error(path + " is not a recognized DV file.");
    }

    // Read header
    _file->seekg(0);
    _file->read(reinterpret_cast<char*>(&hdr), sizeof(IW_MRC_Header));
    closed = false;
  }

  // this is only here for the IVE API
  void setCurrentZWT(int z, int w, int t) {
    _validateZWT(z, w, t);

    size_t frame_size = hdr.ny * hdr.nx * getPixelSize();
    int header_size = 1024 + hdr.inbsym;
    int section_offset = (t * hdr.num_waves * hdr.num_planes() + w * hdr.num_planes() + z);
    _file->seekg(header_size + section_offset * frame_size);
  }

  void readSec(void* array) {
    if (closed) {
      throw std::runtime_error("Cannot read from closed file. Please reopen with .open()");
    }
    size_t frame_size = hdr.ny * hdr.nx * getPixelSize();
    _file->read(reinterpret_cast<char*>(array), frame_size);
  }

  void readSec(void* array, int t, int w, int z) {
    setCurrentZWT(z, w, t);
    readSec(array);
  }

  size_t getPixelSize() { return getPixelTypeSize(static_cast<PixelType>(hdr.mode)); }

  void open() {
    if (closed) {
      _file->open(_path, std::ios::binary);
      if (!_file->is_open()) {
        throw std::runtime_error("Failed to open file");
      }
      closed = false;
    }
  }

  void close() {
    if (!closed) {
      _file->close();
      closed = true;
    }
  }

  ~DVFile() { close(); }

  std::string getPath() const { return _path; }

  IW_MRC_Header getHeader() const { return hdr; }

  bool isClosed() const { return closed; }

  std::map<std::string, int> sizes() {
    int num_real_z =
        hdr.nz / (hdr.num_waves ? hdr.num_waves : 1) / (hdr.num_times ? hdr.num_times : 1);
    std::map<std::string, int> d = {{"T", hdr.num_times},
                                    {"C", hdr.num_waves},
                                    {"Z", num_real_z},
                                    {"Y", hdr.ny},
                                    {"X", hdr.nx}};
    std::string axes = hdr.sequence_order() + "YX";
    std::map<std::string, int> sizes;
    for (char c : axes) {
      std::string key(1, c);  // Convert char to std::string
      sizes[key] = d[key];
    }
    return sizes;
  }
};

//////////////////////////////////////////////////////////////////////////////
// IVE API
//////////////////////////////////////////////////////////////////////////////

// stream -> DVFile
std::map<int, std::unique_ptr<DVFile>> dvfile_map;

DVFile& getDVFile(int istream) {
  auto it = dvfile_map.find(istream);
  if (it == dvfile_map.end()) {
    throw std::runtime_error("Stream not found: " + std::to_string(istream));
  }
  return *(it->second);
}

// attrib is one of "ro" or "new"
int IMOpen(int istream, const char* name, const char* attrib) {
  // Check if the stream identifier is already in use and close it if necessary
  if (dvfile_map.find(istream) != dvfile_map.end()) {
    dvfile_map[istream]->close();
    dvfile_map.erase(istream);
    std::cerr << "Warning: Reusing stream identifier " << istream << ". Previous stream closed."
              << std::endl;
  }

  if (std::string(attrib) == "ro") {
    try {
      dvfile_map[istream] = std::make_unique<DVFile>(name);
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return -1;  // Return non-zero to indicate failure
    }
  } else {
    std::cerr << "Unknown file mode: " << attrib << std::endl;
    return -1;  // Return non-zero to indicate failure
  }
  return 0;  // Return 0 to indicate success
}

void IMClose(int istream) {
  // call destructor of DVFile
  dvfile_map.erase(istream);
}

void IMGetHdr(int istream, IW_MRC_HEADER* header) { *header = getDVFile(istream).getHeader(); }

void IMRdHdr(int istream, int ixyz[3], int mxyz[3], int* imode, float* min, float* max,
             float* mean) {
  IW_MRC_HEADER header;
  IMGetHdr(istream, &header);
  ixyz[0] = header.nx;
  ixyz[1] = header.ny;
  ixyz[2] = header.nz;
  mxyz[0] = header.mx;
  mxyz[1] = header.my;
  mxyz[2] = header.mz;
  *imode = header.mode;
  *min = header.amin;
  *max = header.amax;
  *mean = header.amean;
}

/**
 * @brief Set the image conversion mode during read/write operations from image storage.
 *
 * By default in IVE, images that are read from image storage are converted to
 * 4-byte floating-point data. Similarly, when images are written to image
 * storage they are converted to the data type indicated by the image data type
 * associated with the corresponding stream (see IMAlMode). The default in IVE
 * is ConversionFlag=TRUE.
 * We, however, don't ever convert the data type of the image data. So for now,
 * this is a no-op.
 *
 * @param istream The input stream to be used for the operation.
 * @param flag The flag indicating the type of operation to be performed.
 */
void IMAlCon(int istream, int flag) {
  // if flag is 1, warn:
  if (flag == 1) {
    std::cerr << "Warning: IMAlCon is not implemented. ConversionFlag=TRUE is not supported."
              << std::endl;
  }
}

/**
 * @brief Change the image titles.
 *
 * @param istream The input stream to be used for the operation.
 * @param titles The titles to be changed.  Contains at least NumTitles title strings, each of
 * which must contain exactly 80 characters.
 * @param num_titles The number of titles to be changed.
 */
void IMAlLab(int istream, const char* labels, int nl) {
  std::cerr << "Warning: IMAlLab is not implemented." << std::endl;
}

/**
 * @brief  Enable or disable printing to standard output ("stdout").
 *
 * Certain IM functions will print information to stdout if Format=TRUE, which is the default. To
 * disable printing, set flag=FALSE.
 *
 * @param flag The flag indicating the type of operation to be performed.
 */
void IMAlPrt(int flag) {
  if (flag == 1) {
    std::cerr << "Warning: IMAlPrt is not implemented." << std::endl;
  }
}

/**
 * @brief Position the read/write point at a particular Z, W, T section.
 *
 * If the stream points to a scratch window, IMPosnZWT can only change the destination wavelength.
 *
 * @param istream The input stream to be used for the operation.
 * @param iz The Z section number.
 * @param iw The wavelength number.
 * @param it The time-point number.
 * @return int 0 if successful and 1 if not.
 */
int IMPosnZWT(int istream, int iz, int iw, int it) {
  DVFile& dvfile = getDVFile(istream);

  try {
    dvfile.setCurrentZWT(iz, iw, it);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

/**
 * @brief Read the next section.
 *
 * Reads the next section into ImgBuffer and advances the file pointer to the
 * section after that. The results are undefined if ImgBuffer does not have at
 * least nx * ny elements or the file pointer does not point to the beginning of
 * a section.
 * In most cases, ImgBuffer will contain floating-point data. When
 * image conversion is off, however, the data type of ImgBuffer should
 * correspond to whatever data type is actually stored. For example, if the
 * image data are stored as 16-bit integers, then ImgBuffer should point to a
 * 16-bit buffer. See IMAlCon.
 *
 * @param istream The input stream to be used for the operation.
 * @param ImgBuffer The image buffer to store the data.
 */
void IMRdSec(int istream, void* ImgBuffer) {
  try {
    getDVFile(istream).readSec(ImgBuffer);
  } catch (const std::runtime_error& e) {
    std::cerr << "Error reading section: " << e.what() << std::endl;
    // Handle the error appropriately, e.g., by rethrowing or returning an error code
    throw;  // Rethrow the exception to propagate it further
  }
}

void IMPutHdr(int istream, const IW_MRC_HEADER* header) {
  std::cerr << "Warning: IMPutHdr is not implemented." << std::endl;
}
// not yet implemented ///////////////////////////////////

/**
 * @brief Return extended header values for a particular Z section, wavelength,
 * and time-point.
 *
 * The integer and floating-point values for the requested Z section (ZSecNum),
 * wavelength (WaveNum), and time-point (TimeNum) are returned in IntValues and
 * FloatValues, respectively.
 *
 */
void IMRtExHdrZWT(int istream, int iz, int iw, int it, int ival[], float rval[]) {
  std::cerr << "Warning: IMRtExHdrZWT is not implemented." << std::endl;
}
void IMWrHdr(int istream, const char title[80], int ntflag, float dmin, float dmax, float dmean) {
  std::cerr << "Warning: IMWrHdr is not implemented." << std::endl;
}
void IMWrSec(int istream, const void* array) {
  std::cerr << "Warning: IMWrSec is not implemented." << std::endl;
}