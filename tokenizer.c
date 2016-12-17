#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

bool is_in(char c, char *s) {
    return c != EOF && strchr(s, c) != NULL;
}

/* Character buffer operations */
#define BUFFER_DEFAULT_DELTA 32
struct buffer_s {
    char *data;
    size_t size;
    size_t length;
    size_t size_delta;
    size_t read_position;
};
typedef struct buffer_s *buffer;
buffer buffer_new(size_t size_delta) {
    buffer b = (buffer)malloc(sizeof(struct buffer_s));
    if (!b) {
        perror("buffer");
        exit(-1);
    }
    b->size_delta = size_delta < 2 ? BUFFER_DEFAULT_DELTA : size_delta;
    b->data = (char *)malloc(sizeof(char) * b->size_delta);
    if (!b->data) {
        perror("buffer");
        exit(-1);
    }
    b->size = b->size_delta;
    b->length = 0;
    b->read_position = 0;
    return b;
}
void buffer_free(buffer this) {
    if (this->data) free(this->data);
    free(this);
}
void buffer_append(buffer this, char c) {
    if (this->length + 1 >= this->size) {
        this->data = realloc(this->data, this->size += this->size_delta);
        if (!this->data) {
            perror("buffer");
            exit(-1);
        }
    }
    this->data[this->length++] = c;
    this->data[this->length] = 0;
}
bool buffer_iseof(buffer this) {
    return (this->read_position >= this->length);
}
char *buffer_tocstring(buffer this) {
    return this->data + this->read_position;
}
char buffer_getc(buffer this) {
    if (buffer_iseof(this)) return EOF;
    return this->data[this->read_position++];
}
void buffer_ff(buffer this) {
    ++this->read_position;
}
char buffer_peekc(buffer this) {
    if (buffer_iseof(this)) return EOF;
    return this->data[this->read_position];
}
void buffer_readsize(buffer dst, buffer src, size_t size) {
    char ch;
    while (size-- > 0) {
        ch = buffer_getc(src);
        if (ch == EOF) break;
        buffer_append(dst, ch);
    }
}
void buffer_readseg(buffer dst, buffer src, char *allowed) {
    if (buffer_iseof(src)) return;
    char c;
    while (c = buffer_peekc(src), c != EOF && is_in(c, allowed)) {
        buffer_append(dst, c);
        buffer_ff(src);
    }
}
void buffer_readword(buffer dest, buffer src) {
    char c;
    while (c = buffer_peekc(src), c != EOF && (isalnum(c) || is_in(c, "_-"))) {
        buffer_append(dest, c);
        buffer_ff(src);
    }
}
void buffer_readline(buffer dest, buffer src) {
    char c;
    while (c = buffer_peekc(src), !is_in(c, "\n\r") && c != EOF) {
        buffer_append(dest, c);
        buffer_ff(src);
    }
}
char buffer_getpos(buffer this, size_t pos) {
    if (this->read_position + pos >= this->length) return EOF;
    return this->data[this->read_position + pos];
}
size_t buffer_size(buffer this) {
    return this->length - this->read_position;
}

/* string operation */
#define COUNT(X) (sizeof(X) / sizeof(X[0]))

