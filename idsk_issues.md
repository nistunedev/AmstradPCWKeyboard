# iDSK issues with `CPM_14_keyboard.dsk`

Bug notes for a **future upstream report** to iDSK (https://github.com/cpcsdk/idsk).
We are not fixing iDSK now — this just records the concrete evidence so we can
file it cleanly later. **Until then: do NOT use iDSK to write/import into this
image** (reads/listing are fine).

## Environment

- Tool: bundled `iDSK.exe` (iDSK 0.20), Windows.
- Image: `CPM_14_keyboard.dsk`
  - Magic: `EXTENDED CPC DSK File`
  - Creator: `SAMdisk140826`
  - Format: Amstrad PCW CF2 3", single-sided, 512-byte sectors (size code 2),
    9 sectors/track.
- Real geometry: **40 data tracks**, each `0x1300` (4864) bytes
  (256-byte Track-Info block + 9 × 512-byte sectors).
- File size: 204544 (`0x31F00`). 40 tracks end at `0x2F900` (194816); the
  remaining 9728 bytes are a **SAMdisk `Offset-Info` trailer**, not track data.

## Symptom (what the user sees)

`iDSK CPM_14_keyboard.dsk -l` prints, before the (correct) file listing:

```
Warning : track 40 has 164 sectors ! (wanted 9)
Warning : strange sector numbering in track 40!
Warning : track 40 start at sector0 while track 0 starts at 1
Warning : track 41 has 0 sectors ! (wanted 9)
Warning : strange sector numbering in track 41!
Warning : track 41 start at sector255 while track 0 starts at 1
```

The directory listing itself is correct, but any **write/import** (`-i`) produces
a file that loads as repeating filler/garbage bytes on real CP/M (verified in
CP/M Box). A historical import of `KEYTEST.COM` via iDSK loaded as garbage;
transferring the same bytes through an emulated CP/M drive (CP/M Box `M:` + `PIP`)
worked, which isolates the fault to iDSK's on-disk sector placement.

## Root causes

### 1. Phantom tracks 40/41 from the `Offset-Info` trailer

The Extended-DSK header at offset `0x30` declares **42** tracks, but the
track-size table (`0x34`+) has only **40 non-zero** entries (entries 40 and 41
are `0x00`). iDSK trusts the count of 42 instead of treating zero-size tracks as
absent, then reads bytes at the offset just past the last real track
(`0x2F900`). That offset holds SAMdisk's metadata block:

```
0x2F900: 4F 66 66 73 65 74 2D 49 6E 66 6F 0D 0A ...   "Offset-Info\r\n"
```

iDSK parses this `Offset-Info` block as if it were a `Track-Info` block, reading
bogus fields: track=23, side=157, sector-size code=3, sector count=164 — hence
the "164 sectors" / "sector0" / "side 157" warnings. A correct reader should
either honor the zero-size table entries (no track present) or recognize the
`Offset-Info` extension and stop.

### 2. Sector interleave + per-track skew ignored on write

The disk uses a 2:1 sector interleave with a track-to-track skew. Physical
sector-ID order (`R` field from each Track-Info sector list):

| Track | Physical sector-ID order            |
|------:|-------------------------------------|
| 0     | `1, 6, 2, 7, 3, 8, 4, 9, 5`         |
| 1     | `9, 5, 1, 6, 2, 7, 3, 8, 4`         |

CP/M logical records are mapped through this ordering. iDSK's writer does not
place logical sectors according to the actual per-track sector-ID list, so
imported file data lands in the wrong physical sectors and reads back as
garbage. The interleave is also why the directory's logical sectors are
physically non-contiguous in the file (directory entries at file offsets
`0x1900`–`0x1AFF` and `0x1D00`–`0x1EFF`, with unrelated data sectors at `0x1B00`
and `0x1F00` between them).

### 3. (Minor / gotcha) AMSDOS header on type-1 import

`iDSK -i -t 1` prepends a 128-byte AMSDOS header (correct for Amstrad CPC, wrong
for a CP/M `.COM`). Use `-t 2` for a raw import. Not a bug, but it bit us once.

## Minimal reproduction to attach to the report

1. `iDSK CPM_14_keyboard.dsk -l` → observe the track 40/41 warnings above.
2. `iDSK CPM_14_keyboard.dsk -i SMALL.COM -t 2` then read `SMALL.COM` back
   inside real CP/M (or CP/M Box) → content is filler/garbage, not the bytes
   imported.
3. Compare with transferring the same file via an emulated CP/M drive → correct.

Attach: the `.dsk` sample, iDSK version, the warning text, and the per-track
sector-ID tables above.

## Workarounds we use instead

- **Add/replace files**: transfer inside emulated CP/M — map a host folder as a
  CP/M drive (CP/M Box `M:`) and `PIP A:FILE=M:FILE[V]`. The emulated BIOS writes
  the correct geometry.
- **Patch existing bytes**: edit the `.dsk` **in place, same length, located by
  content** (e.g. hex/binary patch). In-place same-length edits don't move any
  sector, so the interleave/skew is preserved and iDSK's write path is avoided
  entirely.
