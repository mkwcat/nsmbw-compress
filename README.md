# nsmbw-compress

Tool for encoding and decoding various different compression formats supported by
New Super Mario Bros. Wii and other games using the same libraries, based on
a [decompilation of the CX library](https://github.com/doldecomp/sdk_2009-12-11/tree/536dd80cde16989a4914305a1f1095122ab1c44f/source/cx).

Here's a list of the supported formats:
| Format        | Extension | Description             | Encode | Decode | Known Uses                      |
| ------------- | --------- | ----------------------- | ------ | ------ | ------------------------------- |
| `lz`          | `.LZ`     | Lempel-Ziv              | Yes    | Yes    | New Super Mario Bros. Wii       |
| `huff`        | `.HUFF`   | Huffman                 | Yes    | Yes    |                                 |
| `rl`          | `.RL`     | Run-length              | Yes    | Yes    |                                 |
| `lh`          | `.LH`     | LZ + Huffman            | Yes    | Yes    | Mario Sports Mix                |
| `lrc`         | `.LRC`    | LZ + Range Coder        | No     | Yes    |                                 |
| `filter-diff` | `.DIFF`   | Differential Filter     | Yes    | Yes    |                                 |
| `szs`         | `.szs`    | SZS/Yaz0                | Yes    | Yes    | Many first-party Nintendo games |
| `ash`         | `.ash`    | ASH0 (LZ + Huffman)     | Yes    | Yes    | Wii Menu                        |
| `asr`         | `.asr`    | ASR0 (LZ + Range Coder) | Yes    | Yes    | Super Mario Advance 4           |

## Usage
```
Usage: nsmbw-compress [options] <input> [-o output]
Options:
  -h, --help      Show this help message and exit
  -o, --output    <path> Specify the output file name
  -t, --type      <type> Specify the compression type (see supported types below)
  -x, --uncomp    Decompress the input file instead of compressing
  -l, --lz-mode   <0, 1*, auto> Specify the LZ compression mode. Select mode 1 for better efficiency in 99% of cases, or auto to compress in both modes and choose the smaller output. Mode 0 might be more compatible with older games, but this is unlikely to ever be relevant.
  -b, --huff-size <4, 8, auto*> Specify the bit size for Huffman compression, or compress both and automatically choose the smaller one.
  -r, --asr-mode  <0, 1, auto*> Specify the mode for ASR compression. Mode 1 has a larger offset range than mode 0. Auto mode will try both and choose the smaller output.
  -d, --diff-size <4, 8*> Specify the element size for filter-diff encoding.
      --test      Run internal tests and exit
  -v, --verbose   Print verbose output
Supported types for compression:
  lz huff rl lh diff szs ash asr 
Supported types for decompression:
  lz huff rl lh lrc diff szs ash asr 
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
