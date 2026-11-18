"""Stable tensor storage and offline numeric decoding, without a GPU library.

The export policy requires native low-precision features on the requested target.
It is deliberately separate from the Backend's ability to store arbitrary bytes.
"""

from dataclasses import dataclass

from ArdaModelPackage import ExportError, check


@dataclass(frozen=True)
class DTypeInfo:
    name: str
    dtype_id: int
    byte_size: int
    min_sm: int
    storage_dtype: str


# IDs are EArdaRHIDataType, not ONNX or CUDA enums. Thresholds are the export
# policy for native half arithmetic, BF16, FP8 conversion/MMA, and INT8 DP4A.
# They do not promise a particular instruction or Tensor Core lowering.
_TYPES = (
    DTypeInfo("bool", 1, 1, 0, "|b1"),
    DTypeInfo("int8", 2, 1, 61, "|i1"),
    DTypeInfo("uint8", 3, 1, 0, "|u1"),
    DTypeInfo("int16", 4, 2, 0, "<i2"),
    DTypeInfo("uint16", 5, 2, 0, "<u2"),
    DTypeInfo("int32", 6, 4, 0, "<i4"),
    DTypeInfo("uint32", 7, 4, 0, "<u4"),
    DTypeInfo("int64", 8, 8, 0, "<i8"),
    DTypeInfo("uint64", 9, 8, 0, "<u8"),
    DTypeInfo("float16", 10, 2, 53, "<f2"),
    DTypeInfo("bfloat16", 11, 2, 80, "<u2"),
    DTypeInfo("float32", 12, 4, 0, "<f4"),
    DTypeInfo("float64", 13, 8, 0, "<f8"),
    DTypeInfo("float8_e4m3fn", 14, 1, 89, "|u1"),
    DTypeInfo("float8_e5m2", 15, 1, 89, "|u1"),
    DTypeInfo("complex64", 16, 8, 0, "<c8"),
    DTypeInfo("complex128", 17, 16, 0, "<c16"),
)
_BY_NAME = {info.name: info for info in _TYPES}
_BY_ID = {info.dtype_id: info for info in _TYPES}
_ALIASES = {"fp16": "float16", "half": "float16", "bf16": "bfloat16",
            "fp32": "float32", "float": "float32", "fp8": "float8_e4m3fn",
            "fp8_e4m3fn": "float8_e4m3fn", "fp8_e5m2": "float8_e5m2"}
EXPORT_DTYPES = ("float32", "float16", "bfloat16", "float8_e4m3fn", "float8_e5m2", "int8", "int32")


def dtype_info(value):
    if type(value) is int:
        result = _BY_ID.get(value)
    else:
        name = str(value).removeprefix("torch.").lower()
        result = _BY_NAME.get(_ALIASES.get(name, name))
    check(result is not None, f"Unknown tensor dtype: {value!r}")
    return result


def canonical_dtype(value):
    return dtype_info(value).name


def validate_target_dtype(value, sm):
    info = dtype_info(value)
    check(sm >= info.min_sm,
          f"{info.name} export requires sm_{info.min_sm} or newer; requested sm_{sm}. "
          "No dtype conversion or architecture substitution is performed.")


def numpy_storage_dtype(value):
    import numpy as np
    return np.dtype(dtype_info(value).storage_dtype)


def decode_array(array, value):
    """Decode an NPZ storage array or GPU bytes into values for offline comparison.

    BF16 uses uint16 bits and FP8 uses uint8 bits so NPZ loading never depends on
    pickled custom dtypes. No floating-point rounding is applied to stored bits.
    """
    import numpy as np
    info = dtype_info(value)
    array = np.asarray(array)
    check(array.dtype == numpy_storage_dtype(value),
          f"{info.name} reference storage must be {info.storage_dtype}, found {array.dtype}")
    if info.name == "bfloat16":
        return (array.astype(np.uint32) << 16).view(np.float32)
    if info.name in ("float8_e4m3fn", "float8_e5m2"):
        mantissa_bits, bias = (3, 7) if info.name == "float8_e4m3fn" else (2, 15)
        bits = array.astype(np.uint32)
        fraction = bits & ((1 << mantissa_bits) - 1)
        exponent = (bits & 0x7f) >> mantissa_bits
        mantissa = fraction.astype(np.float32) / (1 << mantissa_bits)
        decoded = np.ldexp(np.where(exponent == 0, mantissa, mantissa + 1),
                           np.where(exponent == 0, 1 - bias, exponent.astype(np.int32) - bias))
        if mantissa_bits == 3:
            decoded = np.where((exponent == 15) & (fraction == 7), np.nan, decoded)
        else:
            decoded = np.where(exponent == 31, np.where(fraction == 0, np.inf, np.nan), decoded)
        return np.where(bits & 0x80, -decoded, decoded).astype(np.float32)
    return array


def decode_bytes(raw, shape, value):
    import numpy as np
    return decode_array(np.frombuffer(raw, dtype=numpy_storage_dtype(value)).reshape(shape), value)
