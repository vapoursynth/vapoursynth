# Stop pep8 from complaining (hopefully)
# NOQA

# Ignore Flake Warnings
# flake8: noqa

# Ignore coverage
# (No coverage)

# From https://gist.github.com/pylover/7870c235867cf22817ac5b096defb768
# noinspection PyPep8
# noinspection PyPep8Naming
# noinspection PyTypeChecker
# noinspection PyAbstractClass
# noinspection PyArgumentEqualDefault
# noinspection PyArgumentList
# noinspection PyAssignmentToLoopOrWithParameter
# noinspection PyAttributeOutsideInit
# noinspection PyAugmentAssignment
# noinspection PyBroadException
# noinspection PyByteLiteral
# noinspection PyCallByClass
# noinspection PyChainedComparsons
# noinspection PyClassHasNoInit
# noinspection PyClassicStyleClass
# noinspection PyComparisonWithNone
# noinspection PyCompatibility
# noinspection PyDecorator
# noinspection PyDefaultArgument
# noinspection PyDictCreation
# noinspection PyDictDuplicateKeys
# noinspection PyDocstringTypes
# noinspection PyExceptClausesOrder
# noinspection PyExceptionInheritance
# noinspection PyFromFutureImport
# noinspection PyGlobalUndefined
# noinspection PyIncorrectDocstring
# noinspection PyInitNewSignature
# noinspection PyInterpreter
# noinspection PyListCreation
# noinspection PyMandatoryEncoding
# noinspection PyMethodFirstArgAssignment
# noinspection PyMethodMayBeStatic
# noinspection PyMethodOverriding
# noinspection PyMethodParameters
# noinspection PyMissingConstructor
# noinspection PyMissingOrEmptyDocstring
# noinspection PyNestedDecorators
# noinspection PynonAsciiChar
# noinspection PyNoneFunctionAssignment
# noinspection PyOldStyleClasses
# noinspection PyPackageRequirements
# noinspection PyPropertyAccess
# noinspection PyPropertyDefinition
# noinspection PyProtectedMember
# noinspection PyRaisingNewStyleClass
# noinspection PyRedeclaration
# noinspection PyRedundantParentheses
# noinspection PySetFunctionToLiteral
# noinspection PySimplifyBooleanCheck
# noinspection PySingleQuotedDocstring
# noinspection PyStatementEffect
# noinspection PyStringException
# noinspection PyStringFormat
# noinspection PySuperArguments
# noinspection PyTrailingSemicolon
# noinspection PyTupleAssignmentBalance
# noinspection PyTupleItemAssignment
# noinspection PyUnboundLocalVariable
# noinspection PyUnnecessaryBackslash
# noinspection PyUnreachableCode
# noinspection PyUnresolvedReferences
# noinspection PyUnusedLocal
# noinspection ReturnValueFromInit


from abc import abstractmethod
from ctypes import c_void_p
from enum import IntEnum, IntFlag
from fractions import Fraction
from inspect import Signature
from types import MappingProxyType, TracebackType
from typing import (
    TYPE_CHECKING, Any, BinaryIO, Callable, ContextManager, Dict, Generic, Iterator, Literal,
    MutableMapping, NamedTuple, NoReturn, Optional, Protocol, Sequence, Tuple, Type, TypedDict,
    TypeVar, Union, cast, overload, runtime_checkable
)
from weakref import ReferenceType

