"""Reference-side checks for the pretrained HDR model's domain and color contract."""

import unittest

import numpy as np
import torch

from ArdaOidnExport import ArdaOidnHdr, PU_SCALE, numpy_exposure, pu_forward, pu_inverse


class ArdaOidnColorTests(unittest.TestCase):
    def test_transfer_round_trip_including_piecewise_boundaries(self):
        values = torch.tensor([0, 1e-8, 1.57945760e-6, 1e-4, 3.22087631e-2, 1, 100, 65504])
        recovered = pu_inverse(pu_forward(values) * PU_SCALE / PU_SCALE)
        torch.testing.assert_close(recovered, values, rtol=2e-5, atol=1e-7)

    def test_autoexposure_matches_reference_and_ignores_black_bins(self):
        random = np.random.default_rng(2037)
        image = random.lognormal(-1, 2, (1, 3, 32, 48)).astype(np.float32)
        image[:, :, :16, :16] = 0
        model = ArdaOidnHdr(torch.nn.Identity())
        np.testing.assert_allclose(model.exposure(torch.from_numpy(image)).item(),
                                   numpy_exposure(image), rtol=2e-6)
        self.assertEqual(model.exposure(torch.zeros(1, 3, 32, 48)).item(), 1.0)

    def test_identity_network_returns_linear_hdr_after_exposure_and_inverse(self):
        image = torch.linspace(0, 100, 3 * 32 * 32).reshape(1, 3, 32, 32)
        model = ArdaOidnHdr(torch.nn.Identity())
        torch.testing.assert_close(model(image), image, rtol=2e-5, atol=1e-5)


if __name__ == "__main__":
    unittest.main()
