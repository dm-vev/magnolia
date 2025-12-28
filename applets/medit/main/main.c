#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PYE_VERSION " V2.79 "

typedef int32_t Key;

enum {
    KEY_NONE = 0x00,
    KEY_UP = 0x0B,
    KEY_DOWN = 0x0D,
    KEY_LEFT = 0x1F,
    KEY_RIGHT = 0x1E,
    KEY_HOME = 0x10,
    KEY_END = 0x03,
    KEY_PGUP = 0xFFF1,
    KEY_PGDN = 0xFFF2,
    KEY_WORD_LEFT = 0xFFF3,
    KEY_WORD_RIGHT = 0xFFF4,
    KEY_SHIFT_UP = 0xFFF5,
    KEY_ALT_UP = 0xFFEA,
    KEY_SHIFT_DOWN = 0xFFF6,
    KEY_ALT_DOWN = 0xFFEB,
    KEY_SHIFT_LEFT = 0xFFF0,
    KEY_ALT_LEFT = 0xFFE9,
    KEY_SHIFT_CTRL_LEFT = 0xFFED,
    KEY_SHIFT_RIGHT = 0xFFEF,
    KEY_ALT_RIGHT = 0xFFE8,
    KEY_SHIFT_CTRL_RIGHT = 0xFFEC,
    KEY_QUIT = 0x11,
    KEY_FORCE_QUIT = 0xFFE6,
    KEY_ENTER = 0x0A,
    KEY_BACKSPACE = 0x08,
    KEY_DELETE = 0x7F,
    KEY_DEL_WORD = 0xFFF7,
    KEY_DEL_LINE = 0xFFE7,
    KEY_WRITE = 0x13,
    KEY_TAB = 0x09,
    KEY_BACKTAB = 0x15,
    KEY_FIND = 0x06,
    KEY_GOTO = 0x07,
    KEY_MOUSE = 0x1B,
    KEY_SCRLUP = 0x1C,
    KEY_SCRLDN = 0x1D,
    KEY_FIND_AGAIN = 0x0E,
    KEY_REDRAW = 0x05,
    KEY_UNDO = 0x1A,
    KEY_REDO = 0xFFEE,
    KEY_CUT = 0x18,
    KEY_PASTE = 0x16,
    KEY_COPY = 0x04,
    KEY_FIRST = 0x14,
    KEY_LAST = 0x02,
    KEY_REPLC = 0x12,
    KEY_TOGGLE = 0x01,
    KEY_GET = 0x0F,
    KEY_MARK = 0x0C,
    KEY_NEXT = 0x17,
    KEY_PREV = 0xFFE5,
    KEY_COMMENT = 0xFFFC,
    KEY_MATCH = 0xFFFD,
    KEY_INDENT = 0xFFFE,
    KEY_DEDENT = 0xFFFF,
    KEY_PLACE = 0xFFE4,
    KEY_NEXT_PLACE = 0xFFE3,
    KEY_PREV_PLACE = 0xFFE2,
    KEY_UNDO_PREV = 0xFFE1,
    KEY_UNDO_NEXT = 0xFFE0,
    KEY_UNDO_YANK = 0xFFDF,
};

typedef struct {
    const uint8_t *seq;
    size_t len;
    Key key;
} KeyMapEntry;

static const uint8_t seq_up[] = {0x1b, '[', 'A'};
static const uint8_t seq_s_up[] = {0x1b, '[', '1', ';', '2', 'A'};
static const uint8_t seq_a_up[] = {0x1b, '[', '1', ';', '3', 'A'};
static const uint8_t seq_down[] = {0x1b, '[', 'B'};
static const uint8_t seq_s_down[] = {0x1b, '[', '1', ';', '2', 'B'};
static const uint8_t seq_a_down[] = {0x1b, '[', '1', ';', '3', 'B'};
static const uint8_t seq_left[] = {0x1b, '[', 'D'};
static const uint8_t seq_s_left[] = {0x1b, '[', '1', ';', '2', 'D'};
static const uint8_t seq_sc_left[] = {0x1b, '[', '1', ';', '6', 'D'};
static const uint8_t seq_a_left[] = {0x1b, '[', '1', ';', '3', 'D'};
static const uint8_t seq_right[] = {0x1b, '[', 'C'};
static const uint8_t seq_s_right[] = {0x1b, '[', '1', ';', '2', 'C'};
static const uint8_t seq_sc_right[] = {0x1b, '[', '1', ';', '6', 'C'};
static const uint8_t seq_a_right[] = {0x1b, '[', '1', ';', '3', 'C'};
static const uint8_t seq_home1[] = {0x1b, '[', 'H'};
static const uint8_t seq_home2[] = {0x1b, 'O', 'H'};
static const uint8_t seq_home3[] = {0x1b, '[', '1', '~'};
static const uint8_t seq_end1[] = {0x1b, '[', 'F'};
static const uint8_t seq_end2[] = {0x1b, 'O', 'F'};
static const uint8_t seq_end3[] = {0x1b, '[', '4', '~'};
static const uint8_t seq_pgup[] = {0x1b, '[', '5', '~'};
static const uint8_t seq_pgdn[] = {0x1b, '[', '6', '~'};
static const uint8_t seq_prev[] = {0x1b, '[', '5', ';', '5', '~'};
static const uint8_t seq_next[] = {0x1b, '[', '6', ';', '5', '~'};
static const uint8_t seq_word_left[] = {0x1b, '[', '1', ';', '5', 'D'};
static const uint8_t seq_word_right[] = {0x1b, '[', '1', ';', '5', 'C'};
static const uint8_t seq_del[] = {0x1b, '[', '3', '~'};
static const uint8_t seq_backtab[] = {0x1b, '[', 'Z'};
static const uint8_t seq_scrlup[] = {0x1b, '[', '1', ';', '5', 'A'};
static const uint8_t seq_scrldn[] = {0x1b, '[', '1', ';', '5', 'B'};
static const uint8_t seq_first[] = {0x1b, '[', '1', ';', '5', 'H'};
static const uint8_t seq_last[] = {0x1b, '[', '1', ';', '5', 'F'};
static const uint8_t seq_del_word[] = {0x1b, '[', '3', ';', '5', '~'};
static const uint8_t seq_del_line[] = {0x1b, '[', '3', ';', '2', '~'};
static const uint8_t seq_mouse[] = {0x1b, '[', 'M'};
static const uint8_t seq_place[] = {0x1b, '[', '2', ';', '3', '~'};
static const uint8_t seq_prev_place[] = {0x1b, '[', '5', ';', '3', '~'};
static const uint8_t seq_next_place[] = {0x1b, '[', '6', ';', '3', '~'};
static const uint8_t seq_undo_prev[] = {0x1b, '[', '1', ';', '3', 'H'};
static const uint8_t seq_undo_next[] = {0x1b, '[', '1', ';', '3', 'F'};

static const KeyMapEntry KEYMAP[] = {
    {seq_up, sizeof(seq_up), KEY_UP},
    {seq_s_up, sizeof(seq_s_up), KEY_SHIFT_UP},
    {seq_a_up, sizeof(seq_a_up), KEY_ALT_UP},
    {seq_down, sizeof(seq_down), KEY_DOWN},
    {seq_s_down, sizeof(seq_s_down), KEY_SHIFT_DOWN},
    {seq_a_down, sizeof(seq_a_down), KEY_ALT_DOWN},
    {seq_left, sizeof(seq_left), KEY_LEFT},
    {seq_s_left, sizeof(seq_s_left), KEY_SHIFT_LEFT},
    {seq_sc_left, sizeof(seq_sc_left), KEY_SHIFT_CTRL_LEFT},
    {seq_a_left, sizeof(seq_a_left), KEY_ALT_LEFT},
    {seq_right, sizeof(seq_right), KEY_RIGHT},
    {seq_s_right, sizeof(seq_s_right), KEY_SHIFT_RIGHT},
    {seq_sc_right, sizeof(seq_sc_right), KEY_SHIFT_CTRL_RIGHT},
    {seq_a_right, sizeof(seq_a_right), KEY_ALT_RIGHT},
    {seq_home1, sizeof(seq_home1), KEY_HOME},
    {seq_home2, sizeof(seq_home2), KEY_HOME},
    {seq_home3, sizeof(seq_home3), KEY_HOME},
    {seq_end1, sizeof(seq_end1), KEY_END},
    {seq_end2, sizeof(seq_end2), KEY_END},
    {seq_end3, sizeof(seq_end3), KEY_END},
    {seq_pgup, sizeof(seq_pgup), KEY_PGUP},
    {seq_pgdn, sizeof(seq_pgdn), KEY_PGDN},
    {seq_prev, sizeof(seq_prev), KEY_PREV},
    {seq_next, sizeof(seq_next), KEY_NEXT},
    {seq_word_left, sizeof(seq_word_left), KEY_WORD_LEFT},
    {seq_word_right, sizeof(seq_word_right), KEY_WORD_RIGHT},
    {(const uint8_t *)"\x03", 1, KEY_COPY},
    {(const uint8_t *)"\r", 1, KEY_ENTER},
    {(const uint8_t *)"\x7f", 1, KEY_BACKSPACE},
    {seq_del, sizeof(seq_del), KEY_DELETE},
    {seq_backtab, sizeof(seq_backtab), KEY_BACKTAB},
    {(const uint8_t *)"\x19", 1, KEY_REDO},
    {(const uint8_t *)"\x08", 1, KEY_REPLC},
    {(const uint8_t *)"\x12", 1, KEY_REPLC},
    {(const uint8_t *)"\x11", 1, KEY_QUIT},
    {(const uint8_t *)"\x1b", 1, KEY_QUIT},
    {(const uint8_t *)"\n", 1, KEY_ENTER},
    {(const uint8_t *)"\x13", 1, KEY_WRITE},
    {(const uint8_t *)"\x06", 1, KEY_FIND},
    {(const uint8_t *)"\x0e", 1, KEY_FIND_AGAIN},
    {(const uint8_t *)"\x07", 1, KEY_GOTO},
    {(const uint8_t *)"\x05", 1, KEY_REDRAW},
    {(const uint8_t *)"\x1a", 1, KEY_UNDO},
    {(const uint8_t *)"\x09", 1, KEY_TAB},
    {(const uint8_t *)"\x15", 1, KEY_BACKTAB},
    {(const uint8_t *)"\x18", 1, KEY_CUT},
    {(const uint8_t *)"\x16", 1, KEY_PASTE},
    {(const uint8_t *)"\x04", 1, KEY_UNDO_YANK},
    {(const uint8_t *)"\x0c", 1, KEY_MARK},
    {(const uint8_t *)"\x00", 1, KEY_MARK},
    {(const uint8_t *)"\x14", 1, KEY_FIRST},
    {(const uint8_t *)"\x02", 1, KEY_LAST},
    {(const uint8_t *)"\x01", 1, KEY_TOGGLE},
    {(const uint8_t *)"\x17", 1, KEY_NEXT},
    {(const uint8_t *)"\x0f", 1, KEY_GET},
    {(const uint8_t *)"\x10", 1, KEY_COMMENT},
    {(const uint8_t *)"\x1f", 1, KEY_COMMENT},
    {seq_scrlup, sizeof(seq_scrlup), KEY_SCRLUP},
    {seq_scrldn, sizeof(seq_scrldn), KEY_SCRLDN},
    {seq_first, sizeof(seq_first), KEY_FIRST},
    {seq_last, sizeof(seq_last), KEY_LAST},
    {seq_del_word, sizeof(seq_del_word), KEY_DEL_WORD},
    {seq_del_line, sizeof(seq_del_line), KEY_DEL_LINE},
    {(const uint8_t *)"\x0b", 1, KEY_MATCH},
    {seq_mouse, sizeof(seq_mouse), KEY_MOUSE},
    {seq_place, sizeof(seq_place), KEY_PLACE},
    {seq_prev_place, sizeof(seq_prev_place), KEY_PREV_PLACE},
    {seq_next_place, sizeof(seq_next_place), KEY_NEXT_PLACE},
    {seq_undo_prev, sizeof(seq_undo_prev), KEY_UNDO_PREV},
    {seq_undo_next, sizeof(seq_undo_next), KEY_UNDO_NEXT},
};

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} ByteVec;