__all__ = [
    # Versioning
    '__version__', '__api_version__', 'PluginVersion',

    # Enums and constants
    'MessageType',
        'MESSAGE_TYPE_DEBUG', 'MESSAGE_TYPE_INFORMATION', 'MESSAGE_TYPE_WARNING',
        'MESSAGE_TYPE_CRITICAL', 'MESSAGE_TYPE_FATAL',

    'FilterMode',
        'fmParallel', 'fmParallelRequests', 'fmUnordered', 'fmFrameState',

    'CoreCreationFlags',
        'ccfEnableGraphInspection', 'ccfDisableAutoLoading', 'ccfDisableLibraryUnloading',

    'MediaType',
        'VIDEO', 'AUDIO',

    'ColorFamily',
        'UNDEFINED', 'GRAY', 'RGB', 'YUV',

    'ColorRange',
        'RANGE_FULL', 'RANGE_LIMITED',

    'SampleType',
        'INTEGER', 'FLOAT',

    'PresetVideoFormat',
        'GRAY',
        'GRAY8', 'GRAY9', 'GRAY10', 'GRAY12', 'GRAY14', 'GRAY16', 'GRAY32', 'GRAYH', 'GRAYS',
        'RGB',
        'RGB24', 'RGB27', 'RGB30', 'RGB36', 'RGB42', 'RGB48', 'RGBH', 'RGBS',
        'YUV',
        'YUV410P8',
        'YUV411P8',
        'YUV420P8', 'YUV420P9', 'YUV420P10', 'YUV420P12', 'YUV420P14', 'YUV420P16',
        'YUV422P8', 'YUV422P9', 'YUV422P10', 'YUV422P12', 'YUV422P14', 'YUV422P16',
        'YUV440P8',
        'YUV444P8', 'YUV444P9', 'YUV444P10', 'YUV444P12', 'YUV444P14', 'YUV444P16',
        'YUV420PH', 'YUV422PH', 'YUV444PH',
        'YUV420PS', 'YUV422PS', 'YUV444PS',
        'NONE',

    'AudioChannels',
        'FRONT_LEFT', 'FRONT_RIGHT', 'FRONT_CENTER',
        'BACK_LEFT', 'BACK_RIGHT', 'BACK_CENTER',
        'SIDE_LEFT', 'SIDE_RIGHT',
        'TOP_CENTER',

        'TOP_FRONT_LEFT', 'TOP_FRONT_RIGHT', 'TOP_FRONT_CENTER',
        'TOP_BACK_LEFT', 'TOP_BACK_RIGHT', 'TOP_BACK_CENTER',

        'WIDE_LEFT', 'WIDE_RIGHT',

        'SURROUND_DIRECT_LEFT', 'SURROUND_DIRECT_RIGHT',

        'FRONT_LEFT_OF_CENTER', 'FRONT_RIGHT_OF_CENTER',

        'STEREO_LEFT', 'STEREO_RIGHT',

        'LOW_FREQUENCY', 'LOW_FREQUENCY2',

    'ChromaLocation',
        'CHROMA_TOP_LEFT', 'CHROMA_TOP',
        'CHROMA_LEFT', 'CHROMA_CENTER',
        'CHROMA_BOTTOM_LEFT', 'CHROMA_BOTTOM',

    'FieldBased',
        'FIELD_PROGRESSIVE', 'FIELD_TOP', 'FIELD_BOTTOM',

    'MatrixCoefficients',
        'MATRIX_RGB', 'MATRIX_BT709', 'MATRIX_UNSPECIFIED', 'MATRIX_FCC',
        'MATRIX_BT470_BG', 'MATRIX_ST170_M', 'MATRIX_ST240_M', 'MATRIX_YCGCO', 'MATRIX_BT2020_NCL', 'MATRIX_BT2020_CL',
        'MATRIX_CHROMATICITY_DERIVED_NCL', 'MATRIX_CHROMATICITY_DERIVED_CL', 'MATRIX_ICTCP',

    'TransferCharacteristics',
        'TRANSFER_BT709', 'TRANSFER_UNSPECIFIED', 'TRANSFER_BT470_M', 'TRANSFER_BT470_BG', 'TRANSFER_BT601',
        'TRANSFER_ST240_M', 'TRANSFER_LINEAR', 'TRANSFER_LOG_100', 'TRANSFER_LOG_316', 'TRANSFER_IEC_61966_2_4',
        'TRANSFER_IEC_61966_2_1', 'TRANSFER_BT2020_10', 'TRANSFER_BT2020_12', 'TRANSFER_ST2084', 'TRANSFER_ST428',
        'TRANSFER_ARIB_B67',

    'ColorPrimaries', 'PRIMARIES_BT709', 'PRIMARIES_UNSPECIFIED',
        'PRIMARIES_BT470_M', 'PRIMARIES_BT470_BG', 'PRIMARIES_ST170_M', 'PRIMARIES_ST240_M', 'PRIMARIES_FILM',
        'PRIMARIES_BT2020', 'PRIMARIES_ST428', 'PRIMARIES_ST431_2', 'PRIMARIES_ST432_1', 'PRIMARIES_EBU3213_E',

    # Environment SubSystem
    'Environment', 'EnvironmentData',

    'EnvironmentPolicy',

    'EnvironmentPolicyAPI',
    'register_policy', 'has_policy',
    'register_on_destroy', 'unregister_on_destroy',

    'get_current_environment',

    'VideoOutputTuple',
    'clear_output', 'clear_outputs', 'get_outputs', 'get_output',

    # Logging
    'LogHandle', 'Error',

    # Functions
    'FuncData', 'Func', 'FramePtr',
    'Plugin', 'Function',

    # Formats
    'VideoFormat', 'ChannelLayout',

    # Frames
    'RawFrame', 'VideoFrame', 'AudioFrame',
    'FrameProps',

    # Nodes
    'RawNode', 'VideoNode', 'AudioNode',

    'Core', '_CoreProxy', 'core',

    # Inspection API [UNSTABLE API]
    # '_try_enable_introspection'
]


###
# Typing

T = TypeVar('T')
S = TypeVar('S')

SingleAndSequence = Union[T, Sequence[T]]


@runtime_checkable
class SupportsString(Protocol):
    @abstractmethod
    def __str__(self) -> str:
        ...


DataType = Union[str, bytes, bytearray, SupportsString]

_VapourSynthMapValue = Union[
    SingleAndSequence[int],
    SingleAndSequence[float],
    SingleAndSequence[DataType],
    SingleAndSequence['VideoNode'],
    SingleAndSequence['VideoFrame'],
    SingleAndSequence['AudioNode'],
    SingleAndSequence['AudioFrame'],
    SingleAndSequence['VSMapValueCallback[Any]']
]

BoundVSMapValue = TypeVar('BoundVSMapValue', bound=_VapourSynthMapValue)

VSMapValueCallback = Callable[..., BoundVSMapValue]


class _Future(Generic[T]):
    def set_result(self, value: T) -> None: ...

    def set_exception(self, exception: BaseException) -> None: ...

    def result(self) -> T: ...

    def exception(self) -> Union[NoReturn, None]: ...

###
# Typed dicts


class _VideoFormatInfo(TypedDict):
    id: int
    name: str
    color_family: 'ColorFamily'
    sample_type: 'SampleType'
    bits_per_sample: int
    bytes_per_sample: int
    subsampling_w: int
    subsampling_h: int
    num_planes: int


###
# VapourSynth Versioning


class VapourSynthVersion(NamedTuple):
    release_major: int
    release_minor: int


class VapourSynthAPIVersion(NamedTuple):
    api_major: int
    api_minor: int


__version__: VapourSynthVersion
__api_version__: VapourSynthAPIVersion


###
# Plugin Versioning


class PluginVersion(NamedTuple):
    major: int
    minor: int


###
# VapourSynth Enums and Constants


class MessageType(IntFlag):
    MESSAGE_TYPE_DEBUG = cast(MessageType, ...)
    MESSAGE_TYPE_INFORMATION = cast(MessageType, ...)
    MESSAGE_TYPE_WARNING = cast(MessageType, ...)
    MESSAGE_TYPE_CRITICAL = cast(MessageType, ...)
    MESSAGE_TYPE_FATAL = cast(MessageType, ...)


MESSAGE_TYPE_DEBUG: Literal[MessageType.MESSAGE_TYPE_DEBUG]
MESSAGE_TYPE_INFORMATION: Literal[MessageType.MESSAGE_TYPE_INFORMATION]
MESSAGE_TYPE_WARNING: Literal[MessageType.MESSAGE_TYPE_WARNING]
MESSAGE_TYPE_CRITICAL: Literal[MessageType.MESSAGE_TYPE_CRITICAL]
MESSAGE_TYPE_FATAL: Literal[MessageType.MESSAGE_TYPE_FATAL]


