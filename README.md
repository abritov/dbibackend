# DBI Backend

A clone of https://github.com/lunixoid/dbibackend written in C to not depend on python and its deps

Fast USB server for installing games into Nintendo Switch via DBI installer.

## Requirements

### Nintendo Switch

- DBI v202 or later

### Host

- GCC or compatible C compiler
- libusb-1.0

## Installation

**macOS:**

```bash
brew install libusb
cd dbibackend
make
```

**Linux (Debian/Ubuntu):**

```bash
sudo apt-get install libusb-1.0-0-dev
cd dbibackend
make
```

**Linux (Fedora/RHEL):**

```bash
sudo dnf install libusb-devel
cd dbibackend
make
```

**Windows (MSYS2/MinGW):**

```bash
# Install MSYS2 from https://www.msys2.org/
# Open MSYS2 MinGW 64-bit terminal
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb
cd dbibackend
gcc -o dbibackend.exe dbibackend.c -lusb-1.0
```

**Windows (Visual Studio):**

```bash
# Install libusb from https://libusb.info/
# Extract libusb and note the path
# Open Developer Command Prompt
cd dbibackend
cl dbibackend.c /I"C:\path\to\libusb\include\libusb-1.0" /link libusb-1.0.lib /LIBPATH:"C:\path\to\libusb\MinGW64\dll"
```

**Optional - Install system-wide (Linux/macOS):**

```bash
sudo make install
```

## Usage

**Linux/macOS:**

```bash
./dbibackend /path/to/titles
```

With debug output:

```bash
./dbibackend --debug /path/to/titles
```

**Windows:**

```bash
dbibackend.exe C:\path\to\titles
```

With debug output:

```bash
dbibackend.exe --debug C:\path\to\titles
```

Show help:

```bash
./dbibackend --help
# or on Windows
dbibackend.exe --help
```

## Installation Steps

1. Connect your Nintendo Switch to your PC via USB
2. Run the backend server pointing to your titles directory
3. Launch DBI on your Switch
4. Navigate to "Install titles from USB"
5. Select and install your titles

## Supported File Formats

- `.nsp` - Nintendo Submission Package
- `.nsz` - Compressed NSP
- `.xci` - Nintendo Switch Game Card Image

## Features

- Recursive directory scanning for titles
- USB bulk transfer for fast installation
- Support for large files with chunked transfers (1MB buffer)
- Debug logging for troubleshooting
- Cross-platform support (macOS, Linux, Windows)
- Low memory footprint
- Direct USB communication without interpreter overhead

## Troubleshooting

**Device not found:**

- Ensure your Switch is connected via USB
- On Linux, you may need to run with `sudo`
- On Windows, install the libusbK driver using Zadig (see below)
- Make sure DBI is running on your Switch

**Compilation errors:**

- Ensure libusb-1.0 development headers are installed
- Check that pkg-config can find libusb: `pkg-config --libs libusb-1.0`