typedef struct {
    ByteVec *data;
    size_t len;
    size_t cap;
} LineVec;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Str;

static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }
static size_t max_size(size_t a, size_t b) { return a > b ? a : b; }

static void bvec_init(ByteVec *v) { v->data = NULL; v->len = 0; v->cap = 0; }
static void bvec_free(ByteVec *v) { free(v->data); v->data = NULL; v->len = 0; v->cap = 0; }
static bool bvec_reserve(ByteVec *v, size_t cap) {
    if (cap <= v->cap) {
        return true;
    }
    size_t next = v->cap ? v->cap * 2 : 32;
    if (next < cap) {
        next = cap;
    }
    uint8_t *tmp = (uint8_t *)realloc(v->data, next);
    if (!tmp) {
        return false;
    }
    v->data = tmp;
    v->cap = next;
    return true;
}
static bool bvec_push(ByteVec *v, uint8_t b) {
    if (!bvec_reserve(v, v->len + 1)) {
        return false;
    }
    v->data[v->len++] = b;
    return true;
}
static bool bvec_extend(ByteVec *v, const uint8_t *data, size_t len) {
    if (!bvec_reserve(v, v->len + len)) {
        return false;
    }
    memcpy(v->data + v->len, data, len);
    v->len += len;
    return true;
}
static ByteVec bvec_clone(const ByteVec *src) {
    ByteVec out;
    bvec_init(&out);
    if (src->len == 0) {
        return out;
    }
    if (!bvec_reserve(&out, src->len)) {
        return out;
    }
    memcpy(out.data, src->data, src->len);
    out.len = src->len;
    return out;
}
static void bvec_erase(ByteVec *v, size_t start, size_t end) {
    if (start >= end || end > v->len) {
        return;
    }
    memmove(v->data + start, v->data + end, v->len - end);
    v->len -= end - start;
}
static void bvec_insert(ByteVec *v, size_t pos, const uint8_t *data, size_t len) {
    if (pos > v->len) {
        pos = v->len;
    }
    if (!bvec_reserve(v, v->len + len)) {
        return;
    }
    memmove(v->data + pos + len, v->data + pos, v->len - pos);
    memcpy(v->data + pos, data, len);
    v->len += len;
}

static void linevec_init(LineVec *v) { v->data = NULL; v->len = 0; v->cap = 0; }
static void linevec_free(LineVec *v) {
    if (!v) {
        return;
    }
    for (size_t i = 0; i < v->len; ++i) {
        bvec_free(&v->data[i]);
    }
    free(v->data);
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
}
static bool linevec_reserve(LineVec *v, size_t cap) {
    if (cap <= v->cap) {
        return true;
    }
    size_t next = v->cap ? v->cap * 2 : 16;
    if (next < cap) {
        next = cap;
    }
    ByteVec *tmp = (ByteVec *)realloc(v->data, next * sizeof(*tmp));
    if (!tmp) {
        return false;
    }
    v->data = tmp;
    v->cap = next;
    return true;
}
static bool linevec_push(LineVec *v, const ByteVec *line) {
    if (!linevec_reserve(v, v->len + 1)) {
        return false;
    }
    v->data[v->len++] = bvec_clone(line);
    return true;
}
static bool linevec_push_empty(LineVec *v) {
    if (!linevec_reserve(v, v->len + 1)) {
        return false;
    }
    bvec_init(&v->data[v->len]);
    v->len += 1;
    return true;
}
static void linevec_insert(LineVec *v, size_t pos, const ByteVec *line) {
    if (pos > v->len) {
        pos = v->len;
    }
    if (!linevec_reserve(v, v->len + 1)) {
        return;
    }
    memmove(&v->data[pos + 1], &v->data[pos], (v->len - pos) * sizeof(*v->data));
    v->data[pos] = bvec_clone(line);
    v->len += 1;
}
static void linevec_remove(LineVec *v, size_t pos) {
    if (pos >= v->len) {
        return;
    }
    bvec_free(&v->data[pos]);
    memmove(&v->data[pos], &v->data[pos + 1], (v->len - pos - 1) * sizeof(*v->data));
    v->len -= 1;
}
static LineVec linevec_clone_slice(const LineVec *v, size_t start, size_t end) {
    LineVec out;
    linevec_init(&out);
    if (start >= end || start >= v->len) {
        return out;
    }
    if (end > v->len) {
        end = v->len;
    }
    for (size_t i = start; i < end; ++i) {
        linevec_push(&out, &v->data[i]);
    }
    return out;
}

static void str_init(Str *s) { s->data = NULL; s->len = 0; s->cap = 0; }
static void str_free(Str *s) { free(s->data); s->data = NULL; s->len = 0; s->cap = 0; }
static bool str_reserve(Str *s, size_t cap) {
    if (cap <= s->cap) {
        return true;
    }
    size_t next = s->cap ? s->cap * 2 : 32;
    if (next < cap) {
        next = cap;
    }
    char *tmp = (char *)realloc(s->data, next);
    if (!tmp) {
        return false;
    }
    s->data = tmp;
    s->cap = next;
    return true;
}
static void str_clear(Str *s) { s->len = 0; if (s->data) s->data[0] = '\0'; }
static void str_push_char(Str *s, char c) {
    if (!str_reserve(s, s->len + 2)) {
        return;
    }
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}
static void str_push_cstr(Str *s, const char *cstr) {
    size_t n = strlen(cstr);
    if (!str_reserve(s, s->len + n + 1)) {
        return;
    }
    memcpy(s->data + s->len, cstr, n);
    s->len += n;
    s->data[s->len] = '\0';
}
static void str_set_cstr(Str *s, const char *cstr) {
    str_clear(s);
    str_push_cstr(s, cstr);
}
static void str_set_bytes(Str *s, const uint8_t *data, size_t len) {
    str_clear(s);
    if (!str_reserve(s, len + 1)) {
        return;
    }
    memcpy(s->data, data, len);
    s->len = len;
    s->data[s->len] = '\0';
}
static void str_push_usize(Str *s, size_t value) {
    char buf[24];
    size_t i = sizeof(buf);
    if (value == 0) {
        str_push_char(s, '0');
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    if (i < sizeof(buf)) {
        if (!str_reserve(s, s->len + (sizeof(buf) - i) + 1)) {
            return;
        }
        memcpy(s->data + s->len, &buf[i], sizeof(buf) - i);
        s->len += sizeof(buf) - i;
        s->data[s->len] = '\0';
    }
}

static void push_usize_bytes(ByteVec *v, size_t value) {
    uint8_t buf[24];
    size_t i = sizeof(buf);
    if (value == 0) {
        bvec_push(v, '0');
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = (uint8_t)('0' + (value % 10));
        value /= 10;
    }
    bvec_extend(v, &buf[i], sizeof(buf) - i);
}

static const uint8_t *trim_ascii_space(const uint8_t *data, size_t len, size_t *out_len) {
    size_t start = 0;
    size_t end = len;
    while (start < end) {
        uint8_t b = data[start];
        if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
            start++;
        } else {
            break;
        }
    }
    while (end > start) {
        uint8_t b = data[end - 1];
        if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
            end--;
        } else {
            break;
        }
    }
    *out_len = end - start;
    return data + start;
}

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} InputBuf;