class FilterMode(IntEnum):
    PARALLEL = cast(FilterMode, ...)
    PARALLEL_REQUESTS = cast(FilterMode, ...)
    UNORDERED = cast(FilterMode, ...)
    FRAME_STATE = cast(FilterMode, ...)


PARALLEL: Literal[FilterMode.PARALLEL]
PARALLEL_REQUESTS: Literal[FilterMode.PARALLEL_REQUESTS]
UNORDERED: Literal[FilterMode.UNORDERED]
FRAME_STATE: Literal[FilterMode.FRAME_STATE]


class CoreCreationFlags(IntFlag):
    ENABLE_GRAPH_INSPECTION = cast(CoreCreationFlags, ...)
    DISABLE_AUTO_LOADING = cast(CoreCreationFlags, ...)
    DISABLE_LIBRARY_UNLOADING = cast(CoreCreationFlags, ...)


ENABLE_GRAPH_INSPECTION: Literal[CoreCreationFlags.ENABLE_GRAPH_INSPECTION]
DISABLE_AUTO_LOADING: Literal[CoreCreationFlags.DISABLE_AUTO_LOADING]
DISABLE_LIBRARY_UNLOADING: Literal[CoreCreationFlags.DISABLE_LIBRARY_UNLOADING]


class MediaType(IntEnum):
    VIDEO = cast(MediaType, ...)
    AUDIO = cast(MediaType, ...)


VIDEO: Literal[MediaType.VIDEO]
AUDIO: Literal[MediaType.AUDIO]


class ColorFamily(IntEnum):
    UNDEFINED = cast(ColorFamily, ...)
    GRAY = cast(ColorFamily, ...)
    RGB = cast(ColorFamily, ...)
    YUV = cast(ColorFamily, ...)


UNDEFINED: Literal[ColorFamily.UNDEFINED]
GRAY: Literal[ColorFamily.GRAY]
RGB: Literal[ColorFamily.RGB]
YUV: Literal[ColorFamily.YUV]


class ColorRange(IntEnum):
    RANGE_FULL = cast(ColorRange, ...)
    RANGE_LIMITED = cast(ColorRange, ...)


RANGE_FULL: Literal[ColorRange.RANGE_FULL]
RANGE_LIMITED: Literal[ColorRange.RANGE_LIMITED]


class SampleType(IntEnum):
    INTEGER = cast(SampleType, ...)
    FLOAT = cast(SampleType, ...)


INTEGER: Literal[SampleType.INTEGER]
FLOAT: Literal[SampleType.FLOAT]


class PresetVideoFormat(IntEnum):
    NONE = cast(PresetVideoFormat, ...)

    GRAY8 = cast(PresetVideoFormat, ...)
    GRAY9 = cast(PresetVideoFormat, ...)
    GRAY10 = cast(PresetVideoFormat, ...)
    GRAY12 = cast(PresetVideoFormat, ...)
    GRAY14 = cast(PresetVideoFormat, ...)
    GRAY16 = cast(PresetVideoFormat, ...)
    GRAY32 = cast(PresetVideoFormat, ...)

    GRAYH = cast(PresetVideoFormat, ...)
    GRAYS = cast(PresetVideoFormat, ...)

    YUV420P8 = cast(PresetVideoFormat, ...)
    YUV422P8 = cast(PresetVideoFormat, ...)
    YUV444P8 = cast(PresetVideoFormat, ...)
    YUV410P8 = cast(PresetVideoFormat, ...)
    YUV411P8 = cast(PresetVideoFormat, ...)
    YUV440P8 = cast(PresetVideoFormat, ...)

    YUV420P9 = cast(PresetVideoFormat, ...)
    YUV422P9 = cast(PresetVideoFormat, ...)
    YUV444P9 = cast(PresetVideoFormat, ...)

    YUV420P10 = cast(PresetVideoFormat, ...)
    YUV422P10 = cast(PresetVideoFormat, ...)
    YUV444P10 = cast(PresetVideoFormat, ...)

    YUV420P12 = cast(PresetVideoFormat, ...)
    YUV422P12 = cast(PresetVideoFormat, ...)
    YUV444P12 = cast(PresetVideoFormat, ...)

    YUV420P14 = cast(PresetVideoFormat, ...)
    YUV422P14 = cast(PresetVideoFormat, ...)
    YUV444P14 = cast(PresetVideoFormat, ...)

    YUV420P16 = cast(PresetVideoFormat, ...)
    YUV422P16 = cast(PresetVideoFormat, ...)
    YUV444P16 = cast(PresetVideoFormat, ...)

    YUV420PH = cast(PresetVideoFormat, ...)
    YUV420PS = cast(PresetVideoFormat, ...)

    YUV422PH = cast(PresetVideoFormat, ...)
    YUV422PS = cast(PresetVideoFormat, ...)

    YUV444PH = cast(PresetVideoFormat, ...)
    YUV444PS = cast(PresetVideoFormat, ...)

    RGB24 = cast(PresetVideoFormat, ...)
    RGB27 = cast(PresetVideoFormat, ...)
    RGB30 = cast(PresetVideoFormat, ...)
    RGB36 = cast(PresetVideoFormat, ...)
    RGB42 = cast(PresetVideoFormat, ...)
    RGB48 = cast(PresetVideoFormat, ...)

    RGBH = cast(PresetVideoFormat, ...)
    RGBS = cast(PresetVideoFormat, ...)


NONE: Literal[PresetVideoFormat.NONE]

GRAY8: Literal[PresetVideoFormat.GRAY8]
GRAY9: Literal[PresetVideoFormat.GRAY9]
GRAY10: Literal[PresetVideoFormat.GRAY10]
GRAY12: Literal[PresetVideoFormat.GRAY12]
GRAY14: Literal[PresetVideoFormat.GRAY14]
GRAY16: Literal[PresetVideoFormat.GRAY16]
GRAY32: Literal[PresetVideoFormat.GRAY32]

GRAYH: Literal[PresetVideoFormat.GRAYH]
GRAYS: Literal[PresetVideoFormat.GRAYS]

