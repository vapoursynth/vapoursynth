import os


def get_include():
    """
    Return the directory that contains the VapourSynth header files.
    """
    return os.path.join(os.path.dirname(__file__), "include")
