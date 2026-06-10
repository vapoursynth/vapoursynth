import unittest

import vapoursynth as vs


ALL_LOCATIONS = tuple(vs.ChromaLocation)


def plane_bytes(frame, p):
    # Used for equality comparison
    return bytes(frame[p])


class ChromaLocationMaskTest(unittest.TestCase):
    """Tests for chromaloc-aware mask resampling in
    std.MaskedMerge (first_plane) and std.PreMultiply (alpha).
    """

    def setUp(self):
        self.core = vs.core
        self.core.num_threads = 1

    def create_gray(self, value, w=4, h=4):
        return self.core.std.BlankClip(
            format=vs.GRAY8,
            width=w,
            height=h,
            color=value,
            length=1
        )

    def create_checkerboard(self):
        # 8x8 GRAY8 checkerboard of 4x4 cells making chroma downsample
        # sensitive to both horizontal and vertical chroma siting shift.
        top = self.core.std.StackHorizontal([self.create_gray(20), self.create_gray(235)])
        bottom = self.core.std.StackHorizontal([self.create_gray(235), self.create_gray(20)])
        return self.core.std.StackVertical([top, bottom])

    def create_yuv(self, y, u, v, length=1):
        return self.core.std.BlankClip(
            format=vs.YUV420P8, width=8, height=8, color=(y, u, v), length=length)

    def vary_chromaloc(self, base):
        # Splice base into 6-frame clip whose frames carry _ChromaLocation 0-5
        parts = [base.std.SetFrameProps(_ChromaLocation=i) for i in ALL_LOCATIONS]
        return self.core.std.Splice(parts)

    def repeat(self, clip, n=6):
        return self.core.std.Splice([clip] * n)

    def resize_reference_chroma(self, single_plane, loc, field_based=False):
        # Copy the plane as 4:4:4, then let resize site the chroma to 4:2:0 at
        # `loc`, optionally interlaced. Matches what internal mask resample
        # must reproduce.
        yuv444 = self.core.std.ShufflePlanes(
            (single_plane, single_plane, single_plane),
            (0, 0, 0),
            colorfamily=vs.YUV
        )
        if field_based:
            yuv444 = yuv444.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
        ref = yuv444.resize.Bilinear(format=vs.YUV420P8, chromaloc=loc)
        return plane_bytes(ref.get_frame(0), 1)

    def maskedmerge_reveal_chroma_mask(self, mask, n=0, length=1, field_based=False, chromaloc=None, **kwargs):
        # MaskedMerge(black -> white) returns per-plane resampled mask itself.
        black = self.create_yuv(0, 0, 0, length)
        white = self.create_yuv(255, 255, 255, length)
        if chromaloc is not None:
            black = black.std.SetFrameProps(_ChromaLocation=chromaloc)
            white = white.std.SetFrameProps(_ChromaLocation=chromaloc)
        if field_based:
            # Mask and clips must match in interlacing
            black = black.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
            white = white.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
            mask = mask.std.SetFrameProps(_FieldBased=vs.FIELD_TOP)
        out = self.core.std.MaskedMerge(black, white, mask, first_plane=True, **kwargs)
        return plane_bytes(out.get_frame(n), 1)

    def test_maskedmerge_matches_chromaloc_reference(self):
        # Resampled mask must match location-aware downsample of every siting.
        mask = self.create_checkerboard()
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc),
                    self.resize_reference_chroma(mask, loc))

    def test_maskedmerge_center_equals_unshifted_downsample(self):
        # Old chroma-location-unaware behavior was exactly CHROMA_CENTER siting
        # Simulated here as a reference.
        mask = self.create_checkerboard()
        naive = plane_bytes(mask.resize.Bilinear(width=4, height=4).get_frame(0), 0)
        self.assertEqual(
            self.maskedmerge_reveal_chroma_mask(mask, chromaloc=vs.CHROMA_CENTER),
            naive
        )

    def test_maskedmerge_per_frame_dispatch(self):
        # Resampling must employ frame-specific _ChromaLocation compensation.
        # clipa and clipb carry matching per-frame locations so they stay
        # compatible while the dispatch varies frame to frame.
        mask = self.create_checkerboard()
        black = self.vary_chromaloc(self.create_yuv(0, 0, 0))
        white = self.vary_chromaloc(self.create_yuv(255, 255, 255))
        mask6 = self.repeat(mask)
        out = self.core.std.MaskedMerge(black, white, mask6, first_plane=True)
        results = []
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                result = plane_bytes(out.get_frame(loc), 1)
                self.assertEqual(result, self.resize_reference_chroma(mask, loc))
                results.append(result)
        # Each siting should have a distinct luma alignment
        self.assertEqual(len(set(results)), len(ALL_LOCATIONS))

    def test_maskedmerge_non_subsampled_ignores_chromaloc(self):
        # Chroma siting is irrelevant on non-subsampled clips, so clipa/clipb
        # _ChromaLocation mismatch shouldn't error nor change the result.
        a = self.core.std.BlankClip(format=vs.YUV444P8, width=8, height=8, color=(60, 70, 80))
        b = self.core.std.BlankClip(format=vs.YUV444P8, width=8, height=8, color=(200, 150, 90))
        mask = self.core.std.ShufflePlanes(
            (self.create_checkerboard(), self.create_checkerboard(), self.create_checkerboard()),
            (0, 0, 0),
            colorfamily=vs.YUV
        )
        base = self.core.std.MaskedMerge(a, b, mask, first_plane=True)
        tweaked = self.core.std.MaskedMerge(
            a.std.SetFrameProps(_ChromaLocation=vs.CHROMA_BOTTOM),
            b,
            mask,
            first_plane=True,
        )
        for p in range(3):
            with self.subTest(plane=p):
                self.assertEqual(
                    plane_bytes(base.get_frame(0), p),
                    plane_bytes(tweaked.get_frame(0), p)
                )

    def test_maskedmerge_interlaced_matches_chromaloc_reference(self):
        # For interlaced content, the mask resample must match resize's native
        # interlaced chroma siting for each location (just like progressive)
        mask = self.create_checkerboard()
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc, field_based=True),
                    self.resize_reference_chroma(mask, loc, field_based=True)
                )

    def test_maskedmerge_interlaced_differs_from_progressive(self):
        # Interlacing must actually change the resample, not be ignored.
        mask = self.create_checkerboard()
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertNotEqual(
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc, field_based=True),
                    self.maskedmerge_reveal_chroma_mask(mask, chromaloc=loc)
                )

    def test_maskedmerge_chromaloc_compatibility(self):
        # Subsampled clipa/clipb merge requires matching chroma locations
        mask = self.create_checkerboard()
        off = self.create_yuv(0, 0, 0)
        on = self.create_yuv(255, 255, 255)

        def merged(a_loc, b_loc):
            a = off if a_loc is None else off.std.SetFrameProps(_ChromaLocation=a_loc)
            b = on if b_loc is None else on.std.SetFrameProps(_ChromaLocation=b_loc)
            return self.core.std.MaskedMerge(a, b, mask, first_plane=True)

        # (clipa loc, clipb loc, should_error)
        cases = (
            (vs.CHROMA_LEFT, vs.CHROMA_CENTER, True),   # explicit mismatch
            (None, vs.CHROMA_CENTER, True),             # fallback left vs explicit center
            (None, None, False),                        # both guess left
            (vs.CHROMA_LEFT, None, False),              # explicit left vs fallback left
        )
        for a_loc, b_loc, should_error in cases:
            with self.subTest(a=a_loc, b=b_loc):
                out = merged(a_loc, b_loc)
                if should_error:
                    with self.assertRaises(vs.Error):
                        out.get_frame(0)
                else:
                    out.get_frame(0)  # must not raise

    def premultiply_chroma(self, clip, alpha, chromaloc=None, **kwargs):
        if chromaloc is not None:
            clip = clip.std.SetFrameProps(_ChromaLocation=chromaloc)
        out = self.core.std.PreMultiply(clip, alpha, **kwargs)
        return plane_bytes(out.get_frame(0), 1)

    def premultiply_reference_chroma(self, alpha, loc, field_based=False):
        # With the clip's chroma fixed at 255 and chroma neutral at 128, the
        # premultiply of the resampled alpha (ra) reduces to
        # out = (127 * ra + 127) // 255 + 128
        # Resampled alpha comes from the independent resize reference.
        ra = self.resize_reference_chroma(alpha, loc, field_based=field_based)
        return bytes((127 * v + 127) // 255 + 128 for v in ra)

    def test_premultiply_matches_reference(self):
        alpha = self.create_checkerboard()
        clip = self.create_yuv(128, 255, 255)
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.premultiply_chroma(clip, alpha, chromaloc=loc),
                    self.premultiply_reference_chroma(alpha, loc)
                )

    def test_premultiply_per_frame_dispatch(self):
        alpha = self.repeat(self.create_checkerboard())
        clip = self.vary_chromaloc(self.create_yuv(128, 255, 255))
        out = self.core.std.PreMultiply(clip, alpha)
        single = self.create_checkerboard()
        results = []
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                result = plane_bytes(out.get_frame(loc), 1)
                self.assertEqual(result, self.premultiply_reference_chroma(single, loc))
                results.append(result)
        # Each siting should have a distinct luma alignment
        self.assertEqual(len(set(results)), len(ALL_LOCATIONS))

    def test_premultiply_interlaced_matches_reference(self):
        # The alpha resample for interlaced content should match resize's
        # native interlaced chroma siting.
        alpha = self.create_checkerboard()
        clip = self.create_yuv(128, 255, 255)
        for loc in ALL_LOCATIONS:
            with self.subTest(location=loc.name):
                self.assertEqual(
                    self.premultiply_chroma(
                        clip.std.SetFrameProps(_FieldBased=vs.FIELD_TOP),
                        alpha.std.SetFrameProps(_FieldBased=vs.FIELD_TOP),
                        chromaloc=loc
                    ),
                    self.premultiply_reference_chroma(alpha, loc, field_based=True)
                )


if __name__ == "__main__":
    unittest.main()