YUV420P8: Literal[PresetVideoFormat.YUV420P8]
YUV422P8: Literal[PresetVideoFormat.YUV422P8]
YUV444P8: Literal[PresetVideoFormat.YUV444P8]
YUV410P8: Literal[PresetVideoFormat.YUV410P8]
YUV411P8: Literal[PresetVideoFormat.YUV411P8]
YUV440P8: Literal[PresetVideoFormat.YUV440P8]

YUV420P9: Literal[PresetVideoFormat.YUV420P9]
YUV422P9: Literal[PresetVideoFormat.YUV422P9]
YUV444P9: Literal[PresetVideoFormat.YUV444P9]

YUV420P10: Literal[PresetVideoFormat.YUV420P10]
YUV422P10: Literal[PresetVideoFormat.YUV422P10]
YUV444P10: Literal[PresetVideoFormat.YUV444P10]

YUV420P12: Literal[PresetVideoFormat.YUV420P12]
YUV422P12: Literal[PresetVideoFormat.YUV422P12]
YUV444P12: Literal[PresetVideoFormat.YUV444P12]

YUV420P14: Literal[PresetVideoFormat.YUV420P14]
YUV422P14: Literal[PresetVideoFormat.YUV422P14]
YUV444P14: Literal[PresetVideoFormat.YUV444P14]

YUV420P16: Literal[PresetVideoFormat.YUV420P16]
YUV422P16: Literal[PresetVideoFormat.YUV422P16]
YUV444P16: Literal[PresetVideoFormat.YUV444P16]

YUV420PH: Literal[PresetVideoFormat.YUV420PH]
YUV420PS: Literal[PresetVideoFormat.YUV420PS]

YUV422PH: Literal[PresetVideoFormat.YUV422PH]
YUV422PS: Literal[PresetVideoFormat.YUV422PS]

YUV444PH: Literal[PresetVideoFormat.YUV444PH]
YUV444PS: Literal[PresetVideoFormat.YUV444PS]

RGB24: Literal[PresetVideoFormat.RGB24]
RGB27: Literal[PresetVideoFormat.RGB27]
RGB30: Literal[PresetVideoFormat.RGB30]
RGB36: Literal[PresetVideoFormat.RGB36]
RGB42: Literal[PresetVideoFormat.RGB42]
RGB48: Literal[PresetVideoFormat.RGB48]

RGBH: Literal[PresetVideoFormat.RGBH]
RGBS: Literal[PresetVideoFormat.RGBS]


class AudioChannels(IntEnum):
    FRONT_LEFT = cast(AudioChannels, ...)
    FRONT_RIGHT = cast(AudioChannels, ...)
    FRONT_CENTER = cast(AudioChannels, ...)
    LOW_FREQUENCY = cast(AudioChannels, ...)
    BACK_LEFT = cast(AudioChannels, ...)
    BACK_RIGHT = cast(AudioChannels, ...)
    FRONT_LEFT_OF_CENTER = cast(AudioChannels, ...)
    FRONT_RIGHT_OF_CENTER = cast(AudioChannels, ...)
    BACK_CENTER = cast(AudioChannels, ...)
    SIDE_LEFT = cast(AudioChannels, ...)
    SIDE_RIGHT = cast(AudioChannels, ...)
    TOP_CENTER = cast(AudioChannels, ...)
    TOP_FRONT_LEFT = cast(AudioChannels, ...)
    TOP_FRONT_CENTER = cast(AudioChannels, ...)
    TOP_FRONT_RIGHT = cast(AudioChannels, ...)
    TOP_BACK_LEFT = cast(AudioChannels, ...)
    TOP_BACK_CENTER = cast(AudioChannels, ...)
    TOP_BACK_RIGHT = cast(AudioChannels, ...)
    STEREO_LEFT = cast(AudioChannels, ...)
    STEREO_RIGHT = cast(AudioChannels, ...)
    WIDE_LEFT = cast(AudioChannels, ...)
    WIDE_RIGHT = cast(AudioChannels, ...)
    SURROUND_DIRECT_LEFT = cast(AudioChannels, ...)
    SURROUND_DIRECT_RIGHT = cast(AudioChannels, ...)
    LOW_FREQUENCY2 = cast(AudioChannels, ...)


FRONT_LEFT: Literal[AudioChannels.FRONT_LEFT]
FRONT_RIGHT: Literal[AudioChannels.FRONT_RIGHT]
FRONT_CENTER: Literal[AudioChannels.FRONT_CENTER]
LOW_FREQUENCY: Literal[AudioChannels.LOW_FREQUENCY]
BACK_LEFT: Literal[AudioChannels.BACK_LEFT]
BACK_RIGHT: Literal[AudioChannels.BACK_RIGHT]
FRONT_LEFT_OF_CENTER: Literal[AudioChannels.FRONT_LEFT_OF_CENTER]
FRONT_RIGHT_OF_CENTER: Literal[AudioChannels.FRONT_RIGHT_OF_CENTER]
BACK_CENTER: Literal[AudioChannels.BACK_CENTER]
SIDE_LEFT: Literal[AudioChannels.SIDE_LEFT]
SIDE_RIGHT: Literal[AudioChannels.SIDE_RIGHT]
TOP_CENTER: Literal[AudioChannels.TOP_CENTER]
TOP_FRONT_LEFT: Literal[AudioChannels.TOP_FRONT_LEFT]
TOP_FRONT_CENTER: Literal[AudioChannels.TOP_FRONT_CENTER]
TOP_FRONT_RIGHT: Literal[AudioChannels.TOP_FRONT_RIGHT]
TOP_BACK_LEFT: Literal[AudioChannels.TOP_BACK_LEFT]
TOP_BACK_CENTER: Literal[AudioChannels.TOP_BACK_CENTER]
TOP_BACK_RIGHT: Literal[AudioChannels.TOP_BACK_RIGHT]
STEREO_LEFT: Literal[AudioChannels.STEREO_LEFT]
STEREO_RIGHT: Literal[AudioChannels.STEREO_RIGHT]
WIDE_LEFT: Literal[AudioChannels.WIDE_LEFT]
WIDE_RIGHT: Literal[AudioChannels.WIDE_RIGHT]
SURROUND_DIRECT_LEFT: Literal[AudioChannels.SURROUND_DIRECT_LEFT]
SURROUND_DIRECT_RIGHT: Literal[AudioChannels.SURROUND_DIRECT_RIGHT]
LOW_FREQUENCY2: Literal[AudioChannels.LOW_FREQUENCY2]


