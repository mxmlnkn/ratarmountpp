# Ratarmount++

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](http://opensource.org/licenses/MIT)
[![Build Status](https://github.com/mxmlnkn/ratarmountpp/workflows/tests/badge.svg)](https://github.com/mxmlnkn/ratarmountpp/actions)
![C++17](https://img.shields.io/badge/C++-17-blue.svg?style=flat-square)


This repository provides a prototype of ratarmount implemented with C++ to improve performance over the Python version, which has its limits in the Python layer itself and Python's global interpreter lock.
For now, this module is a proof of concept and will only work with uncompressed and bzip2 compressed TAR files, which do have preexisting `<file name>.sqlite.index` files created by the original ratarmount.


# Installation

```
sudo apt install g++ cmake
git clone --recursive https://github.com/mxmlnkn/ratarmountpp.git
cd ratarmountpp; mkdir build; cd build
cmake ..
make -j $( nproc )
sudo make install
```
