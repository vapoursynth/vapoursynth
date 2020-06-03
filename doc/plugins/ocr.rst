.. _ocr:

OCR
===

A filter that performs optical character recognition on video frames.

`Tesseract 3.04.00 language data files <https://github.com/tesseract-ocr/tessdata/tree/3.04.00>`_
are required. See the *datapath* parameter.

.. function:: Recognize(clip clip[, string datapath, string language="", string[] options])
   :module: ocr

   This function runs Tesseract on each video frame and adds the following
   properties:

      OCRString
         The OCR result as UTF-8 string.

      OCRConfidence
         Confidence value for each recognized word as an array of integers in
         range 0-100. The number of confidence values should correspond to the
         number of space-delimited words in ``OCRString``.

   Parameters:

      clip
         Clip to be processed. Must be grayscale with 8 bits per sample.

      datapath
         Path to a folder containing a “tessdata” folder, in which Tesseract’s
         data files must be found. Must have a trailing slash.

         In Windows, this parameter’s default value is the folder where the
         Ocr plugin DLL resides. In other operating systems, this parameter’s
         default value is empty, and Tesseract’s default data path will be used.

      language
         An ISO 639-3 language string. Uses Tesseract’s default language
         if unset (usually ``eng``). The language may be a string of the form
         ``[~]<lang>[+[~]<lang>]*``, indicating that multiple languages are to
         be loaded. E.g. ``hin+eng`` will load Hindi and English. Languages
         may specify internally that they want to be loaded with one or more
         other languages, so the ``~`` sign is available to override that.
         E.g. if ``hin`` were set to load ``eng`` by default, then ``hin+~eng``
         would force loading only ``hin``. The number of loaded languages is
         limited only by memory, with the caveat that loading additional
         languages will impact both speed and accuracy, as there is more work
         to do to decide on the applicable language, and there is more chance
         of hallucinating incorrect words.

      options
         Options to be passed to Tesseract, as a list of (key, value) pairs.
         Available options are documented in ``tesseractclass.h`` of Tesseract’s
         source code.

         .. warning::
             Tesseract is not completely thread-safe. Changing any of the
             options starting with ``classify`` or ``textord`` will change them
             for all instances of this filter.

    Example::

        ret = core.ocr.Recognize(src, language="eng", options=["tessedit_char_whitelist", "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.:;,-!?\"'"])

    .. note::
        This only really works on frames that contain nothing but text, so make
        sure to filter the input appropriately if this is not the case.