class ChromaLocation(IntEnum):
    CHROMA_LEFT = cast(ChromaLocation, ...)
    CHROMA_CENTER = cast(ChromaLocation, ...)
    CHROMA_TOP_LEFT = cast(ChromaLocation, ...)
    CHROMA_TOP = cast(ChromaLocation, ...)
    CHROMA_BOTTOM_LEFT = cast(ChromaLocation, ...)
    CHROMA_BOTTOM = cast(ChromaLocation, ...)


CHROMA_LEFT: Literal[ChromaLocation.CHROMA_LEFT]
CHROMA_CENTER: Literal[ChromaLocation.CHROMA_CENTER]
CHROMA_TOP_LEFT: Literal[ChromaLocation.CHROMA_TOP_LEFT]
CHROMA_TOP: Literal[ChromaLocation.CHROMA_TOP]
CHROMA_BOTTOM_LEFT: Literal[ChromaLocation.CHROMA_BOTTOM_LEFT]
CHROMA_BOTTOM: Literal[ChromaLocation.CHROMA_BOTTOM]


class FieldBased(IntEnum):
    FIELD_PROGRESSIVE = cast(FieldBased, ...)
    FIELD_TOP = cast(FieldBased, ...)
    FIELD_BOTTOM = cast(FieldBased, ...)


FIELD_PROGRESSIVE: Literal[FieldBased.FIELD_PROGRESSIVE]
FIELD_TOP: Literal[FieldBased.FIELD_TOP]
FIELD_BOTTOM: Literal[FieldBased.FIELD_BOTTOM]


class MatrixCoefficients(IntEnum):
    MATRIX_RGB = cast(MatrixCoefficients, ...)
    MATRIX_BT709 = cast(MatrixCoefficients, ...)
    MATRIX_UNSPECIFIED = cast(MatrixCoefficients, ...)
    MATRIX_FCC = cast(MatrixCoefficients, ...)
    MATRIX_BT470_BG = cast(MatrixCoefficients, ...)
    MATRIX_ST170_M = cast(MatrixCoefficients, ...)
    MATRIX_ST240_M = cast(MatrixCoefficients, ...)
    MATRIX_YCGCO = cast(MatrixCoefficients, ...)
    MATRIX_BT2020_NCL = cast(MatrixCoefficients, ...)
    MATRIX_BT2020_CL = cast(MatrixCoefficients, ...)
    MATRIX_CHROMATICITY_DERIVED_NCL = cast(MatrixCoefficients, ...)
    MATRIX_CHROMATICITY_DERIVED_CL = cast(MatrixCoefficients, ...)
    MATRIX_ICTCP = cast(MatrixCoefficients, ...)


MATRIX_RGB: Literal[MatrixCoefficients.MATRIX_RGB]
MATRIX_BT709: Literal[MatrixCoefficients.MATRIX_BT709]
MATRIX_UNSPECIFIED: Literal[MatrixCoefficients.MATRIX_UNSPECIFIED]
MATRIX_FCC: Literal[MatrixCoefficients.MATRIX_FCC]
MATRIX_BT470_BG: Literal[MatrixCoefficients.MATRIX_BT470_BG]
MATRIX_ST170_M: Literal[MatrixCoefficients.MATRIX_ST170_M]
MATRIX_ST240_M: Literal[MatrixCoefficients.MATRIX_ST240_M]
MATRIX_YCGCO: Literal[MatrixCoefficients.MATRIX_YCGCO]
MATRIX_BT2020_NCL: Literal[MatrixCoefficients.MATRIX_BT2020_NCL]
MATRIX_BT2020_CL: Literal[MatrixCoefficients.MATRIX_BT2020_CL]
MATRIX_CHROMATICITY_DERIVED_NCL: Literal[MatrixCoefficients.MATRIX_CHROMATICITY_DERIVED_NCL]
MATRIX_CHROMATICITY_DERIVED_CL: Literal[MatrixCoefficients.MATRIX_CHROMATICITY_DERIVED_CL]
MATRIX_ICTCP: Literal[MatrixCoefficients.MATRIX_ICTCP]


class TransferCharacteristics(IntEnum):
    TRANSFER_BT709 = cast(TransferCharacteristics, ...)
    TRANSFER_UNSPECIFIED = cast(TransferCharacteristics, ...)
    TRANSFER_BT470_M = cast(TransferCharacteristics, ...)
    TRANSFER_BT470_BG = cast(TransferCharacteristics, ...)
    TRANSFER_BT601 = cast(TransferCharacteristics, ...)
    TRANSFER_ST240_M = cast(TransferCharacteristics, ...)
    TRANSFER_LINEAR = cast(TransferCharacteristics, ...)
    TRANSFER_LOG_100 = cast(TransferCharacteristics, ...)
    TRANSFER_LOG_316 = cast(TransferCharacteristics, ...)
    TRANSFER_IEC_61966_2_4 = cast(TransferCharacteristics, ...)
    TRANSFER_IEC_61966_2_1 = cast(TransferCharacteristics, ...)
    TRANSFER_BT2020_10 = cast(TransferCharacteristics, ...)
    TRANSFER_BT2020_12 = cast(TransferCharacteristics, ...)
    TRANSFER_ST2084 = cast(TransferCharacteristics, ...)
    TRANSFER_ST428 = cast(TransferCharacteristics, ...)
    TRANSFER_ARIB_B67 = cast(TransferCharacteristics, ...)


