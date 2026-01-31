# resparse
[![License: WTFPL](https://img.shields.io/badge/License-WTFPL-brightgreen.svg)](http://www.wtfpl.net/about/)

## About

Make a file occupy less disk space by deallocating zero-filled blocks. See [Sparse file](https://en.wikipedia.org/wiki/Sparse_file) wikipedia article

## Usage

`resparse.exe <input> [options]`

Positional:
  - `input`  Input file path

Options:
  - `-b`, `--blockSize <int>`  Block size (default: 65536) 
  - `-v`, `--verbose`   Enable verbose output (default: false)
  - `-h`, `--help`      Show this help message