/* token classification */
enum TOKEN_TYPE {
    TYPE_NULL,
    TYPE_CHAR,
    TYPE_CHARSET,
    TYPE_WORD,
    TYPE_LINE,
    TYPE_MULTIWORD
};
enum FORWARD_LOOK_TYPE {
    LL_0,
    LL_1,
};
typedef struct token_s {
    char *display_name;
    enum TOKEN_TYPE type;
    enum FORWARD_LOOK_TYPE fwd;
    char *detection_arg;
    char *detection_arg2;
} token_type;
token_type token_types[11] = {
    { "null",           TYPE_WORD,      LL_0,       " \t\v\f\n\r",  NULL },
    { "macro",          TYPE_LINE,      LL_0,       "#",            NULL },
    { "delimeter",      TYPE_CHAR,      LL_0,       ",;",            NULL },
    { "c_comment",      TYPE_LINE,      LL_1,       "////",         NULL },
    { "cxx_comment",    TYPE_MULTIWORD, LL_1,       "/**/",         NULL },
    { "operator",       TYPE_CHARSET,   LL_0,       "+-*/<>=!&",      NULL },
    { "bracklet",       TYPE_CHAR,      LL_0,       "()[]{}",       NULL },
    { "char_literal",   TYPE_MULTIWORD, LL_0,       "'",            NULL  },
    { "string_literal", TYPE_MULTIWORD, LL_0,       """",           NULL },
    { "number_literal", TYPE_WORD,      LL_0,       "1234567890.",  NULL },
    { "identifier",     TYPE_WORD,      LL_0,       NULL,           NULL },
};
size_t get_token(buffer buf, buffer out) {
    while (is_in(buffer_peekc(buf), token_types[0].detection_arg)) buffer_getc(buf);
    for (size_t i = 0; i < COUNT(token_types); ++i) {
        token_type testing_token_type = token_types[i];
        bool start_detected;

        // detect token start
        switch (testing_token_type.fwd) {
            case LL_0:
                if (!testing_token_type.detection_arg) {
                    start_detected = true;
                } else if (is_in(buffer_peekc(buf), testing_token_type.detection_arg)) {
                    start_detected = true;
                } else {
                    start_detected = false;
                }
                break;
            case LL_1:
                start_detected = true;
                for (size_t i = 0; i < 2; ++i) {
                    if (((char *)testing_token_type.detection_arg)[i] != buffer_getpos(buf, i)) {
                        start_detected = false;
                        break;
                    }
                }
                break;
        }
        if (!start_detected) continue;
        // printf("Detect start token type %s: %c\n", testing_token_type.display_name, *buffer_tocstring(buf));
        // detect token end
        bool end_detected = true;
        char *pos;
        switch (testing_token_type.type) {
            case TYPE_NULL:
                buffer_readseg(out, buf, testing_token_type.detection_arg);
                end_detected = false; // eat blank chars
                break;
            case TYPE_CHAR:
                buffer_readsize(out, buf, 1);
                break;
            case TYPE_CHARSET:
                buffer_readseg(out, buf, testing_token_type.detection_arg);
                break;
            case TYPE_WORD:
                buffer_readword(out, buf);
                break;
            case TYPE_LINE:
                buffer_readline(out, buf);
                break;
            case TYPE_MULTIWORD:
                if (testing_token_type.fwd == LL_0) {
                    pos = strstr(buffer_tocstring(buf) + 1, testing_token_type.detection_arg2);
                    while (pos && *(pos+1) == testing_token_type.detection_arg2[0])
                        pos = strstr(pos + 1, testing_token_type.detection_arg2);
                    if (pos) {
                        size_t size = pos - buffer_tocstring(buf) + 1;
                        buffer_readsize(out, buf, size);
                    } else {
                        end_detected = false;
                        printf("not detected 1\n");
                    }
                } else {
                    pos = strstr(buffer_tocstring(buf), testing_token_type.detection_arg2 + 2);
                    if (pos) {
                        size_t size = pos - buffer_tocstring(buf) + 1;
                        buffer_readsize(out, buf, size);
                    } else {
                        end_detected = false;
                        printf("not detected 2\n");
                    }
                }
                break;
        }
        if (!end_detected) continue;
        return i;
    }
    return -1;
}

int main(void) {
    // read
    buffer program = buffer_new(0);
    char c = 0;
    while ((c = getchar()) != EOF) {
        if (c == 0) {
            fprintf(stderr, "Cannot process char \\0\n");
            exit(-1);
        }
        buffer_append(program, c);
    }
    // printf("%s\n", buffer_tocstring(program));
    // tokenize
    buffer token;
    size_t type;
    while(!buffer_iseof(program)) {
        token = buffer_new(32);
        if ((type = get_token(program, token))) {
            printf("%s: \t\t%s\n", token_types[type].display_name, buffer_tocstring(token));
            buffer_free(token);
            token = buffer_new(32);
        } else break;
    }
    buffer_free(token);
}
