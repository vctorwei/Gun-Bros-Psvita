# SoLoud WavStream EOF fix

This directory contains the WavStream translation unit from SoLoud commit
[`7b6cb7185d12b0d3283a9bf30e6cc3295e57a77c`](https://github.com/jarikomppa/soloud/tree/7b6cb7185d12b0d3283a9bf30e6cc3295e57a77c),
the version used by the VitaSDK SoLoud 1.11 package. Upstream license notices
remain in the source files.

The local change marks a zero decoded Vorbis frame as EOF before entering the
original loop/stop branch. Gun Bros music can decode to 1,105 fewer samples than
its declared granule count. In the original implementation, neither the output
offset nor stream offset advances after this EOF, so the shared mixer loops
forever and both music and sound effects stop.

`stb_vorbis.h` includes `stb_vorbis.c` with `STB_VORBIS_HEADER_ONLY`. They supply
the matching declarations; the decoder is not compiled here. The SDK library
still supplies the decoder, mixer and Vita output backend. Compiling all of the
WavStream methods into the executable prevents the original WavStream archive
member from being selected, without duplicate definitions or global linker
overrides.

This uses the existing two-argument `getAudio(float *, unsigned int)` ABI and
requires `SOLOUD_VERSION == 111`. It preserves positive-frame processing,
looping, WAV playback, output settings and the music/SFX preference code.

## Regression test

With a native Clang/GCC compiler, the pinned upstream ZIP and your original
game music files, run from the repository root:

```sh
python3 tests/test_music_eof.py \
  --upstream-archive /path/to/soloud-7b6cb7185d12b0d3283a9bf30e6cc3295e57a77c.zip \
  --data /path/to/gunbros_free/files
```

The test checks the upstream SHA256, compiles this repository's actual fixed
WavStream against the original decoder/core, and exercises all seven tracks
with mono/stereo and loop/stop playback. Four original-code controls must reach
EOF and stall. No game assets are copied into the repository, and output goes
to the ignored `build/music-eof-tests` directory. `--soloud-source` can instead
point to an exact, unmodified checkout of the pinned upstream version.

Validation of this source change: 28 fixed cases completed and all four original
controls reproduced the EOF stall. ARM compilation and archive linking also
confirmed that the patched WavStream is selected without duplicate definitions.
A full clean project build remains blocked by the pre-existing missing
`lib/falso_jni/FalsoRocro.c`; that dependency is outside this audio-only change.