typedef struct {
    Key key;
    int data_kind; /* 0 none, 1 char, 2 mouse */
    uint8_t ch[4];
    size_t ch_len;
    size_t mouse_x;
    size_t mouse_y;
    uint8_t mouse_code;
} InputEvent;

static void inputbuf_init(InputBuf *in) { in->buf = NULL; in->len = 0; in->cap = 0; }
static void inputbuf_free(InputBuf *in) { free(in->buf); in->buf = NULL; in->len = 0; in->cap = 0; }
static void inputbuf_reserve(InputBuf *in, size_t cap) {
    if (cap <= in->cap) {
        return;
    }
    size_t next = in->cap ? in->cap * 2 : 64;
    if (next < cap) {
        next = cap;
    }
    uint8_t *tmp = (uint8_t *)realloc(in->buf, next);
    if (!tmp) {
        return;
    }
    in->buf = tmp;
    in->cap = next;
}
static void inputbuf_fill(InputBuf *in) {
    uint8_t tmp[64];
    ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
    if (n > 0) {
        inputbuf_reserve(in, in->len + (size_t)n);
        memcpy(in->buf + in->len, tmp, (size_t)n);
        in->len += (size_t)n;
    }
}
static void inputbuf_ensure(InputBuf *in) {
    if (in->len == 0) {
        inputbuf_fill(in);
    }
}
static uint8_t inputbuf_read_byte(InputBuf *in) {
    inputbuf_ensure(in);
    if (in->len == 0) {
        return 0;
    }
    uint8_t b = in->buf[0];
    memmove(in->buf, in->buf + 1, in->len - 1);
    in->len -= 1;
    return b;
}
static size_t inputbuf_read_bytes(InputBuf *in, uint8_t *out, size_t count) {
    size_t got = 0;
    while (got < count) {
        inputbuf_ensure(in);
        if (in->len == 0) {
            break;
        }
        out[got++] = inputbuf_read_byte(in);
    }
    return got;
}

static Key key_for_seq(const uint8_t *seq, size_t len) {
    for (size_t i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); ++i) {
        if (KEYMAP[i].len == len && memcmp(KEYMAP[i].seq, seq, len) == 0) {
            return KEYMAP[i].key;
        }
    }
    return 0;
}

static bool is_prefix(const uint8_t *seq, size_t len) {
    for (size_t i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); ++i) {
        if (KEYMAP[i].len >= len && memcmp(KEYMAP[i].seq, seq, len) == 0) {
            return true;
        }
    }
    return false;
}

static size_t key_max_len(void) {
    size_t max_len = 0;
    for (size_t i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); ++i) {
        if (KEYMAP[i].len > max_len) {
            max_len = KEYMAP[i].len;
        }
    }
    return max_len;
}

static void read_utf8_char(InputBuf *in, uint8_t first, uint8_t *out, size_t *out_len) {
    *out_len = 0;
    out[(*out_len)++] = first;
    size_t needed = 0;
    if ((first & 0x80) == 0) {
        needed = 0;
    } else if ((first & 0xE0) == 0xC0) {
        needed = 1;
    } else if ((first & 0xF0) == 0xE0) {
        needed = 2;
    } else if ((first & 0xF8) == 0xF0) {
        needed = 3;
    }
    if (needed > 0) {
        uint8_t tmp[4] = {0};
        size_t got = inputbuf_read_bytes(in, tmp, needed);
        for (size_t i = 0; i < got; ++i) {
            out[(*out_len)++] = tmp[i];
        }
    }
}

static InputEvent input_get(InputBuf *in, size_t key_max) {
    InputEvent evt;
    memset(&evt, 0, sizeof(evt));
    for (;;) {
        inputbuf_ensure(in);
        if (in->len == 0) {
            continue;
        }
        uint8_t first = inputbuf_read_byte(in);
        if (first == 0x1b) {
            uint8_t seq[16];
            size_t seq_len = 0;
            seq[seq_len++] = first;
            for (;;) {
                if (seq_len == 2 && seq[0] == 0x1b && seq[1] == 0x1b) {
                    seq_len = 1;
                    seq[0] = 0x1b;
                    break;
                }
                Key key = key_for_seq(seq, seq_len);
                if (key != 0) {
                    if (key == KEY_MOUSE) {
                        uint8_t raw[3] = {0};
                        if (inputbuf_read_bytes(in, raw, 3) == 3) {
                            uint8_t mouse_fct = raw[0];
                            size_t mouse_x = (size_t)(raw[1] - 33);
                            size_t mouse_y = (size_t)(raw[2] - 33);
                            if (mouse_fct == 0x61) {
                                evt.key = KEY_SCRLDN;
                                evt.data_kind = 1;
                                evt.ch[0] = 3;
                                evt.ch_len = 1;
                                return evt;
                            }
                            if (mouse_fct == 0x60) {
                                evt.key = KEY_SCRLUP;
                                evt.data_kind = 1;
                                evt.ch[0] = 3;
                                evt.ch_len = 1;
                                return evt;
                            }
                            evt.key = KEY_MOUSE;
                            evt.data_kind = 2;
                            evt.mouse_x = mouse_x;
                            evt.mouse_y = mouse_y;
                            evt.mouse_code = mouse_fct;
                            return evt;
                        }
                    } else {
                        evt.key = key;
                        return evt;
                    }
                }
                if (seq_len >= key_max) {
                    break;
                }
                if (!is_prefix(seq, seq_len)) {
                    break;
                }
                if (in->len == 0) {
                    inputbuf_fill(in);
                    if (in->len == 0) {
                        break;
                    }
                }
                uint8_t next = inputbuf_read_byte(in);
                if (seq_len == 1 && ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) && next != 'O') {
                    seq[0] = (uint8_t)(next & 0x1f);
                    seq_len = 1;
                    break;
                }
                if (seq_len < sizeof(seq)) {
                    seq[seq_len++] = next;
                }
                uint8_t last = seq[seq_len - 1];
                if (last == '~' || (((last >= 'A' && last <= 'Z') || (last >= 'a' && last <= 'z')) && seq_len > 2)) {
                    break;
                }
            }
            Key mapped = key_for_seq(seq, seq_len);
            if (mapped != 0) {
                evt.key = mapped;
                return evt;
            }
        } else if (first >= 0x20) {
            evt.key = KEY_NONE;
            evt.data_kind = 1;
            read_utf8_char(in, first, evt.ch, &evt.ch_len);
            return evt;
        } else {
            Key mapped = key_for_seq(&first, 1);
            if (mapped != 0) {
                evt.key = mapped;
                return evt;
            }
        }
    }
}

typedef struct {
    uint8_t flag;
    ByteVec text;
} ScreenLine;

typedef struct {
    size_t width;
    size_t height;
    ScreenLine *scrbuf;
    size_t scrbuf_len;
} Terminal;

static struct termios term_saved;
static bool term_saved_valid = false;

static void term_enable_raw(void) {
    if (tcgetattr(STDIN_FILENO, &term_saved) != 0) {
        return;
    }
    term_saved_valid = true;
    struct termios raw = term_saved;
    raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t) ~(OPOST);
    raw.c_cflag |= (tcflag_t) CS8;
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void term_restore(void) {
    if (!term_saved_valid) {
        return;
    }
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_saved);
    term_saved_valid = false;
}

static void term_write_bytes(const uint8_t *data, size_t len) {
    if (len == 0) {
        return;
    }
    (void)write(STDOUT_FILENO, data, len);
}

static void term_write_str(const char *s) {
    term_write_bytes((const uint8_t *)s, strlen(s));
}

static void term_goto(Terminal *t, size_t row, size_t col) {
    (void)t;
    ByteVec buf;
    bvec_init(&buf);
    bvec_extend(&buf, (const uint8_t *)"\x1b[", 2);
    push_usize_bytes(&buf, row + 1);
    bvec_push(&buf, ';');
    push_usize_bytes(&buf, col + 1);
    bvec_push(&buf, 'H');
    term_write_bytes(buf.data, buf.len);
    bvec_free(&buf);
}

static void term_clear_to_eol(void) { term_write_str("\x1b[0K"); }
static void term_cursor(bool on) { term_write_str(on ? "\x1b[?25h" : "\x1b[?25l"); }
static void term_hilite(uint8_t mode) {
    if (mode == 1) {
        term_write_str("\x1b[1;37;44m");
    } else if (mode == 2) {
        term_write_str("\x1b[43m");
    } else {
        term_write_str("\x1b[0m");
    }
}
static void term_mouse_reporting(bool on) { term_write_str(on ? "\x1b[?9h" : "\x1b[?9l"); }

static void term_scroll_region(size_t stop) {
    if (stop > 0) {
        ByteVec buf;
        bvec_init(&buf);
        bvec_extend(&buf, (const uint8_t *)"\x1b[1;", 4);
        push_usize_bytes(&buf, stop);
        bvec_push(&buf, 'r');
        term_write_bytes(buf.data, buf.len);
        bvec_free(&buf);
    } else {
        term_write_str("\x1b[r");
    }
}

