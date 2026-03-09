# nsmbw-compress

Tool for encoding and decoding various different compression formats supported by New Super Mario Bros. Wii, based on a [decompilation of the CX library](https://github.com/doldecomp/sdk_2009-12-11/tree/536dd80cde16989a4914305a1f1095122ab1c44f/source/cx).

Supported formats for **compression** are:
- LZ77 (nsmbw: `.LZ`)
- Huffman (`.HUFF`)
- Run-length (nsmbw: `.RL`)
- LZ77 + Huffman (nsmbw: `.LH`)
  - Encoding may be currently unstable or inefficient.
- SZS/Yaz0 (nsmbw: `.szs`)

All formats supported by New Super Mario Bros. Wii can be decompressed, which includes the following:
- LZ77 (nsmbw: `.LZ`)
- Huffman (`.HUFF`)
- Run-length (nsmbw: `.RL`)
- LZ77 + Huffman (nsmbw: `.LH`)
- LRC(?) (nsmbw: `.LRC`)
- "Filter Diff" (`.DIFF`)
- SZS/Yaz0 (nsmbw: `.szs`)

## Usage
```
Usage: nsmbw-compress [options] <input> [-o output]
Options:
  -h, --help     Show this help message and exit
  -o, --output   Specify the output file name
  -t, --type     Specify the compression type (see supported types below)
  -x, --uncomp   Decompress the input file instead of compressing
  -b, --bitsize  Specify the bit size for Huffman compression (4 or 8, default: 8)
  -l, --old-lz77 Use the older low-efficiency mode of LZ77
      --test     Run internal tests and exit
  -v, --verbose  Print verbose output
Supported types for compression:
  lz huff rl lh szs
Supported types for decompression:
  lz huff rl lh lrc filter-diff szs
```

## Building

Run the following in the repository root:
```
mkdir build
cd build
cmake ..
cmake --build .
```