TRANSFER_BT709: Literal[TransferCharacteristics.TRANSFER_BT709]
TRANSFER_UNSPECIFIED: Literal[TransferCharacteristics.TRANSFER_UNSPECIFIED]
TRANSFER_BT470_M: Literal[TransferCharacteristics.TRANSFER_BT470_M]
TRANSFER_BT470_BG: Literal[TransferCharacteristics.TRANSFER_BT470_BG]
TRANSFER_BT601: Literal[TransferCharacteristics.TRANSFER_BT601]
TRANSFER_ST240_M: Literal[TransferCharacteristics.TRANSFER_ST240_M]
TRANSFER_LINEAR: Literal[TransferCharacteristics.TRANSFER_LINEAR]
TRANSFER_LOG_100: Literal[TransferCharacteristics.TRANSFER_LOG_100]
TRANSFER_LOG_316: Literal[TransferCharacteristics.TRANSFER_LOG_316]
TRANSFER_IEC_61966_2_4: Literal[TransferCharacteristics.TRANSFER_IEC_61966_2_4]
TRANSFER_IEC_61966_2_1: Literal[TransferCharacteristics.TRANSFER_IEC_61966_2_1]
TRANSFER_BT2020_10: Literal[TransferCharacteristics.TRANSFER_BT2020_10]
TRANSFER_BT2020_12: Literal[TransferCharacteristics.TRANSFER_BT2020_12]
TRANSFER_ST2084: Literal[TransferCharacteristics.TRANSFER_ST2084]
TRANSFER_ST428: Literal[TransferCharacteristics.TRANSFER_ST428]
TRANSFER_ARIB_B67: Literal[TransferCharacteristics.TRANSFER_ARIB_B67]


class ColorPrimaries(IntEnum):
    PRIMARIES_BT709 = cast(ColorPrimaries, ...)
    PRIMARIES_UNSPECIFIED = cast(ColorPrimaries, ...)
    PRIMARIES_BT470_M = cast(ColorPrimaries, ...)
    PRIMARIES_BT470_BG = cast(ColorPrimaries, ...)
    PRIMARIES_ST170_M = cast(ColorPrimaries, ...)
    PRIMARIES_ST240_M = cast(ColorPrimaries, ...)
    PRIMARIES_FILM = cast(ColorPrimaries, ...)
    PRIMARIES_BT2020 = cast(ColorPrimaries, ...)
    PRIMARIES_ST428 = cast(ColorPrimaries, ...)
    PRIMARIES_ST431_2 = cast(ColorPrimaries, ...)
    PRIMARIES_ST432_1 = cast(ColorPrimaries, ...)
    PRIMARIES_EBU3213_E = cast(ColorPrimaries, ...)


PRIMARIES_BT709: Literal[ColorPrimaries.PRIMARIES_BT709]
PRIMARIES_UNSPECIFIED: Literal[ColorPrimaries.PRIMARIES_UNSPECIFIED]
PRIMARIES_BT470_M: Literal[ColorPrimaries.PRIMARIES_BT470_M]
PRIMARIES_BT470_BG: Literal[ColorPrimaries.PRIMARIES_BT470_BG]
PRIMARIES_ST170_M: Literal[ColorPrimaries.PRIMARIES_ST170_M]
PRIMARIES_ST240_M: Literal[ColorPrimaries.PRIMARIES_ST240_M]
PRIMARIES_FILM: Literal[ColorPrimaries.PRIMARIES_FILM]
PRIMARIES_BT2020: Literal[ColorPrimaries.PRIMARIES_BT2020]
PRIMARIES_ST428: Literal[ColorPrimaries.PRIMARIES_ST428]
PRIMARIES_ST431_2: Literal[ColorPrimaries.PRIMARIES_ST431_2]
PRIMARIES_ST432_1: Literal[ColorPrimaries.PRIMARIES_ST432_1]
PRIMARIES_EBU3213_E: Literal[ColorPrimaries.PRIMARIES_EBU3213_E]


###
# VapourSynth Environment SubSystem


class EnvironmentData:
    def __init__(self) -> NoReturn: ...


class EnvironmentPolicy:
    def on_policy_registered(self, special_api: 'EnvironmentPolicyAPI') -> None: ...

    def on_policy_cleared(self) -> None: ...

    @abstractmethod
    def get_current_environment(self) -> Union[EnvironmentData, None]: ...

    @abstractmethod
    def set_environment(self, environment: Union[EnvironmentData, None]) -> Union[EnvironmentData, None]: ...

    def is_alive(self, environment: EnvironmentData) -> bool: ...


class EnvironmentPolicyAPI:
    def __init__(self) -> NoReturn: ...

    def wrap_environment(self, environment_data: EnvironmentData) -> 'Environment': ...

    def create_environment(self, flags: int = 0) -> EnvironmentData: ...

    def set_logger(self, env: EnvironmentData, logger: Callable[[int, str], None]) -> None: ...

    def get_vapoursynth_api(self, version: int) -> c_void_p: ...

    def get_core_ptr(self, environment_data: EnvironmentData) -> c_void_p: ...

    def destroy_environment(self, env: EnvironmentData) -> None: ...

    def unregister_policy(self) -> None: ...


def register_policy(policy: EnvironmentPolicy) -> None:
    ...


if not TYPE_CHECKING:
    def _try_enable_introspection(version: int = None): ...


def has_policy() -> bool:
    ...


def register_on_destroy(callback: Callable[..., None]) -> None:
    ...


def unregister_on_destroy(callback: Callable[..., None]) -> None:
    ...


class Environment:
    env: ReferenceType[EnvironmentData]

    def __init__(self) -> NoReturn: ...

    @property
    def alive(self) -> bool: ...

    @property
    def single(self) -> bool: ...

    @classmethod
    def is_single(cls) -> bool: ...

    @property
    def env_id(self) -> int: ...

    @property
    def active(self) -> bool: ...

    def copy(self) -> 'Environment': ...

    def use(self) -> ContextManager[None]: ...

    def __eq__(self, other: 'Environment') -> bool: ...  # type: ignore[override]

    def __repr__(self) -> str: ...


def get_current_environment() -> Environment:
    ...


class Local:
    def __getattr__(self, key: str) -> Any: ...
    
    # Even though object does have set/del methods, typecheckers will treat them differently
    # when they are not explicit; for example by raising a member not found warning.

    def __setattr__(self, key: str, value: Any) -> None: ...
    
    def __delattr__(self, key: str) -> None: ...