static void term_scroll_up(Terminal *t, size_t scrolling) {
    if (scrolling == 0 || t->height == 0 || t->scrbuf_len == 0) {
        return;
    }
    if (scrolling > t->scrbuf_len) {
        scrolling = t->scrbuf_len;
    }
    memmove(&t->scrbuf[0], &t->scrbuf[scrolling], (t->scrbuf_len - scrolling) * sizeof(*t->scrbuf));
    for (size_t i = t->scrbuf_len - scrolling; i < t->scrbuf_len; ++i) {
        t->scrbuf[i].flag = 0xff;
        t->scrbuf[i].text.len = 0;
    }
    term_goto(t, 0, 0);
    for (size_t i = 0; i < scrolling; ++i) {
        term_write_str("\x1bM");
    }
}

static void term_scroll_down(Terminal *t, size_t scrolling) {
    if (scrolling == 0 || t->height == 0 || t->scrbuf_len == 0) {
        return;
    }
    if (scrolling > t->scrbuf_len) {
        scrolling = t->scrbuf_len;
    }
    memmove(&t->scrbuf[scrolling], &t->scrbuf[0], (t->scrbuf_len - scrolling) * sizeof(*t->scrbuf));
    for (size_t i = 0; i < scrolling; ++i) {
        t->scrbuf[i].flag = 0xff;
        t->scrbuf[i].text.len = 0;
    }
    term_goto(t, t->height - 1, 0);
    for (size_t i = 0; i < scrolling; ++i) {
        term_write_str("\n");
    }
}

static void term_get_screen_size(InputBuf *in, size_t *out_rows, size_t *out_cols) {
    term_write_str("\x1b[999;999H\x1b[6n");
    uint8_t buf[64];
    size_t len = 0;
    while (len < sizeof(buf)) {
        uint8_t b = inputbuf_read_byte(in);
        if (b == 'R') {
            break;
        }
        buf[len++] = b;
    }
    size_t row = 24;
    size_t col = 80;
    if (len >= 2 && buf[0] == 0x1b && buf[1] == '[') {
        size_t nums[3] = {0};
        size_t ncount = 0;
        size_t current = 0;
        bool has = false;
        for (size_t i = 2; i < len; ++i) {
            uint8_t b = buf[i];
            if (b >= '0' && b <= '9') {
                current = current * 10 + (b - '0');
                has = true;
            } else if (b == ';') {
                if (ncount < 3) {
                    nums[ncount++] = current;
                }
                current = 0;
                has = false;
            }
        }
        if (has && ncount < 3) {
            nums[ncount++] = current;
        }
        if (ncount >= 2) {
            row = nums[0];
            col = nums[1];
        }
    }
    *out_rows = row;
    *out_cols = col;
}

static void term_init(Terminal *t, InputBuf *in) {
    term_enable_raw();
    size_t rows = 24;
    size_t cols = 80;
    term_get_screen_size(in, &rows, &cols);
    t->width = cols;
    t->height = rows > 0 ? rows - 1 : 0;
    t->scrbuf_len = t->height;
    t->scrbuf = (ScreenLine *)calloc(t->scrbuf_len, sizeof(*t->scrbuf));
    for (size_t i = 0; i < t->scrbuf_len; ++i) {
        t->scrbuf[i].flag = 0xff;
        bvec_init(&t->scrbuf[i].text);
    }
    term_scroll_region(t->height);
    term_mouse_reporting(false);
}

static void term_redraw(Terminal *t, InputBuf *in) {
    term_cursor(false);
    size_t rows = 24;
    size_t cols = 80;
    term_get_screen_size(in, &rows, &cols);
    t->width = cols;
    t->height = rows > 0 ? rows - 1 : 0;
    for (size_t i = 0; i < t->scrbuf_len; ++i) {
        bvec_free(&t->scrbuf[i].text);
    }
    free(t->scrbuf);
    t->scrbuf_len = t->height;
    t->scrbuf = (ScreenLine *)calloc(t->scrbuf_len, sizeof(*t->scrbuf));
    for (size_t i = 0; i < t->scrbuf_len; ++i) {
        t->scrbuf[i].flag = 0xff;
        bvec_init(&t->scrbuf[i].text);
    }
    term_scroll_region(t->height);
    term_mouse_reporting(false);
}

typedef struct {
    uint32_t editor_id;
    size_t line;
} Place;

typedef struct {
    LineVec yank_buffer;
    Str find_pattern;
    Str replc_pattern;
    ByteVec comment_char;
    ByteVec word_char;
    ByteVec file_char;
    bool case_sensitive;
    bool autoindent;
    size_t match_span;
    Place *place_list;
    size_t place_len;
    size_t place_cap;
    size_t place_index;
    size_t max_places;
} Shared;

static void shared_init(Shared *s) {
    linevec_init(&s->yank_buffer);
    str_init(&s->find_pattern);
    str_init(&s->replc_pattern);
    bvec_init(&s->comment_char);
    bvec_extend(&s->comment_char, (const uint8_t *)"# ", 2);
    bvec_init(&s->word_char);
    bvec_extend(&s->word_char, (const uint8_t *)"_\\", 2);
    bvec_init(&s->file_char);
    bvec_extend(&s->file_char, (const uint8_t *)"_.-", 3);
    s->case_sensitive = false;
    s->autoindent = true;
    s->match_span = 50;
    s->place_list = NULL;
    s->place_len = 0;
    s->place_cap = 0;
    s->place_index = 0;
    s->max_places = 20;
}

static void shared_free(Shared *s) {
    linevec_free(&s->yank_buffer);
    str_free(&s->find_pattern);
    str_free(&s->replc_pattern);
    bvec_free(&s->comment_char);
    bvec_free(&s->word_char);
    bvec_free(&s->file_char);
    free(s->place_list);
}

static void shared_set_comment_char(Shared *s, const char *value) {
    bvec_free(&s->comment_char);
    bvec_init(&s->comment_char);
    if (value && value[0]) {
        bvec_extend(&s->comment_char, (const uint8_t *)value, strlen(value));
    }
}

typedef struct {
    size_t line;
    ptrdiff_t span;
    LineVec text;
    Key key;
    size_t col;
    bool chain;
} UndoItem;

typedef struct {
    UndoItem *data;
    size_t len;
    size_t cap;
} UndoVec;

static void undo_vec_init(UndoVec *v) { v->data = NULL; v->len = 0; v->cap = 0; }
static void undo_item_free(UndoItem *item) { linevec_free(&item->text); }
static void undo_vec_free(UndoVec *v) {
    for (size_t i = 0; i < v->len; ++i) {
        undo_item_free(&v->data[i]);
    }
    free(v->data);
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
}
static bool undo_vec_reserve(UndoVec *v, size_t cap) {
    if (cap <= v->cap) {
        return true;
    }
    size_t next = v->cap ? v->cap * 2 : 16;
    if (next < cap) {
        next = cap;
    }
    UndoItem *tmp = (UndoItem *)realloc(v->data, next * sizeof(*tmp));
    if (!tmp) {
        return false;
    }
    v->data = tmp;
    v->cap = next;
    return true;
}
static void undo_vec_push(UndoVec *v, const UndoItem *item) {
    if (!undo_vec_reserve(v, v->len + 1)) {
        return;
    }
    v->data[v->len++] = *item;
}

typedef struct {
    uint32_t id;
    size_t top_line;
    size_t cur_line;
    size_t row;
    size_t vcol;
    size_t col;
    size_t margin;
    size_t tab_size;
    bool changed;
    uint32_t hash;
    Str message;
    Str fname;
    LineVec content;
    UndoVec undo;
    size_t undo_limit;
    size_t undo_index;
    UndoVec redo;
    bool mark;
    size_t mark_line;
    size_t mark_col;
    int mark_flag;
    bool write_tabs;
    Str work_dir;
    bool is_dir;
    size_t key_max;
} Editor;

static uint32_t hash_buffer(const LineVec *lines) {
    uint32_t res = 0;
    for (size_t i = 0; i < lines->len; ++i) {
        uint32_t h = 0;
        const ByteVec *line = &lines->data[i];
        for (size_t j = 0; j < line->len; ++j) {
            h = h * 227u + (uint32_t)line->data[j] + 1u;
        }
        res = res * 227u + 1u;
        res ^= h;
    }
    return res & 0x3fffffff;
}

static void editor_init(Editor *e, uint32_t id, size_t tab_size, size_t undo_limit) {
    e->id = id;
    e->top_line = 0;
    e->cur_line = 0;
    e->row = 0;
    e->vcol = 0;
    e->col = 0;
    e->margin = 0;
    e->tab_size = tab_size;
    e->changed = false;
    e->hash = 0;
    str_init(&e->message);
    str_init(&e->fname);
    linevec_init(&e->content);
    linevec_push_empty(&e->content);
    undo_vec_init(&e->undo);
    e->undo_limit = undo_limit;
    e->undo_index = 0;
    undo_vec_init(&e->redo);
    e->mark = false;
    e->mark_line = 0;
    e->mark_col = 0;
    e->mark_flag = 0;
    e->write_tabs = false;
    str_init(&e->work_dir);
    e->is_dir = false;
    e->key_max = key_max_len();
}

static void editor_free(Editor *e) {
    str_free(&e->message);
    str_free(&e->fname);
    linevec_free(&e->content);
    undo_vec_free(&e->undo);
    undo_vec_free(&e->redo);
    str_free(&e->work_dir);
}

