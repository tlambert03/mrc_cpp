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

    hdr.print();
  }

  void readSec(void* array, int t = 0, int c = 0, int z = 0) {
    if (closed) {
      throw std::runtime_error("Cannot read from closed file. Please reopen with .open()");
    }

    size_t frame_size = hdr.ny * hdr.nx * getPixelSize();
    int header_size = 1024 + hdr.inbsym;
    int section_offset = calculateOffset(t, c, z);
    _file->seekg(header_size + section_offset * frame_size);
    _file->read(reinterpret_cast<char*>(array), frame_size);
  }

  size_t getPixelSize() { return getPixelTypeSize(static_cast<PixelType>(hdr.mode)); }

  int calculateOffset(int t, int c, int z) {
    // determine offset given data order of TCZYX
    if (t >= hdr.num_times) {
      throw std::runtime_error("Time index out of range");
    }
    if (c >= hdr.num_waves) {
      throw std::runtime_error("Wavelength index out of range");
    }
    int num_real_z =
        hdr.nz / (hdr.num_waves ? hdr.num_waves : 1) / (hdr.num_times ? hdr.num_times : 1);
    if (z >= num_real_z) {
      throw std::runtime_error("Section index out of range");
    }
    return (t * hdr.num_waves * num_real_z + c * num_real_z + z);
  }

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

// not yet implemented ///////////////////////////////////

void IMAlCon(int istream, int flag);
void IMAlLab(int istream, const char* labels, int nl);
void IMAlPrt(int flag);
int IMPosnZWT(int istream, int iz, int iw, int it);
void IMPutHdr(int istream, const void* header);  // header is a pointer to IW_MRC_HEADER
void IMRdSec(int istream, void* array);
void IMRtExHdrZWT(int istream, int iz, int iw, int it, int ival[], float rval[]);
void IMWrHdr(int istream, const char title[80], int ntflag, float dmin, float dmax, float dmean);
void IMWrSec(int istream, const void* array);