class VideoOutputTuple(NamedTuple):
    clip: 'VideoNode'
    alpha: Union['VideoNode', None]
    alt_output: Literal[0, 1, 2]


class Error(Exception):
    ...


def clear_output(index: int = 0) -> None:
    ...


def clear_outputs() -> None:
    ...


def get_outputs() -> MappingProxyType[int, Union[VideoOutputTuple, 'AudioNode']]:
    ...


def get_output(index: int = 0) -> Union[VideoOutputTuple, 'AudioNode']:
    ...


class FuncData:
    def __init__(self) -> NoReturn: ...

    def __call__(self, **kwargs: _VapourSynthMapValue) -> _VapourSynthMapValue: ...


class Func:
    def __init__(self) -> NoReturn: ...

    def __call__(self, **kwargs: _VapourSynthMapValue) -> _VapourSynthMapValue: ...


class FramePtr:
    def __init__(self) -> NoReturn: ...


class VideoFormat:
    id: int
    name: str
    color_family: ColorFamily
    sample_type: SampleType
    bits_per_sample: int
    bytes_per_sample: int
    subsampling_w: int
    subsampling_h: int
    num_planes: int

    def __init__(self) -> NoReturn: ...

    def _as_dict(self) -> _VideoFormatInfo: ...

    def replace(
        self, *,
        color_family: Union[ColorFamily, None] = None,
        sample_type: Union[SampleType, None] = None,
        bits_per_sample: Union[int, None] = None,
        subsampling_w: Union[int, None] = None,
        subsampling_h: Union[int, None] = None
    ) -> 'VideoFormat': ...

    @overload
    def __eq__(self, other: 'VideoFormat') -> bool: ...

    @overload
    def __eq__(self, other: Any) -> Literal[False]: ...

    def __int__(self) -> int: ...

    def __hash__(self) -> int: ...


class FrameProps(MutableMapping[str, _VapourSynthMapValue]):
    def __init__(self) -> NoReturn: ...

    def setdefault(
        self, key: str, default: _VapourSynthMapValue = 0
    ) -> _VapourSynthMapValue: ...

    def copy(self) -> MutableMapping[str, _VapourSynthMapValue]: ...

    # Since we're inheriting from the MutableMapping abstract class,
    # we *have* to specify that we have indeed created these methods.
    # If we don't, mypy will complain that we're working with abstract methods.

    def __setattr__(self, name: str, value: _VapourSynthMapValue) -> None: ...

    def __getattr__(self, name: str) -> _VapourSynthMapValue: ...

    def __delattr__(self, name: str) -> None: ...

    def __setitem__(self, name: str, value: _VapourSynthMapValue) -> None: ...

    def __getitem__(self, name: str) -> _VapourSynthMapValue: ...

    def __delitem__(self, name: str) -> None: ...

    def __iter__(self) -> Iterator[str]: ...

    def __len__(self) -> int: ...


class ChannelLayout(int):
    def __init__(self) -> NoReturn: ...

    def __contains__(self, layout: AudioChannels) -> bool: ...

    def __iter__(self) -> Iterator[AudioChannels]: ...

    @overload
    def __eq__(self, other: 'ChannelLayout') -> bool: ...

    @overload
    def __eq__(self, other: Any) -> Literal[False]: ...

    def __len__(self) -> int: ...


class audio_view(memoryview):  # type: ignore[misc]
    @property
    def shape(self) -> tuple[int]: ...

    @property
    def strides(self) -> tuple[int]: ...

    @property
    def ndim(self) -> Literal[1]: ...

    @property
    def obj(self) -> FramePtr: ...  # type: ignore[override]

    def __getitem__(self, index: int) -> int | float: ...  # type: ignore[override]

    def __setitem__(self, index: int, other: int | float) -> None: ...  # type: ignore[override]

    def tolist(self) -> list[int | float]: ...  # type: ignore[override]


class video_view(memoryview):  # type: ignore[misc]
    @property
    def shape(self) -> tuple[int, int]: ...

    @property
    def strides(self) -> tuple[int, int]: ...

    @property
    def ndim(self) -> Literal[2]: ...

    @property
    def obj(self) -> FramePtr: ...  # type: ignore[override]

    def __getitem__(self, index: Tuple[int, int]) -> int | float: ...  # type: ignore[override]

    def __setitem__(self, index: Tuple[int, int], other: int | float) -> None: ...  # type: ignore[override]

    def tolist(self) -> list[int | float]: ...  # type: ignore[override]


class RawFrame:
    def __init__(self) -> None: ...

    @property
    def closed(self) -> bool: ...

    def close(self) -> None: ...

    def copy(self: 'SelfFrame') -> 'SelfFrame': ...

    @property
    def props(self) -> FrameProps: ...

    @props.setter
    def props(self, new_props: MappingProxyType[str, _VapourSynthMapValue]) -> None: ...

    def get_write_ptr(self, plane: int) -> c_void_p: ...

    def get_read_ptr(self, plane: int) -> c_void_p: ...

    def get_stride(self, plane: int) -> int: ...

    @property
    def readonly(self) -> bool: ...

    def __enter__(self: 'SelfFrame') -> 'SelfFrame': ...

    def __exit__(
        self, exc_type: Union[Type[BaseException], None],
        exc_value: Union[BaseException, None],
        traceback: Union[TracebackType, None], /,
    ) -> Union[bool, None]: ...

    def __getitem__(self, index: int) -> memoryview: ...

    def __len__(self) -> int: ...


SelfFrame = TypeVar('SelfFrame', bound=RawFrame)


class VideoFrame(RawFrame):
    format: VideoFormat
    width: int
    height: int

    def readchunks(self) -> Iterator[video_view]: ...

    def __getitem__(self, index: int) -> video_view: ...


class AudioFrame(RawFrame):
    sample_type: SampleType
    bits_per_sample: int
    bytes_per_sample: int
    channel_layout: int
    num_channels: int

    @property
    def channels(self) -> ChannelLayout: ...

    def __getitem__(self, index: int) -> audio_view: ...

#include <plugins/implementations>