static void editor_status_line(Editor *e, Terminal *t) {
    Str msg;
    str_init(&msg);
    if (e->changed) {
        str_push_char(&msg, '*');
    }
    str_push_cstr(&msg, e->fname.data ? e->fname.data : "");
    if (t->width > 40) {
        str_push_cstr(&msg, " Row: ");
        str_push_usize(&msg, e->cur_line + 1);
        str_push_char(&msg, '/');
        str_push_usize(&msg, e->content.len);
        str_push_cstr(&msg, " Col: ");
        str_push_usize(&msg, e->vcol + 1);
        str_push_cstr(&msg, "  ");
        str_push_cstr(&msg, e->message.data ? e->message.data : "");
    } else {
        str_push_char(&msg, ' ');
        str_push_usize(&msg, e->cur_line + 1);
        str_push_char(&msg, ':');
        str_push_usize(&msg, e->vcol + 1);
        str_push_cstr(&msg, "  ");
        str_push_cstr(&msg, e->message.data ? e->message.data : "");
    }
    term_goto(t, t->height, 0);
    term_hilite(1);
    size_t cut = t->width > 0 ? min_size(msg.len, t->width - 1) : 0;
    term_write_bytes((const uint8_t *)msg.data, cut);
    term_clear_to_eol();
    term_hilite(0);
    str_free(&msg);
}

static size_t spaces(const ByteVec *line, size_t start) {
    size_t n = 0;
    for (size_t i = start; i < line->len; ++i) {
        if (line->data[i] == ' ') {
            n++;
        } else {
            break;
        }
    }
    return n;
}

static bool is_word_char(const ByteVec *set, uint8_t c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return true;
    }
    for (size_t i = 0; i < set->len; ++i) {
        if (set->data[i] == c) {
            return true;
        }
    }
    return false;
}

static ptrdiff_t skip_until(const ByteVec *line, ptrdiff_t start, const ByteVec *set, int dir) {
    ptrdiff_t pos = start;
    while (pos >= 0 && (size_t)pos < line->len) {
        if (is_word_char(set, line->data[pos])) {
            break;
        }
        pos += dir;
    }
    return pos;
}

static size_t skip_while(const ByteVec *line, size_t start, const ByteVec *set, int dir) {
    ptrdiff_t pos = (ptrdiff_t)start;
    while (pos >= 0 && (size_t)pos < line->len && is_word_char(set, line->data[pos])) {
        pos += dir;
    }
    if (dir < 0) {
        return pos > 0 ? (size_t)pos : 0;
    }
    return (size_t)pos;
}

static void editor_move_left(Editor *e) {
    if (e->col > 0) {
        e->col -= 1;
    }
}
static void editor_move_right(Editor *e, size_t len) {
    if (e->col < len) {
        e->col += 1;
    }
}
static void editor_move_up(Editor *e, Terminal *t) {
    if (e->cur_line > 0) {
        e->cur_line--;
        if (e->row > 0) {
            e->row--;
        } else if (e->top_line > 0) {
            e->top_line--;
        }
        if (e->col > e->content.data[e->cur_line].len) {
            e->col = e->content.data[e->cur_line].len;
        }
    } else {
        e->col = 0;
    }
    (void)t;
}
static void editor_move_down(Editor *e, Terminal *t) {
    if (e->cur_line + 1 < e->content.len) {
        e->cur_line++;
        if (e->row + 1 < t->height) {
            e->row++;
        } else {
            e->top_line++;
        }
        if (e->col > e->content.data[e->cur_line].len) {
            e->col = e->content.data[e->cur_line].len;
        }
    } else if (e->cur_line + 1 == e->content.len) {
        e->col = e->content.data[e->cur_line].len;
    }
}

static void editor_getsymbol(Editor *e, const ByteVec *zap, ByteVec *out) {
    bvec_init(out);
    if (!zap || e->cur_line >= e->content.len) {
        return;
    }
    const ByteVec *line = &e->content.data[e->cur_line];
    if (e->col >= line->len) {
        return;
    }
    if (!is_word_char(zap, line->data[e->col])) {
        return;
    }
    ptrdiff_t start = (ptrdiff_t)e->col;
    while (start >= 0 && is_word_char(zap, line->data[start])) {
        start -= 1;
    }
    size_t end = e->col;
    while (end < line->len && is_word_char(zap, line->data[end])) {
        end += 1;
    }
    size_t s = (size_t)(start + 1);
    if (end > s) {
        bvec_extend(out, line->data + s, end - s);
    }
}

static size_t line_edit_max_len(const Terminal *t, size_t prompt_len) {
    if (t->width == 0) {
        return 0;
    }
    if (prompt_len + 2 >= t->width) {
        return 0;
    }
    return t->width - prompt_len - 2;
}

static void line_edit_draw(const Terminal *t, const char *prompt, const ByteVec *res, size_t pos) {
    size_t prompt_len = strlen(prompt);
    term_goto((Terminal *)t, t->height, 0);
    term_hilite(1);
    size_t max_width = t->width > 0 ? t->width - 1 : 0;
    size_t used = 0;
    if (prompt_len > 0 && max_width > 0) {
        size_t n = min_size(prompt_len, max_width);
        term_write_bytes((const uint8_t *)prompt, n);
        used += n;
    }
    if (res->len > 0 && used < max_width) {
        size_t n = min_size(res->len, max_width - used);
        term_write_bytes(res->data, n);
        used += n;
    }
    term_clear_to_eol();
    size_t cur_col = prompt_len + pos;
    if (t->width > 0) {
        cur_col = min_size(cur_col, t->width - 1);
    }
    term_goto((Terminal *)t, t->height, cur_col);
    term_hilite(0);
}

static bool editor_line_edit(Editor *e, Terminal *t, InputBuf *in, const char *prompt, const char *def,
                             const ByteVec *zap, Str *out) {
    ByteVec res;
    bvec_init(&res);
    if (def && def[0]) {
        bvec_extend(&res, (const uint8_t *)def, strlen(def));
    }
    size_t pos = res.len;
    bool del_all = true;
    size_t max_len = line_edit_max_len(t, strlen(prompt));
    for (;;) {
        if (res.len > max_len) {
            res.len = max_len;
            if (pos > res.len) {
                pos = res.len;
            }
        }
        line_edit_draw(t, prompt, &res, pos);
        InputEvent evt = input_get(in, e->key_max);
        Key key = evt.key;
        if (key == KEY_NONE && evt.data_kind == 1 && evt.ch_len > 0) {
            if (res.len + evt.ch_len <= max_len) {
                bvec_insert(&res, pos, evt.ch, evt.ch_len);
                pos += evt.ch_len;
            }
        } else if (key == KEY_ENTER || key == KEY_TAB) {
            str_set_bytes(out, res.data, res.len);
            bvec_free(&res);
            return true;
        } else if (key == KEY_QUIT || key == KEY_COPY) {
            bvec_free(&res);
            return false;
        } else if (key == KEY_LEFT) {
            if (pos > 0) {
                pos -= 1;
            }
        } else if (key == KEY_RIGHT) {
            if (pos < res.len) {
                pos += 1;
            }
        } else if (key == KEY_HOME) {
            pos = 0;
        } else if (key == KEY_END) {
            pos = res.len;
        } else if (key == KEY_DELETE) {
            if (del_all) {
                res.len = 0;
                pos = 0;
            } else if (pos < res.len) {
                bvec_erase(&res, pos, pos + 1);
            }
        } else if (key == KEY_BACKSPACE) {
            if (pos > 0) {
                bvec_erase(&res, pos - 1, pos);
                pos -= 1;
            }
        } else if (key == KEY_PASTE) {
            ByteVec sym;
            editor_getsymbol(e, zap, &sym);
            if (sym.len > 0 && res.len < max_len) {
                size_t n = min_size(sym.len, max_len - res.len);
                bvec_extend(&res, sym.data, n);
            }
            bvec_free(&sym);
        }
        del_all = false;
    }
}

static void editor_set_mark(Editor *e, int flag) {
    if (!e->mark) {
        e->mark = true;
        e->mark_line = e->cur_line;
        e->mark_col = e->col;
    }
    if (e->mark_flag < flag) {
        e->mark_flag = flag;
    }
}
static void editor_check_mark(Editor *e) {
    if (e->mark) {
        e->mark_flag--;
        if (e->mark_flag <= 0) {
            e->mark = false;
            e->mark_flag = 0;
        }
    }
}
static void editor_clear_mark(Editor *e) {
    e->mark = false;
    e->mark_flag = 0;
}

static void editor_mark_range(Editor *e, size_t *sline, size_t *scol, size_t *eline, size_t *ecol) {
    size_t a_line = e->mark_line;
    size_t a_col = e->mark_col;
    size_t b_line = e->cur_line;
    size_t b_col = e->col;
    if (a_line > b_line || (a_line == b_line && a_col > b_col)) {
        size_t tmp;
        tmp = a_line; a_line = b_line; b_line = tmp;
        tmp = a_col; a_col = b_col; b_col = tmp;
    }
    *sline = a_line;
    *scol = a_col;
    *eline = b_line + 1;
    *ecol = b_col;
}

static void editor_yank_mark(Editor *e, Shared *shared) {
    size_t sline, scol, eline, ecol;
    editor_mark_range(e, &sline, &scol, &eline, &ecol);
    linevec_free(&shared->yank_buffer);
    shared->yank_buffer = linevec_clone_slice(&e->content, sline, eline);
    if (shared->yank_buffer.len > 0) {
        ByteVec *last = &shared->yank_buffer.data[shared->yank_buffer.len - 1];
        if (ecol < last->len) {
            last->len = ecol;
        }
        ByteVec *first = &shared->yank_buffer.data[0];
        if (scol < first->len) {
            bvec_erase(first, 0, scol);
        } else {
            first->len = 0;
        }
    }
}

