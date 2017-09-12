#ifdef _MSC_VER
#undef fseeko
#undef ftello
#define fseeko _fseeki64
#define ftello _ftelli64
#else
// To shut up the "implicit declaration of fseeko" warning.
#define _POSIX_C_SOURCE 200112L
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iconv.h>

#ifdef _WIN32
#include <windows.h>
#endif



// Function stolen from libass.
static char *sub_recode(char *data, size_t *size, const char *codepage, char *error, size_t error_size) {
    iconv_t icdsc;
    char *tocp = "UTF-8";
    char *outbuf;

    if ((icdsc = iconv_open(tocp, codepage)) == (iconv_t) (-1)) {
        snprintf(error, error_size, "failed to open iconv descriptor.");

        return NULL;
    }

    {
        size_t osize = *size;
        size_t ileft = *size;
        size_t oleft = *size - 1;
        char *ip;
        char *op;
        size_t rc;
        int clear = 0;

        outbuf = malloc(osize);
        if (!outbuf) {
            snprintf(error, error_size, "failed to allocate %zu bytes for iconv.", osize);

            goto out;
        }

        ip = data;
        op = outbuf;

        while (1) {
            if (ileft)
                rc = iconv(icdsc, &ip, &ileft, &op, &oleft);
            else {              // clear the conversion state and leave
                clear = 1;
                rc = iconv(icdsc, NULL, NULL, &op, &oleft);
            }
            if (rc == (size_t) (-1)) {
                if (errno == E2BIG) {
                    size_t offset = op - outbuf;
                    char *nbuf = realloc(outbuf, osize + *size);
                    if (!nbuf) {
                        free(outbuf);
                        outbuf = NULL;
                        snprintf(error, error_size, "failed to reallocate %zu bytes for iconv.", osize + *size);
                        goto out;
                    }
                    outbuf = nbuf;
                    op = outbuf + offset;
                    osize += *size;
                    oleft += *size;
                } else {
                    snprintf(error, error_size, "failed to convert subtitle file to UTF8, at byte %zu.", (size_t)(ip - data));
                    free(outbuf);
                    outbuf = NULL;
                    goto out;
                }
            } else if (clear)
                break;
        }
        outbuf[osize - oleft - 1] = 0;
        *size = osize - oleft - 1;
    }

out:
    if (icdsc != (iconv_t) (-1)) {
        (void) iconv_close(icdsc);
    }

    return outbuf;
}


static int isUtf8(const char *charset) {
    size_t length = strlen(charset);
    if (length != 4 && length != 5)
        return 0;

    char charset_upper[10] = { 0 };

    int diff = 'a' - 'A';
    for (size_t i = 0; i < length; i++) {
        charset_upper[i] = charset[i];
        if (charset[i] >= 'a' && charset[i] <= 'z')
            charset_upper[i] -= diff;
    }

    return strcmp(charset_upper, "UTF8") == 0 ||
           strcmp(charset_upper, "UTF-8") == 0;
}


char *convertToUtf8(const char *file_name, const char *charset, int64_t *file_size, char *error, size_t error_size) {
#ifdef _WIN32
    int required_size = MultiByteToWideChar(CP_UTF8, 0, file_name, -1, NULL, 0);
    wchar_t *wbuffer = malloc(required_size * sizeof(wchar_t));
    if (!wbuffer) {
        snprintf(error, error_size, "failed to allocate %zu bytes for file name conversion for _wfopen.", required_size * sizeof(wchar_t));

        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, file_name, -1, wbuffer, required_size) != required_size) {
        free(wbuffer);

        snprintf(error, error_size, "file name conversion for _wfopen failed.");

        return NULL;
    }
    FILE *f = _wfopen(wbuffer, L"rb");
    free(wbuffer);
#else
    FILE *f = fopen(file_name, "rb");
#endif

    if (!f) {
        snprintf(error, error_size, "failed to open subtitle file: %s.", strerror(errno));

        return NULL;
    }

    if (fseeko(f, 0, SEEK_END)) {
        snprintf(error, error_size, "failed to seek to the end of the subtitle file: %s.", strerror(errno));

        fclose(f);

        return NULL;
    }

    *file_size = ftello(f);
    if (*file_size == -1) {
        snprintf(error, error_size, "failed to obtain the size of the subtitle file: %s.", strerror(errno));

        fclose(f);

        return NULL;
    }

    if (fseeko(f, 0, SEEK_SET)) {
        snprintf(error, error_size, "failed to seek back to the beginning of the subtitle file: %s.", strerror(errno));

        fclose(f);

        return NULL;
    }


    char *contents = malloc(*file_size);
    if (!contents) {
        snprintf(error, error_size, "failed to allocate %" PRId64 " bytes for the contents of the subtitle file.", *file_size);

        fclose(f);

        return NULL;
    }

    size_t bytes_read = fread(contents, 1, *file_size, f);
    if (bytes_read != (size_t)*file_size) {
        snprintf(error, error_size, "expected to read %" PRId64 " bytes, but read only %zu bytes.", *file_size, bytes_read);

        fclose(f);
        free(contents);

        return NULL;
    }

    fclose(f);
    f = NULL;


    if (charset && !isUtf8(charset)) {
        char *utf8_contents = sub_recode(contents, (size_t *)file_size, charset, error, error_size);
        free(contents);
        contents = utf8_contents;
    }

    return contents;
}