class RawNode:
    def __init__(self) -> None: ...

    def get_frame(self, n: int) -> RawFrame: ...

    @overload
    def get_frame_async(self, n: int, cb: None = None) -> _Future[RawFrame]: ...

    @overload
    def get_frame_async(self, n: int, cb: Callable[[Union[RawFrame, None], Union[Exception, None]], None]) -> None: ...

    def frames(
        self, prefetch: Union[int, None] = None, backlog: Union[int, None] = None, close: bool = False
    ) -> Iterator[RawFrame]: ...

    def clear_cache(self) -> None: ...

    def set_output(self, index: int = 0) -> None: ...

    def is_inspectable(self, version: Union[int, None] = None) -> bool: ...

    if not TYPE_CHECKING:
        @property
        def _node_name(self) -> str: ...

        @property
        def _name(self) -> str: ...

        @property
        def _inputs(self) -> Dict[str, _VapourSynthMapValue]: ...

        @property
        def _timings(self) -> int: ...

        @property
        def _mode(self) -> FilterMode: ...

        @property
        def _dependencies(self): ...

    @overload
    def __eq__(self: 'SelfRawNode', other: 'SelfRawNode', /) -> bool: ...

    @overload
    def __eq__(self, other: Any, /) -> Literal[False]: ...

    def __add__(self: 'SelfRawNode', other: 'SelfRawNode', /) -> 'SelfRawNode': ...

    def __radd__(self: 'SelfRawNode', other: 'SelfRawNode', /) -> 'SelfRawNode': ...

    def __mul__(self: 'SelfRawNode', other: int) -> 'SelfRawNode': ...

    def __rmul__(self: 'SelfRawNode', other: int) -> 'SelfRawNode': ...

    def __getitem__(self: 'SelfRawNode', index: Union[int, slice], /) -> 'SelfRawNode': ...

    def __len__(self) -> int: ...


SelfRawNode = TypeVar('SelfRawNode', bound=RawNode)


class VideoNode(RawNode):
    format: VideoFormat

    width: int
    height: int

    fps_num: int
    fps_den: int

    fps: Fraction

    num_frames: int

    def set_output(
        self, index: int = 0, alpha: Union['VideoNode', None] = None, alt_output: Literal[0, 1, 2] = 0
    ) -> None: ...

    def output(
        self, fileobj: BinaryIO, y4m: bool = False, progress_update: Callable[[int, int], None] | None = None,
        prefetch: int = 0, backlog: int = -1
    ) -> None: ...

    def get_frame(self, n: int) -> VideoFrame: ...

    @overload  # type: ignore[override]
    def get_frame_async(self, n: int, cb: None = None) -> _Future[VideoFrame]: ...

    @overload
    def get_frame_async(self, n: int, cb: Callable[[Union[VideoFrame, None], Union[Exception, None]], None]) -> None: ...

    def frames(
        self, prefetch: Union[int, None] = None, backlog: Union[int, None] = None, close: bool = False
    ) -> Iterator[VideoFrame]: ...

#include <plugins/bound/VideoNode>


class AudioNode(RawNode):
    sample_type: SampleType
    bits_per_sample: int
    bytes_per_sample: int

    channel_layout: int
    num_channels: int

    sample_rate: int
    num_samples: int

    num_frames: int

    @property
    def channels(self) -> ChannelLayout: ...

    def get_frame(self, n: int) -> AudioFrame: ...

    @overload  # type: ignore[override]
    def get_frame_async(self, n: int, cb: None = None) -> _Future[AudioFrame]: ...

    @overload
    def get_frame_async(self, n: int, cb: Callable[[Union[AudioFrame, None], Union[Exception, None]], None]) -> None: ...

    def frames(
        self, prefetch: Union[int, None] = None, backlog: Union[int, None] = None, close: bool = False
    ) -> Iterator[AudioFrame]: ...

#include <plugins/bound/AudioNode>


class LogHandle:
    def __init__(self) -> NoReturn: ...


class Function:
    plugin: 'Plugin'
    name: str
    signature: str
    return_signature: str

    def __init__(self) -> NoReturn: ...

    def __call__(self, *args: _VapourSynthMapValue, **kwargs: _VapourSynthMapValue) -> _VapourSynthMapValue: ...

    @property
    def __signature__(self) -> Signature: ...


class Plugin:
    identifier: str
    namespace: str
    name: str

    def __init__(self) -> NoReturn: ...

    def __getattr__(self, name: str) -> Function: ...

    def functions(self) -> Iterator[Function]: ...

    @property
    def version(self) -> PluginVersion: ...

    @property
    def plugin_path(self) -> str: ...


class Core:
    def __init__(self) -> NoReturn: ...

    @property
    def num_threads(self) -> int: ...

    @num_threads.setter
    def num_threads(self, value: int) -> None: ...

    @property
    def max_cache_size(self) -> int: ...

    @max_cache_size.setter
    def max_cache_size(self, value: int) -> None: ...

    @property
    def used_cache_size(self) -> int: ...

    @property
    def flags(self) -> int: ...

    def plugins(self) -> Iterator[Plugin]: ...

    def query_video_format(
        self, color_family: ColorFamily, sample_type: SampleType, bits_per_sample: int, subsampling_w: int = 0,
        subsampling_h: int = 0
    ) -> VideoFormat: ...

    def get_video_format(self, id: Union[VideoFormat, int, PresetVideoFormat]) -> VideoFormat: ...

    def create_video_frame(self, format: VideoFormat, width: int, height: int) -> VideoFrame: ...

    def log_message(self, message_type: MessageType, message: str) -> None: ...

    def add_log_handler(self, handler_func: Callable[[MessageType, str], None]) -> LogHandle: ...

    def remove_log_handler(self, handle: LogHandle) -> None: ...
      
    def clear_cache(self) -> None: ...
      
    @property
    def core_version(self) -> VapourSynthVersion: ...

    @property
    def api_version(self) -> VapourSynthAPIVersion: ...

    # deprecated
    def version(self) -> str: ...

    # deprecated
    def version_number(self) -> int: ...

#include <plugins/bound/Core>


class _CoreProxy(Core):
    @property
    def core(self) -> Core: ...


core: _CoreProxy