static void editor_undo_add(Editor *e, size_t line, const LineVec *text, Key key, ptrdiff_t span, bool chain) {
    UndoItem item;
    item.line = line;
    item.span = span;
    item.text = linevec_clone_slice(text, 0, text->len);
    item.key = key;
    item.col = e->col;
    item.chain = chain;
    if (e->undo.len >= e->undo_limit) {
        undo_item_free(&e->undo.data[0]);
        memmove(&e->undo.data[0], &e->undo.data[1], (e->undo.len - 1) * sizeof(*e->undo.data));
        e->undo.len -= 1;
    }
    undo_vec_push(&e->undo, &item);
    undo_vec_free(&e->redo);
    undo_vec_init(&e->redo);
    e->undo_index = e->undo.len;
}

static void editor_delete_mark(Editor *e, Shared *shared, bool yank) {
    if (yank) {
        editor_yank_mark(e, shared);
    }
    size_t sline, scol, eline, ecol;
    editor_mark_range(e, &sline, &scol, &eline, &ecol);
    LineVec slice = linevec_clone_slice(&e->content, sline, eline);
    editor_undo_add(e, sline, &slice, KEY_NONE, 1, false);
    linevec_free(&slice);

    ByteVec tail;
    bvec_init(&tail);
    if (eline > 0 && eline - 1 < e->content.len) {
        const ByteVec *line = &e->content.data[eline - 1];
        if (ecol < line->len) {
            bvec_extend(&tail, line->data + ecol, line->len - ecol);
        }
    }
    if (sline < e->content.len) {
        ByteVec *line = &e->content.data[sline];
        if (scol < line->len) {
            line->len = scol;
        }
        bvec_extend(line, tail.data, tail.len);
    }
    if (sline + 1 < eline) {
        size_t count = eline - sline - 1;
        for (size_t i = 0; i < count; ++i) {
            linevec_remove(&e->content, sline + 1);
        }
    }
    if (e->content.len == 0) {
        linevec_push_empty(&e->content);
    }
    e->col = scol;
    e->cur_line = sline;
    editor_clear_mark(e);
    bvec_free(&tail);
}

static size_t find_subslice(const uint8_t *hay, size_t hay_len, const uint8_t *needle, size_t needle_len) {
    if (needle_len == 0 || hay_len < needle_len) {
        return (size_t)-1;
    }
    for (size_t i = 0; i + needle_len <= hay_len; ++i) {
        if (memcmp(hay + i, needle, needle_len) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static ssize_t find_in_line(const ByteVec *line, const char *pat, bool case_sensitive, size_t start) {
    size_t pat_len = strlen(pat);
    if (pat_len == 0 || start >= line->len) {
        return -1;
    }
    if (case_sensitive) {
        size_t pos = find_subslice(line->data + start, line->len - start, (const uint8_t *)pat, pat_len);
        return pos == (size_t)-1 ? -1 : (ssize_t)(pos + start);
    }
    for (size_t i = start; i + pat_len <= line->len; ++i) {
        size_t j = 0;
        for (; j < pat_len; ++j) {
            uint8_t a = line->data[i + j];
            uint8_t b = (uint8_t)pat[j];
            if (a >= 'A' && a <= 'Z') {
                a = (uint8_t)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (uint8_t)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (j == pat_len) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static bool editor_find_in_file(Editor *e, const char *pat, size_t start_col, size_t end_line, Shared *shared) {
    size_t line = e->cur_line;
    size_t col = start_col;
    size_t last = min_size(end_line, e->content.len);
    for (; line < last; ++line) {
        const ByteVec *l = &e->content.data[line];
        ssize_t pos = find_in_line(l, pat, shared->case_sensitive, col);
        if (pos >= 0) {
            e->cur_line = line;
            e->col = (size_t)pos;
            return true;
        }
        col = 0;
    }
    return false;
}

static void editor_display_window(Editor *e, Terminal *t) {
    if (e->content.len == 0) {
        linevec_push_empty(&e->content);
    }
    if (e->cur_line >= e->content.len) {
        e->cur_line = e->content.len - 1;
    }
    size_t line_len = e->content.data[e->cur_line].len;
    e->vcol = min_size(e->col, line_len);
    if (e->vcol >= t->width + e->margin) {
        e->margin = e->vcol - t->width + (t->width >> 2);
    } else if (e->vcol < e->margin) {
        e->margin = e->vcol >= (t->width >> 2) ? e->vcol - (t->width >> 2) : 0;
    }
    if (!(e->top_line <= e->cur_line && e->cur_line < e->top_line + t->height)) {
        e->top_line = e->cur_line >= e->row ? e->cur_line - e->row : 0;
    }
    e->row = e->cur_line - e->top_line;
    term_cursor(false);
    for (size_t row = 0; row < t->height; ++row) {
        size_t idx = e->top_line + row;
        ByteVec out;
        bvec_init(&out);
        if (idx < e->content.len) {
            const ByteVec *line = &e->content.data[idx];
            if (e->margin < line->len) {
                size_t len = min_size(line->len - e->margin, t->width);
                bvec_extend(&out, line->data + e->margin, len);
            }
        }
        ScreenLine *sl = &t->scrbuf[row];
        if (sl->flag != 0 || sl->text.len != out.len || (out.len && memcmp(sl->text.data, out.data, out.len) != 0)) {
            term_goto(t, row, 0);
            term_write_bytes(out.data, out.len);
            term_clear_to_eol();
            sl->flag = 0;
            bvec_free(&sl->text);
            sl->text = out;
        } else {
            bvec_free(&out);
        }
    }
    editor_status_line(e, t);
    term_goto(t, e->row, e->vcol - e->margin);
    term_cursor(true);
}

static ByteVec packtabs(const ByteVec *line) {
    ByteVec out;
    bvec_init(&out);
    size_t col = 0;
    for (size_t i = 0; i < line->len; ++i) {
        uint8_t c = line->data[i];
        if (c == ' ') {
            size_t run = 0;
            while (i + run < line->len && line->data[i + run] == ' ') {
                run++;
            }
            if (run >= 2) {
                size_t spaces = 8 - (col % 8);
                if (run >= spaces) {
                    bvec_push(&out, '\t');
                    col += spaces;
                    i += spaces - 1;
                    continue;
                }
            }
        }
        bvec_push(&out, c);
        col += 1;
    }
    return out;
}

static ByteVec expandtabs(const ByteVec *line, bool *write_tabs) {
    ByteVec out;
    bvec_init(&out);
    bool has_tab = false;
    size_t pos = 0;
    for (size_t i = 0; i < line->len; ++i) {
        uint8_t c = line->data[i];
        if (c == '\t') {
            has_tab = true;
            size_t spaces = 8 - (pos % 8);
            for (size_t j = 0; j < spaces; ++j) {
                bvec_push(&out, ' ');
            }
            pos += spaces;
        } else {
            bvec_push(&out, c);
            pos += 1;
        }
    }
    if (has_tab) {
        *write_tabs = true;
        return out;
    }
    bvec_free(&out);
    return bvec_clone(line);
}

static void editor_get_file(Editor *e, const char *fname) {
    if (!fname || !fname[0]) {
        return;
    }
    str_set_cstr(&e->fname, fname);
    struct stat st;
    if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0 || (stat(fname, &st) == 0 && S_ISDIR(st.st_mode))) {
        (void)chdir(fname);
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd))) {
            str_set_cstr(&e->work_dir, cwd);
        } else {
            str_set_cstr(&e->work_dir, "/");
        }
        e->content.len = 0;
        ByteVec line;
        bvec_init(&line);
        bvec_extend(&line, (const uint8_t *)"Directory '", 11);
        bvec_extend(&line, (const uint8_t *)(e->work_dir.data ? e->work_dir.data : "/"),
                    strlen(e->work_dir.data ? e->work_dir.data : "/"));
        bvec_push(&line, '\'');
        linevec_push(&e->content, &line);
        bvec_free(&line);
        linevec_push_empty(&e->content);

        DIR *dir = opendir(".");
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                ByteVec nm;
                bvec_init(&nm);
                bvec_extend(&nm, (const uint8_t *)ent->d_name, strlen(ent->d_name));
                linevec_push(&e->content, &nm);
                bvec_free(&nm);
            }
            closedir(dir);
        }
        e->is_dir = true;
    } else {
        FILE *fp = fopen(fname, "rb");
        if (!fp) {
            str_clear(&e->message);
            str_push_cstr(&e->message, "Error: file '");
            str_push_cstr(&e->message, fname);
            str_push_cstr(&e->message, "' may not exist");
            return;
        }
        linevec_free(&e->content);
        linevec_init(&e->content);
        ByteVec buf;
        bvec_init(&buf);
        uint8_t tmp[512];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), fp)) > 0) {
            bvec_extend(&buf, tmp, n);
        }
        fclose(fp);
        size_t start = 0;
        for (size_t i = 0; i <= buf.len; ++i) {
            if (i == buf.len || buf.data[i] == '\n') {
                ByteVec line;
                bvec_init(&line);
                size_t len = i - start;
                if (len > 0 && buf.data[start + len - 1] == '\r') {
                    len -= 1;
                }
                if (len > 0) {
                    bvec_extend(&line, buf.data + start, len);
                }
                ByteVec expanded = expandtabs(&line, &e->write_tabs);
                linevec_push(&e->content, &expanded);
                bvec_free(&expanded);
                bvec_free(&line);
                start = i + 1;
            }
        }
        if (e->content.len == 0) {
            linevec_push_empty(&e->content);
        }
        e->is_dir = false;
        bvec_free(&buf);
    }
    e->hash = hash_buffer(&e->content);
}

