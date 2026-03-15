# nsmbw-compress

Tool for encoding and decoding various different compression formats supported by
New Super Mario Bros. Wii, based on
a [decompilation of the CX library](https://github.com/doldecomp/sdk_2009-12-11/tree/536dd80cde16989a4914305a1f1095122ab1c44f/source/cx).

Here's a list of the supported formats:
| Format        | Extension | Description         | Encode | Decode |
| ------------- | --------- | ------------------- | ------ | ------ |
| `lz`          | `.LZ`     | Lempel-Ziv          | Yes    | Yes    |
| `huff`        | `.HUFF`   | Huffman             | Yes    | Yes    |
| `rl`          | `.RL`     | Run-length          | Yes    | Yes    |
| `lh`          | `.LH`     | LZ + Huffman        | Yes    | Yes    |
| `lrc`         | `.LRC`    | LZ + Range Coder    | No     | Yes    |
| `filter-diff` | `.DIFF`   | Differential Filter | Yes    | Yes    |
| `szs`         | `.szs`    | SZS/Yaz0            | Yes    | Yes    |

## Usage
```
Usage: nsmbw-compress [options] <input> [-o output]
Options:
  -h, --help     Show this help message and exit
  -o, --output   Specify the output file name
  -t, --type     Specify the compression type (see supported types below)
  -x, --uncomp   Decompress the input file instead of compressing
  -b, --bitsize  Specify the bit size for Huffman compression (4 or 8, default: 8)
  -l, --old-lz11 Use the older low-efficiency mode of LZ11
  -d, --diffsize Specify the size for filter-diff compression (8 or 16, default: 8)
      --test     Run internal tests and exit
  -v, --verbose  Print verbose output
Supported types for compression:
  lz huff rl lh filter-diff szs 
Supported types for decompression:
  lz huff rl lh lrc filter-diff szs 
```

## Comparisons

Here is a comparison between compressed sizes of nsmbw-compress and the internal `ntcompress`
tool used by Nintendo. The file used here is `Kinopio.arc` from New Super Mario Bros. Wii,
which has an uncompressed size of `381536` bytes. The following table compares the size in bytes
of the output of each format:
| Format          | nsmbw-compress | ntcompress         |
| --------------- | -------------- | ------------------ |
| `lz`            | `205549`       | `207455` (`+1906`) |
| `lz` (old-lz11) | `212909`       | `214638` (`+1729`) |
| `huff` (4-bit)  | `345836`       | `345836` (`+0`)    |
| `huff` (8-bit)  | `316208`       | `316208` (`+0`)    |
| `rl`            | `345406`       | `345406` (`+0`)    |
| `lh`            | `158756`       | `166692` (`+7936`) |

In the single-file test, the output of nsmbw-compress is either identical to or smaller than the
output of ntcompress in every supported format, with the greatest savings exhibited in LH.

## Building

Run the following in the repository root:
```
mkdir build
cd build
cmake ..
cmake --build .
```
Or simply run your preferred compiler with `*.c` in the repository root.
