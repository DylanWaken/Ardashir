"""Bit-pattern and target-policy checks independent of the model compiler."""

import unittest

import numpy as np
import torch

from ArdaDTypes import canonical_dtype, decode_array, dtype_info, validate_target_dtype
from ArdaModelPackage import ExportError


class PrecisionStorageTests(unittest.TestCase):
    def test_all_bfloat16_patterns_match_torch_decoding(self):
        storage = np.arange(65536, dtype=np.uint16)
        expected = torch.from_numpy(storage).view(torch.bfloat16).float().numpy()
        actual = decode_array(storage, "bf16")
        np.testing.assert_array_equal(actual, expected)
        np.testing.assert_array_equal(np.signbit(actual[np.isfinite(actual)]),
                                      np.signbit(expected[np.isfinite(expected)]))

    def test_all_float8_patterns_match_torch_decoding(self):
        storage = np.arange(256, dtype=np.uint8)
        for name in ("float8_e4m3fn", "float8_e5m2"):
            with self.subTest(dtype=name):
                expected = torch.from_numpy(storage).view(getattr(torch, name)).float().numpy()
                actual = decode_array(storage, name)
                np.testing.assert_array_equal(actual, expected)
                np.testing.assert_array_equal(np.signbit(actual[np.isfinite(actual)]),
                                              np.signbit(expected[np.isfinite(expected)]))

    def test_raw_reference_storage_cannot_be_silently_cast(self):
        for name in ("float16", "bfloat16", "float8_e4m3fn", "float8_e5m2", "int8"):
            with self.subTest(dtype=name), self.assertRaises(ExportError):
                decode_array(np.zeros(4, dtype=np.float32), name)

    def test_architecture_requirements_and_stable_type_ids(self):
        for name, identity, width, threshold in (("fp16", 10, 2, 53), ("bf16", 11, 2, 80),
                ("fp8", 14, 1, 89), ("float8_e5m2", 15, 1, 89), ("int8", 2, 1, 61)):
            with self.subTest(dtype=name):
                info = dtype_info(name)
                self.assertEqual((info.dtype_id, info.byte_size, info.min_sm), (identity, width, threshold))
                self.assertEqual(dtype_info(identity), info)
                validate_target_dtype(name, threshold)
                with self.assertRaises(ExportError):
                    validate_target_dtype(name, threshold - 1)
        self.assertEqual(canonical_dtype(torch.bfloat16), "bfloat16")


if __name__ == "__main__":
    unittest.main()