static void editor_put_file(Editor *e, const char *fname) {
    if (!fname || !fname[0]) {
        return;
    }
    Str tmp;
    str_init(&tmp);
    str_push_cstr(&tmp, fname);
    str_push_cstr(&tmp, ".pyetmp");
    FILE *fp = fopen(tmp.data, "wb");
    if (fp) {
        for (size_t i = 0; i < e->content.len; ++i) {
            const ByteVec *line = &e->content.data[i];
            ByteVec out = e->write_tabs ? packtabs(line) : bvec_clone(line);
            if (out.len) {
                fwrite(out.data, 1, out.len, fp);
            }
            fputc('\n', fp);
            bvec_free(&out);
        }
        fclose(fp);
    }
    unlink(fname);
    FILE *src = fopen(tmp.data, "rb");
    if (src) {
        FILE *dst = fopen(fname, "wb");
        if (dst) {
            uint8_t buf[512];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                fwrite(buf, 1, n, dst);
            }
            fclose(dst);
        }
        fclose(src);
        unlink(tmp.data);
    }
    str_free(&tmp);
}

static bool editor_handle_insert(Editor *e, const InputEvent *evt, Shared *shared) {
    if (evt->data_kind != 1 || evt->ch_len == 0) {
        return false;
    }
    if (e->mark) {
        editor_delete_mark(e, shared, false);
    }
    ByteVec line = bvec_clone(&e->content.data[e->cur_line]);
    LineVec slice;
    linevec_init(&slice);
    linevec_push(&slice, &line);
    editor_undo_add(e, e->cur_line, &slice, KEY_NONE, 1, false);
    linevec_free(&slice);
    bvec_insert(&line, e->col, evt->ch, evt->ch_len);
    bvec_free(&e->content.data[e->cur_line]);
    e->content.data[e->cur_line] = line;
    e->col += evt->ch_len;
    return true;
}

static void editor_redraw(Editor *e, Terminal *t, InputBuf *in, bool show_version) {
    term_redraw(t, in);
    if (show_version) {
        str_set_cstr(&e->message, PYE_VERSION);
    }
    e->changed = e->hash != hash_buffer(&e->content);
    if (e->row >= t->height) {
        e->row = t->height > 0 ? t->height - 1 : 0;
    }
}

