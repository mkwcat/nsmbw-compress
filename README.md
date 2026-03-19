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

Here is a comparison between compressed sizes of nsmbw-compress, the internal `ntcompress`
tool used by Nintendo, and Cue's Nintendo DS/GBA Compressors for LZ specifically.

The file used here is `Kinopio.arc` from New Super Mario Bros. Wii, which has an
uncompressed size of `381536` bytes. The following table compares the size in bytes
of the output of each format:
| Format          | nsmbw-compress | ntcompress         | Cue (lzx)          |
| --------------- | -------------- | ------------------ | ------------------ |
| `lz`            | `205415`       | `207455` (`+2040`) | `207177` (`+1762`) |
| `lz` (old-lz11) | `212769`       | `214638` (`+1869`) | N/A                |
| `huff` (4-bit)  | `345836`       | `345836` (`+0`)    | N/A                |
| `huff` (8-bit)  | `316208`       | `316208` (`+0`)    | N/A                |
| `rl`            | `345406`       | `345406` (`+0`)    | N/A                |
| `lh`            | `158713`       | `166692` (`+7979`) | N/A                |

In the single-file test, the output of nsmbw-compress is either identical to or smaller
than the output of ntcompress in every supported format, with the greatest savings
exhibited in LH.

The following table compares the `szs` format specifically, which was not supported
by ntcompress, between nsmbw-compress and [Wiimm's SZS Tool (wszst)](https://szs.wiimm.de/wszst/).
| Format          | nsmbw-compress | wszst `--compr 10`  | wszst `--compr 120` |
| --------------- | -------------- | ----------------- | ----------------- |
| `szs`           | `205480`       | `205620` (`+140`) | `205344` (`-136`) |

Wiimm's tool overtakes nsmbw-compress by a fair margin when using the time-consuming
backtracking option (specifically `--compr 120` in this test). I don't intend to create
my own full backtracking alternative, so wszst is still preferred if you need the best
possible compression.

## Building

Run the following in the repository root:
```
mkdir build
cd build
cmake ..
cmake --build .
```
Or simply run your preferred compiler with `*.c` in the repository root.
