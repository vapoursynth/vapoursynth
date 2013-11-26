#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "vscore.h"

enum ParserStates {
    WANT_KEY_START,
    WANT_KEY_CHAR,
    WANT_DELIMITER,
    WANT_VALUE_START,
    WANT_VALUE_CHAR
};

static inline bool isAlphaNumUnderscore(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

VSMap *readSettings(const std::string &path) {
    VSMap *settings = vsapi.createMap();
    std::string err;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        // Ignore "No such file or directory"
        if (errno != ENOENT) {
            err.append("Couldn't open '").append(path).append("' for reading. Error: ").append(strerror(errno));
            vsapi.setError(settings, err.c_str());
        }
        return settings;
    }

    if (fseek(f, 0, SEEK_END)) {
        err.append("Couldn't find the size of '").append(path).append("' by seeking to its end. Error: ").append(strerror(errno));
        vsapi.setError(settings, err.c_str());
        fclose(f);
        return settings;
    }

    long size = ftell(f);
    if (size == -1) {
        err.append("Couldn't find the size of '").append(path).append("'. ftell failed with the error: ").append(strerror(errno));
        vsapi.setError(settings, err.c_str());
        fclose(f);
        return settings;
    }

    if (size > 100*1024) {
        err.append("Configuration file '").append(path).append("' is ridiculously large. Ignoring.");
        vsapi.setError(settings, err.c_str());
        fclose(f);
        return settings;
    }

    rewind(f);

    std::vector<char> buffer(size);

    if (fread(buffer.data(), 1, size, f) != size) {
        err.append("Didn't read the expected number of bytes from '").append(path).append("'.");
        vsapi.setError(settings, err.c_str());
        fclose(f);
        return settings;
    }
    fclose(f);

    buffer.push_back('\n');

    int key_start = 0, key_end = 0, value_start = 0, value_end = 0;
    err.append("Error while parsing '").append(path).append("': ");
    ParserStates state = WANT_KEY_START;
    int line = 1;
    std::string line_str(std::string("Line ").append(std::to_string(line)).append(": "));

    for (int i = 0; i < size; i++) {
        switch (state) {
            case WANT_KEY_START:
                if (isAlphaNumUnderscore(buffer[i])) {
                    key_start = i;
                    state = WANT_KEY_CHAR;
                }
                break;
            case WANT_KEY_CHAR:
                if (buffer[i] == '\n') {
                    err.append(line_str).append("No delimiter found before reaching the end of the line.");
                    vsapi.setError(settings, err.c_str());
                    return settings;
                } else if (buffer[i] == ' ') {
                    key_end = i - 1;
                    state = WANT_DELIMITER;
                } else if (buffer[i] == '=') {
                    key_end = i - 1;
                    state = WANT_VALUE_START;
                } else if (!isAlphaNumUnderscore(buffer[i])) {
                    err.append(line_str).append("Garbage found inside key.");
                    vsapi.setError(settings, err.c_str());
                    return settings;
                }
                break;
            case WANT_DELIMITER:
                if (buffer[i] == '=') {
                    state = WANT_VALUE_START;
                } else if (buffer[i] == '\n') {
                    err.append(line_str).append("No delimiter found before reaching the end of the line.");
                    vsapi.setError(settings, err.c_str());
                    return settings;
                } else {
                    err.append(line_str).append("Expected '=' but found garbage instead.");
                    vsapi.setError(settings, err.c_str());
                    return settings;
                }
                break;
            case WANT_VALUE_START:
                if (buffer[i] == '\n') {
                    err.append(line_str).append("No value found for key before reaching the end of the line.");
                    vsapi.setError(settings, err.c_str());
                    return settings;
                }
                if (buffer[i] != ' ') {
                    value_start = i;
                    state = WANT_VALUE_CHAR;
                }
                break;
            case WANT_VALUE_CHAR:
                if (buffer[i] == '\n') {
                    value_end = i - 1;
                    std::string key(&buffer[key_start], key_end - key_start + 1);
                    std::string value(&buffer[value_start], value_end - value_start + 1);
                    vsapi.propSetData(settings, key.c_str(), value.c_str(), value.size(), paReplace);
                    state = WANT_KEY_START;
                }
                break;
            default:
                err.append(line_str).append("Shit broke. This should never happen.");
                vsapi.setError(settings, err.c_str());
                return settings;
                break;
        }

        if (buffer[i] == '\n') {
            line++;
            line_str = std::string("Line ").append(std::to_string(line)).append(": ");
        }
    }

    return settings;
}