static bool editor_handle_keys(Editor *e, Terminal *t, InputBuf *in, Shared *shared, const InputEvent *evt, Key *out_key) {
    Key key = evt->key;
    *out_key = key;
    if (key == KEY_NONE) {
        return editor_handle_insert(e, evt, shared);
    }

    ByteVec line = bvec_clone(&e->content.data[e->cur_line]);
    switch (key) {
    case KEY_SHIFT_CTRL_LEFT:
        editor_set_mark(e, 999999999);
        key = KEY_WORD_LEFT;
        break;
    case KEY_SHIFT_CTRL_RIGHT:
        editor_set_mark(e, 999999999);
        key = KEY_WORD_RIGHT;
        break;
    default:
        break;
    }

    switch (key) {
    case KEY_WORD_LEFT: {
        e->col = e->vcol;
        if (e->col > 0) {
            ptrdiff_t pos = skip_until(&line, (ptrdiff_t)e->col - 1, &shared->word_char, -1);
            size_t u = pos < 0 ? 0 : (size_t)pos;
            size_t end = skip_while(&line, u, &shared->word_char, -1);
            e->col = min_size(e->col, end + 1);
        }
        break;
    }
    case KEY_WORD_RIGHT: {
        e->col = e->vcol;
        if (e->col < line.len) {
            ptrdiff_t pos = skip_until(&line, (ptrdiff_t)e->col, &shared->word_char, 1);
            size_t u = pos < 0 ? 0 : (size_t)pos;
            size_t end = skip_while(&line, u, &shared->word_char, 1);
            e->col = min_size(end, line.len);
        }
        break;
    }
    case KEY_LEFT:
        editor_move_left(e);
        break;
    case KEY_RIGHT:
        editor_move_right(e, line.len);
        break;
    case KEY_UP:
        editor_move_up(e, t);
        break;
    case KEY_DOWN:
        editor_move_down(e, t);
        break;
    case KEY_BACKSPACE:
        if (e->mark) {
            editor_delete_mark(e, shared, false);
        } else if (e->col == 0) {
            if (e->cur_line > 0) {
                LineVec slice = linevec_clone_slice(&e->content, e->cur_line - 1, e->cur_line + 1);
                editor_undo_add(e, e->cur_line - 1, &slice, KEY_NONE, 1, false);
                linevec_free(&slice);
                ByteVec *prev = &e->content.data[e->cur_line - 1];
                bvec_extend(prev, line.data, line.len);
                linevec_remove(&e->content, e->cur_line);
                e->cur_line -= 1;
                e->col = prev->len;
            }
        } else {
            LineVec slice;
            linevec_init(&slice);
            linevec_push(&slice, &line);
            editor_undo_add(e, e->cur_line, &slice, KEY_BACKSPACE, 1, false);
            linevec_free(&slice);
            bvec_erase(&line, e->col - 1, e->col);
            bvec_free(&e->content.data[e->cur_line]);
            e->content.data[e->cur_line] = line;
            e->col -= 1;
            return true;
        }
        break;
    case KEY_DELETE:
        if (e->mark) {
            editor_delete_mark(e, shared, false);
        } else if (e->col >= line.len) {
            if (e->cur_line + 1 < e->content.len) {
                ByteVec next = bvec_clone(&e->content.data[e->cur_line + 1]);
                LineVec slice = linevec_clone_slice(&e->content, e->cur_line, e->cur_line + 2);
                editor_undo_add(e, e->cur_line, &slice, KEY_NONE, 1, false);
                linevec_free(&slice);
                if (shared->autoindent && e->col > 0) {
                    size_t ns = spaces(&next, 0);
                    if (ns > 0) {
                        bvec_erase(&next, 0, ns);
                    }
                }
                ByteVec *cur = &e->content.data[e->cur_line];
                bvec_extend(cur, next.data, next.len);
                bvec_free(&next);
                linevec_remove(&e->content, e->cur_line + 1);
            }
        } else {
            LineVec slice;
            linevec_init(&slice);
            linevec_push(&slice, &line);
            editor_undo_add(e, e->cur_line, &slice, KEY_DELETE, 1, false);
            linevec_free(&slice);
            bvec_erase(&line, e->col, e->col + 1);
            bvec_free(&e->content.data[e->cur_line]);
            e->content.data[e->cur_line] = line;
            return true;
        }
        break;
    case KEY_HOME: {
        size_t indent = spaces(&line, 0);
        e->col = e->col == 0 ? indent : 0;
        break;
    }
    case KEY_END: {
        size_t trimmed_len = 0;
        const uint8_t *comment = trim_ascii_space(shared->comment_char.data, shared->comment_char.len, &trimmed_len);
        ByteVec split = bvec_clone(&line);
        if (trimmed_len > 0) {
            size_t pos = find_subslice(line.data, line.len, comment, trimmed_len);
            if (pos != (size_t)-1) {
                split.len = pos;
            }
        }
        size_t ni = 0;
        for (size_t i = split.len; i > 0; --i) {
            if (split.data[i - 1] != ' ') {
                ni = i;
                break;
            }
        }
        size_t ns = spaces(&line, 0);
        e->col = (e->col >= line.len && ni > ns) ? ni : line.len;
        bvec_free(&split);
        break;
    }
    case KEY_PGUP:
        if (e->cur_line >= t->height) {
            e->cur_line -= t->height;
        } else {
            e->cur_line = 0;
        }
        break;
    case KEY_PGDN:
        e->cur_line = min_size(e->cur_line + t->height, e->content.len ? e->content.len - 1 : 0);
        break;
    case KEY_FIND: {
        Str pat;
        str_init(&pat);
        const char *def = shared->find_pattern.data ? shared->find_pattern.data : "";
        if (editor_line_edit(e, t, in, "Find: ", def, &shared->word_char, &pat)) {
            if (pat.len > 0) {
                str_set_bytes(&shared->find_pattern, (const uint8_t *)pat.data, pat.len);
                editor_clear_mark(e);
                if (!editor_find_in_file(e, shared->find_pattern.data, e->col + 1, e->content.len, shared)) {
                    str_clear(&e->message);
                    str_push_cstr(&e->message, shared->find_pattern.data);
                    str_push_cstr(&e->message, " not found (again)");
                }
                e->row = t->height >> 1;
            }
        }
        str_free(&pat);
        break;
    }
    case KEY_FIND_AGAIN: {
        if (shared->find_pattern.len > 0) {
            if (!editor_find_in_file(e, shared->find_pattern.data, e->col + 1, e->content.len, shared)) {
                str_clear(&e->message);
                str_push_cstr(&e->message, shared->find_pattern.data);
                str_push_cstr(&e->message, " not found (again)");
            }
            e->row = t->height >> 1;
        }
        break;
    }
    case KEY_GOTO: {
        Str line;
        str_init(&line);
        if (editor_line_edit(e, t, in, "Goto Line: ", "", NULL, &line)) {
            if (line.len > 0) {
                long num = strtol(line.data, NULL, 10);
                if (num > 0) {
                    size_t target = (size_t)(num - 1);
                    if (target >= e->content.len) {
                        target = e->content.len ? e->content.len - 1 : 0;
                    }
                    e->cur_line = target;
                    e->row = t->height >> 1;
                }
            }
        }
        str_free(&line);
        break;
    }
    case KEY_REDRAW:
        editor_redraw(e, t, in, true);
        break;
    case KEY_MARK:
        editor_set_mark(e, 2);
        break;
    case KEY_COPY:
        if (e->mark) {
            editor_yank_mark(e, shared);
            editor_clear_mark(e);
        }
        break;
    case KEY_CUT:
        if (!e->mark) {
            if (e->cur_line + 1 < e->content.len) {
                e->mark = true;
                e->mark_line = e->cur_line + 1;
                e->mark_col = 0;
            } else {
                e->mark = true;
                e->mark_line = e->cur_line;
                e->mark_col = line.len;
            }
            e->col = 0;
        }
        editor_delete_mark(e, shared, true);
        break;
    case KEY_PASTE:
        if (shared->yank_buffer.len > 0) {
            LineVec slice;
            linevec_init(&slice);
            linevec_push(&slice, &line);
            editor_undo_add(e, e->cur_line, &slice, KEY_NONE, 1, false);
            linevec_free(&slice);
            ByteVec *cur = &e->content.data[e->cur_line];
            ByteVec tail;
            bvec_init(&tail);
            if (e->col < cur->len) {
                bvec_extend(&tail, cur->data + e->col, cur->len - e->col);
                cur->len = e->col;
            }
            ByteVec first = bvec_clone(&shared->yank_buffer.data[0]);
            bvec_extend(cur, first.data, first.len);
            bvec_free(&first);
            for (size_t i = 1; i < shared->yank_buffer.len; ++i) {
                ByteVec ln = bvec_clone(&shared->yank_buffer.data[i]);
                linevec_insert(&e->content, e->cur_line + i, &ln);
                bvec_free(&ln);
            }
            size_t last_line = e->cur_line + shared->yank_buffer.len - 1;
            bvec_extend(&e->content.data[last_line], tail.data, tail.len);
            bvec_free(&tail);
            e->cur_line = last_line;
            e->col = shared->yank_buffer.data[shared->yank_buffer.len - 1].len;
        }
        break;
    case KEY_COMMENT: {
        size_t sline = e->cur_line;
        size_t eline = e->cur_line + 1;
        if (e->mark) {
            size_t scol = 0;
            size_t ecol = 0;
            editor_mark_range(e, &sline, &scol, &eline, &ecol);
            if (ecol == 0 && eline > 0) {
                eline -= 1;
            }
        }
        if (sline < eline && eline <= e->content.len) {
            LineVec slice = linevec_clone_slice(&e->content, sline, eline);
            editor_undo_add(e, sline, &slice, KEY_COMMENT, (ptrdiff_t)(eline - sline), false);
            linevec_free(&slice);
            size_t cc_len = shared->comment_char.len;
            for (size_t i = sline; i < eline; ++i) {
                ByteVec *ln = &e->content.data[i];
                size_t ns = spaces(ln, 0);
                if (ln->len == 0 || ns >= ln->len) {
                    continue;
                }
                if (cc_len > 0 && ln->len >= ns + cc_len &&
                    memcmp(ln->data + ns, shared->comment_char.data, cc_len) == 0) {
                    bvec_erase(ln, ns, ns + cc_len);
                } else if (cc_len > 0) {
                    bvec_insert(ln, ns, shared->comment_char.data, cc_len);
                }
            }
        }
        break;
    }
    case KEY_WRITE:
        {
            Str fname;
            str_init(&fname);
            const char *def = (!e->is_dir && e->fname.data) ? e->fname.data : "";
            if (editor_line_edit(e, t, in, "Save File: ", def, &shared->file_char, &fname)) {
                if (fname.len > 0) {
                    bool same = e->fname.data && strcmp(fname.data, e->fname.data) == 0;
                    if (!same && access(fname.data, F_OK) == 0) {
                        Str res;
                        str_init(&res);
                        bool ok = editor_line_edit(e, t, in, "The file exists! Overwrite (y/N)? ", "N", NULL, &res);
                        bool allow = ok && res.len > 0 && (res.data[0] == 'y' || res.data[0] == 'Y');
                        str_free(&res);
                        if (!allow) {
                            str_free(&fname);
                            break;
                        }
                    }
                    editor_put_file(e, fname.data);
                    str_set_cstr(&e->fname, fname.data);
                    e->hash = hash_buffer(&e->content);
                    e->changed = false;
                    e->is_dir = false;
                }
            }
            str_free(&fname);
        }
        break;
    case KEY_GET:
        {
            Str fname;
            str_init(&fname);
            if (editor_line_edit(e, t, in, "Open file: ", "", &shared->file_char, &fname)) {
                if (fname.len > 0) {
                    editor_get_file(e, fname.data);
                }
            }
            str_free(&fname);
        }
        break;
    case KEY_TOGGLE: {
        Str pat;
        str_init(&pat);
        Str prompt;
        str_init(&prompt);
        Str comment;
        str_init(&comment);
        str_set_bytes(&comment, shared->comment_char.data, shared->comment_char.len);
        str_push_cstr(&prompt, "Autoindent ");
        str_push_cstr(&prompt, shared->autoindent ? "y" : "n");
        str_push_cstr(&prompt, ", Search Case ");
        str_push_cstr(&prompt, shared->case_sensitive ? "y" : "n");
        str_push_cstr(&prompt, ", Tabsize ");
        str_push_usize(&prompt, e->tab_size);
        str_push_cstr(&prompt, ", Comment ");
        str_push_cstr(&prompt, comment.data ? comment.data : "");
        str_push_cstr(&prompt, ", Tabwrite ");
        str_push_cstr(&prompt, e->write_tabs ? "y" : "n");
        str_push_cstr(&prompt, ": ");
        if (editor_line_edit(e, t, in, prompt.data ? prompt.data : "", "", NULL, &pat)) {
            if (pat.len > 0) {
                char *cfg = pat.data;
                for (int idx = 0; idx < 5; ++idx) {
                    char *tok = strsep(&cfg, ",");
                    if (!tok) {
                        break;
                    }
                    while (*tok == ' ') {
                        tok++;
                    }
                    if (tok[0] == '\0') {
                        continue;
                    }
                    if (idx == 0) {
                        shared->autoindent = (tok[0] == 'y' || tok[0] == 'Y');
                    } else if (idx == 1) {
                        shared->case_sensitive = (tok[0] == 'y' || tok[0] == 'Y');
                    } else if (idx == 2) {
                        long v = strtol(tok, NULL, 10);
                        if (v > 0) {
                            e->tab_size = (size_t)v;
                        }
                    } else if (idx == 3) {
                        shared_set_comment_char(shared, tok);
                    } else if (idx == 4) {
                        e->write_tabs = (tok[0] == 'y' || tok[0] == 'Y');
                    }
                }
            }
        }
        str_free(&comment);
        str_free(&prompt);
        str_free(&pat);
        break;
    }
    case KEY_FIRST:
        editor_check_mark(e);
        e->cur_line = 0;
        break;
    case KEY_LAST:
        editor_check_mark(e);
        e->cur_line = e->content.len ? e->content.len - 1 : 0;
        e->row = t->height ? t->height - 1 : 0;
        break;
    case KEY_ENTER: {
        if (e->mark) {
            editor_delete_mark(e, shared, false);
        }
        LineVec slice;
        linevec_init(&slice);
        linevec_push(&slice, &line);
        editor_undo_add(e, e->cur_line, &slice, KEY_NONE, 1, false);
        linevec_free(&slice);
        ByteVec *cur = &e->content.data[e->cur_line];
        ByteVec new_line;
        bvec_init(&new_line);
        if (e->col < cur->len) {
            bvec_extend(&new_line, cur->data + e->col, cur->len - e->col);
            cur->len = e->col;
        }
        if (shared->autoindent) {
            size_t ns = spaces(cur, 0);
            if (ns > 0) {
                bvec_insert(&new_line, 0, cur->data, ns);
            }
        }
        linevec_insert(&e->content, e->cur_line + 1, &new_line);
        bvec_free(&new_line);
        e->cur_line += 1;
        e->col = shared->autoindent ? spaces(&e->content.data[e->cur_line], 0) : 0;
        break;
    }
    default:
        break;
    }
    bvec_free(&line);
    return true;
}

static int editor_loop(Editor *e, Terminal *t, InputBuf *in, Shared *shared) {
    editor_redraw(e, t, in, true);
    for (;;) {
        editor_display_window(e, t);
        InputEvent evt = input_get(in, e->key_max);
        Key key = evt.key;
        if (key == KEY_QUIT) {
            if (e->changed) {
                Str res;
                str_init(&res);
                if (editor_line_edit(e, t, in, "File changed! Quit (y/N/f)? ", "N", NULL, &res)) {
                    if (res.len > 0) {
                        uint8_t c = (uint8_t)(res.data[0] | 0x20);
                        if (c == 'y') {
                            str_free(&res);
                            return 0;
                        }
                        if (c == 'f') {
                            str_free(&res);
                            return 2;
                        }
                    }
                }
                str_free(&res);
            } else {
                return 0;
            }
        }
        if (key == KEY_FORCE_QUIT) {
            return 2;
        }
        editor_handle_keys(e, t, in, shared, &evt, &key);
    }
}

int main(int argc, char **argv) {
    InputBuf input;
    inputbuf_init(&input);
    Terminal term;
    term_init(&term, &input);
    Shared shared;
    shared_init(&shared);

    Editor editor;
    editor_init(&editor, 1, 4, 500);
    if (argc > 1) {
        editor_get_file(&editor, argv[1]);
    } else {
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd))) {
            editor_get_file(&editor, cwd);
        }
    }

    int rc = editor_loop(&editor, &term, &input, &shared);
    editor_free(&editor);
    shared_free(&shared);
    inputbuf_free(&input);
    for (size_t i = 0; i < term.scrbuf_len; ++i) {
        bvec_free(&term.scrbuf[i].text);
    }
    free(term.scrbuf);
    term_cursor(true);
    term_hilite(0);
    term_restore();
    return rc == 2 ? 1 : 0;
}
