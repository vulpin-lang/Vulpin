/* `vulpin repl` — interactive REPL. Keeps a single VM alive for the whole
 * session: each accepted input's parsed statements are appended to the VM's
 * program array and vr() is re-run, which resumes from the ip it stopped at
 * rather than restarting — so variables, functions and labels persist
 * naturally. See vm.c: vr() loops `while(v->ip<v->nc)`, it never resets ip.
 *
 * Entry point is vulpin_repl_main(argc, argv), called from vulpin.c's main()
 * when argv[1] is "repl"; it's otherwise a standalone translation unit with
 * no dependencies beyond libc (no readline, nothing external).
 */
#include "lib/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

extern int parser_had_error(void);
extern const char *parser_error(void);

/* ANSI colors, enabled only when stdout is a real terminal (so piping the
 * REPL's output, e.g. in tests, stays plain text). */
static const char *CRESET = "", *CBOLD = "", *CDIM = "", *CRED = "",
                   *CGREEN = "", *CYELLOW = "", *CMAGENTA = "", *CCYAN = "",
                   *CGRAY = "";
static void init_colors(void) {
    if (!isatty(1)) return;
    CRESET = "\033[0m"; CBOLD = "\033[1m"; CDIM = "\033[2m"; CRED = "\033[31m";
    CGREEN = "\033[32m"; CYELLOW = "\033[33m"; CMAGENTA = "\033[35m";
    CCYAN = "\033[36m"; CGRAY = "\033[90m";
}

/* Redirect fd 1 or 2 to a temp file for the duration of a VM call, so we can
 * post-process and recolor whatever it printed before it reaches the real
 * terminal. The VM/parser themselves are never touched. */
typedef struct { FILE *tmp; int saved_fd; int fd; } Capture;
static Capture capture_begin(int fd) {
    Capture c; c.fd = fd;
    fflush(fd == 1 ? stdout : stderr);
    c.saved_fd = dup(fd);
    c.tmp = tmpfile();
    dup2(fileno(c.tmp), fd);
    return c;
}
static char *capture_end(Capture c) {
    fflush(c.fd == 1 ? stdout : stderr);
    dup2(c.saved_fd, c.fd);
    close(c.saved_fd);
    fseek(c.tmp, 0, SEEK_END);
    long sz = ftell(c.tmp);
    fseek(c.tmp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, c.tmp);
    buf[n] = 0;
    fclose(c.tmp);
    return buf;
}

/* vm.c's VERR prints "Error: ..." (runtime) or the parser prints
 * "error: ..." plus "  --> line N, column M" / source-line / caret context
 * (syntax). Recolor the message line red/bold and dim the context lines. */
static void emit_colored_stderr(const char *text) {
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len == 0) { if (!nl) break; p = nl + 1; continue; }
        int is_msg = (len >= 6 && (!strncmp(p, "Error:", 6) || !strncmp(p, "error:", 6)));
        fprintf(stderr, "%s%.*s%s\n", is_msg ? CRED : CGRAY, (int)len, p, CRESET);
        if (!nl) break;
        p = nl + 1;
    }
}

/* Drop-in replacement for vr(vm) that recolors whatever it wrote to stderr. */
static int vr_colored(VM *vm) {
    Capture c = capture_begin(2);
    int rc = vr(vm);
    char *err = capture_end(c);
    if (*err) emit_colored_stderr(err);
    free(err);
    return rc;
}

/* The parser prints its own "error: ..." diagnostic straight to stderr as
 * soon as it hits a problem (see parser.c's perr()/perr_node()). An
 * "unterminated X block" error just means our buffered input isn't a
 * complete statement yet — expected and common while typing a multi-line
 * block — so we mute stderr for probe parses and only let a real syntax
 * error's diagnostic through (via a second, unmuted parse). */
static int saved_stderr_fd = -1, devnull_fd = -1;
static void mute_stderr(void) {
    fflush(stderr);
    saved_stderr_fd = dup(2);
    devnull_fd = open("/dev/null", O_WRONLY);
    dup2(devnull_fd, 2);
}
static void unmute_stderr(void) {
    fflush(stderr);
    dup2(saved_stderr_fd, 2);
    close(saved_stderr_fd);
    close(devnull_fd);
}

/* ---------------------------------------------------------------------
 * Live syntax-highlighting line editor.
 *
 * GNU readline has no hook for recoloring the buffer as the user types,
 * so this is a small raw-mode editor of our own: it reads one byte at a
 * time, maintains buf/cursor itself, and redraws the whole line with
 * ANSI colors on every keystroke. It only owns input collection — raw
 * mode is entered at the start of vul_readline() and left before it
 * returns, so by the time a submitted line reaches vr() (which may run
 * Vulpin's `K` input statement, or `o"..."` / `!...` shell-outs) the
 * terminal is back in normal cooked mode for those to behave correctly.
 * History is a small home-grown store (see hist_add/hist_load/hist_save
 * below) rather than readline's history.h — the REPL has no external
 * dependency beyond libc, matching Vulpin's own build.
 *
 * The tokenizer below is cosmetic only — it mirrors parser.c's top-level
 * command dispatch (parseStmtAt) closely enough to color correctly for
 * normal input, but it isn't the real grammar and never needs to be;
 * worst case a weird line is colored a little wrong, parsing is unaffected.
 * --------------------------------------------------------------------- */
static struct termios orig_termios;
static int raw_active = 0;

static void raw_off(void) {
    if (!raw_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_active = 0;
}
static void raw_on(void) {
    fflush(stdout);
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(unsigned)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_active = 1;
}

/* Leading statement commands per parser.c's parseStmtAt() switch, plus the
 * block-closer letters and the handful of punctuation commands. */
static int is_leading_cmd(char c) {
    return strchr("GPXEDKAMTFRLJWVCZIOUS", c) != NULL ||
           strchr(":&~QNY;?!", c) != NULL;
}
static int is_op_char(char c) { return strchr("+-*/%=<>!&|", c) != NULL; }

/* Appends a colorized copy of buf[0..len) to out (capped at outcap),
 * advancing *o. Not used for anything but display. */
static void colorize_append(const char *buf, size_t len, char *out, size_t outcap, size_t *o) {
    size_t i = 0;
    while (i < len && (buf[i] == ' ' || buf[i] == '\t') && *o < outcap - 1) out[(*o)++] = buf[i++];
    if (i < len && *o < outcap - 1) {
        if (buf[i] == '#') {
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CGRAY);
            while (i < len && *o < outcap - 1) out[(*o)++] = buf[i++];
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CRESET);
            return;
        }
        if (is_leading_cmd(buf[i])) {
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s%s%c%s", CBOLD, CMAGENTA, buf[i], CRESET);
            i++;
        }
    }
    int in_str = 0;
    while (i < len && *o < outcap - 1) {
        char c = buf[i];
        if (in_str) {
            out[(*o)++] = c; i++;
            if (c == '"') { in_str = 0; *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CRESET); }
            continue;
        }
        if (c == '"') { in_str = 1; *o += (size_t)snprintf(out + *o, outcap - *o, "%s\"", CGREEN); i++; continue; }
        if (c == '#') {
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CGRAY);
            while (i < len && *o < outcap - 1) out[(*o)++] = buf[i++];
            break;
        }
        if (isdigit((unsigned char)c)) {
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CCYAN);
            while (i < len && *o < outcap - 1 && (isdigit((unsigned char)buf[i]) || buf[i] == '.')) out[(*o)++] = buf[i++];
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CRESET);
            continue;
        }
        if (is_op_char(c)) {
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CDIM);
            while (i < len && *o < outcap - 1 && is_op_char(buf[i])) out[(*o)++] = buf[i++];
            *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CRESET);
            continue;
        }
        out[(*o)++] = c; i++;
    }
    if (in_str) *o += (size_t)snprintf(out + *o, outcap - *o, "%s", CRESET);
}

/* Our own minimal line history (replaces readline's history.h, so the REPL
 * has no external dependency beyond libc — matching Vulpin's own build).
 * Just an append-only array of malloc'd lines, persisted one-per-line. */
static char **hist_lines = NULL;
static int hist_count = 0, hist_cap = 0;

static void hist_add(const char *line) {
    if (!line || !*line) return;
    if (hist_count > 0 && !strcmp(hist_lines[hist_count - 1], line)) return; /* skip consecutive dupes */
    if (hist_count >= hist_cap) {
        hist_cap = hist_cap ? hist_cap * 2 : 64;
        hist_lines = realloc(hist_lines, (size_t)hist_cap * sizeof(char *));
    }
    hist_lines[hist_count++] = strdup(line);
}
static void hist_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, f)) != -1) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        hist_add(line);
    }
    free(line);
    fclose(f);
}
static void hist_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < hist_count; i++) fprintf(f, "%s\n", hist_lines[i]);
    fclose(f);
}

static void redraw(const char *label, const char *label_color, const char *buf, size_t len, size_t cursor) {
    char out[8192];
    size_t o = 0;
    o += (size_t)snprintf(out + o, sizeof(out) - o, "\r\033[K%s%s%s", label_color, label, CRESET);
    colorize_append(buf, len, out, sizeof(out), &o);
    write(STDOUT_FILENO, out, o);
    char mv[32];
    int ml = snprintf(mv, sizeof(mv), "\r\033[%zuC", strlen(label) + cursor);
    write(STDOUT_FILENO, mv, ml);
}

/* Set once at startup: whether both stdin and stdout are a real terminal.
 * The raw-mode editor below assumes an interactive terminal (it writes
 * cursor-control escapes unconditionally) — when piped (scripts, tests,
 * `vulpin repl < file`), fall back to plain line-buffered reads instead,
 * so output stays clean. */
static int tty_interactive = 0;

/* Returns a malloc'd line (caller frees), or NULL on EOF with an empty
 * buffer (mirrors readline()'s own EOF convention). */
static char *vul_readline(int is_continuation) {
    const char *label = is_continuation ? "  ...> " : "vul> ";
    const char *label_color = is_continuation ? CDIM : CBOLD;
    /* vul> gets its own bold+cyan combo; build it once per call since
     * CBOLD/CCYAN are runtime strings, not compile-time constants. */
    char combo[32];
    if (!is_continuation) { snprintf(combo, sizeof(combo), "%s%s", CBOLD, CCYAN); label_color = combo; }

    if (!tty_interactive) {
        printf("%s%s%s", label_color, label, CRESET);
        fflush(stdout);
        char *pline = NULL;
        size_t pcap = 0;
        ssize_t n = getline(&pline, &pcap, stdin);
        if (n == -1) { free(pline); return NULL; }
        while (n > 0 && (pline[n - 1] == '\n' || pline[n - 1] == '\r')) pline[--n] = 0;
        return pline;
    }

    size_t cap = 128, len = 0, cursor = 0;
    char *buf = malloc(cap);
    buf[0] = 0;

    int hist_total = hist_count;
    int hist_pos = -1; /* -1 == live line, not navigating history */
    char *hist_saved_live = NULL;

    raw_on();
    redraw(label, label_color, buf, len, cursor);
    for (;;) {
        unsigned char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) {
            raw_off();
            if (len == 0) { free(buf); free(hist_saved_live); return NULL; }
            break;
        }
        if (c == '\r' || c == '\n') { write(STDOUT_FILENO, "\r\n", 2); break; }
        else if (c == 3) { /* Ctrl-C: abort this line, stay in the editor */
            write(STDOUT_FILENO, "^C\r\n", 4);
            len = cursor = 0; buf[0] = 0;
            hist_pos = -1; free(hist_saved_live); hist_saved_live = NULL;
        }
        else if (c == 4) { /* Ctrl-D */
            if (len == 0) { raw_off(); free(buf); free(hist_saved_live); return NULL; }
        }
        else if (c == 127 || c == 8) { /* backspace */
            if (cursor > 0) { memmove(buf + cursor - 1, buf + cursor, len - cursor); len--; cursor--; buf[len] = 0; }
        }
        else if (c == 1) cursor = 0;          /* Ctrl-A: home */
        else if (c == 5) cursor = len;        /* Ctrl-E: end */
        else if (c == 11) { buf[cursor] = 0; len = cursor; } /* Ctrl-K: kill to EOL */
        else if (c == 27) { /* ESC sequence */
            unsigned char s0, s1;
            if (read(STDIN_FILENO, &s0, 1) <= 0) continue;
            if (s0 != '[' && s0 != 'O') continue;
            if (read(STDIN_FILENO, &s1, 1) <= 0) continue;
            if (s1 == 'C') { if (cursor < len) cursor++; }
            else if (s1 == 'D') { if (cursor > 0) cursor--; }
            else if (s1 == 'H') cursor = 0;
            else if (s1 == 'F') cursor = len;
            else if (s1 == 'A' || s1 == 'B') { /* up/down: history */
                if (hist_total > 0) {
                    if (s1 == 'A') {
                        if (hist_pos == -1) { free(hist_saved_live); hist_saved_live = strdup(buf); hist_pos = hist_total - 1; }
                        else if (hist_pos > 0) hist_pos--;
                        const char *e = hist_lines[hist_pos];
                        size_t el = strlen(e);
                        if (el + 1 > cap) { cap = el + 1; buf = realloc(buf, cap); }
                        memcpy(buf, e, el + 1); len = cursor = el;
                    } else if (hist_pos != -1) {
                        if (hist_pos < hist_total - 1) {
                            hist_pos++;
                            const char *e = hist_lines[hist_pos];
                            size_t el = strlen(e);
                            if (el + 1 > cap) { cap = el + 1; buf = realloc(buf, cap); }
                            memcpy(buf, e, el + 1); len = cursor = el;
                        } else {
                            hist_pos = -1;
                            const char *e = hist_saved_live ? hist_saved_live : "";
                            size_t el = strlen(e);
                            if (el + 1 > cap) { cap = el + 1; buf = realloc(buf, cap); }
                            memcpy(buf, e, el + 1); len = cursor = el;
                            free(hist_saved_live); hist_saved_live = NULL;
                        }
                    }
                }
            } else if (s1 == '3') { /* delete key: ESC [ 3 ~ */
                unsigned char tilde; read(STDIN_FILENO, &tilde, 1);
                if (cursor < len) { memmove(buf + cursor, buf + cursor + 1, len - cursor - 1); len--; buf[len] = 0; }
            } else if (s1 >= '1' && s1 <= '9') { /* Home/End variants: ESC [ 1~ / 4~ etc */
                unsigned char tilde; read(STDIN_FILENO, &tilde, 1);
                if (s1 == '1') cursor = 0; else if (s1 == '4') cursor = len;
            }
        }
        else if (c >= 32 && c < 127) { /* printable */
            if (len + 2 > cap) { cap *= 2; buf = realloc(buf, cap); }
            memmove(buf + cursor + 1, buf + cursor, len - cursor);
            buf[cursor] = (char)c; len++; cursor++; buf[len] = 0;
        }
        else continue; /* ignore other control bytes */
        redraw(label, label_color, buf, len, cursor);
    }
    raw_off();
    free(hist_saved_live);
    char *result = strdup(buf);
    free(buf);
    return result;
}

static const char *HIST_FILE_SUFFIX = "/.vulpin_history";

static char *hist_path(void) {
    const char *home = getenv("HOME");
    if (!home) return NULL;
    size_t n = strlen(home) + strlen(HIST_FILE_SUFFIX) + 1;
    char *p = malloc(n);
    snprintf(p, n, "%s%s", home, HIST_FILE_SUFFIX);
    return p;
}

/* Grow prog->c the same way parser.c's internal `ac()` does, and reparent
 * frag's top-level children into prog. Frees just frag's own node/array,
 * never the children (now owned by prog). */
static void merge_fragment(Node *prog, Node *frag) {
    for (int i = 0; i < frag->n; i++) {
        if (prog->n >= prog->cap) {
            prog->cap = prog->cap ? prog->cap * 2 : 4;
            prog->c = realloc(prog->c, (size_t)prog->cap * sizeof(Node *));
        }
        prog->c[prog->n++] = frag->c[i];
    }
    free(frag->c);
    free(frag->v);
    free(frag);
}

/* v->v (the VM's variable table) is an opaque VM2* — vm.h exposes no way to
 * iterate it. Rather than patch the vendored VM, we "cheat" off parser.c's
 * AST instead: every place a variable gets bound has a recognizable node
 * type, so we track names by scanning each accepted fragment's top-level
 * nodes (block bodies are flat siblings here, not nested children, so one
 * pass over frag->c already covers everything). :vars then reads current
 * values back out of the live VM the normal way, via a tiny synthesized
 * print per tracked name. */
static char **known_vars = NULL;
static int known_vars_n = 0, known_vars_cap = 0;

static int var_idx(const char *name) {
    for (int i = 0; i < known_vars_n; i++)
        if (!strcmp(known_vars[i], name)) return i;
    return -1;
}
static void track_var(const char *name) {
    if (!name || !*name || var_idx(name) != -1) return;
    if (known_vars_n >= known_vars_cap) {
        known_vars_cap = known_vars_cap ? known_vars_cap * 2 : 8;
        known_vars = realloc(known_vars, (size_t)known_vars_cap * sizeof(char *));
    }
    known_vars[known_vars_n++] = strdup(name);
}
static void untrack_var(const char *name) {
    int i = var_idx(name);
    if (i == -1) return;
    free(known_vars[i]);
    known_vars[i] = known_vars[--known_vars_n];
}
static void clear_tracked_vars(void) {
    for (int i = 0; i < known_vars_n; i++) free(known_vars[i]);
    known_vars_n = 0;
}

static void scan_vars(Node *frag) {
    for (int i = 0; i < frag->n; i++) {
        Node *n = frag->c[i];
        switch (n->t) {
            case ND_ASSIGN: case ND_ARITH:
                if (n->n >= 1) track_var(n->c[0]->v);
                break;
            case ND_FOR:
                if (n->n >= 1) track_var(n->c[0]->v);
                break;
            case ND_ENUMERATE:
                if (n->n >= 2) { track_var(n->c[0]->v); track_var(n->c[1]->v); }
                break;
            case ND_INPUT:
                if (n->n >= 1 && n->c[0]->t == ND_IDENT) track_var(n->c[0]->v);
                break;
            case ND_DEL:
                untrack_var(n->v);
                break;
            default: break;
        }
    }
}

/* Color a value by its Vulpin runtime type name, as reported by Vulpin's own
 * `I <expr>` (inspect) statement — "TypeName: value". */
static const char *type_color(const char *typename) {
    if (!strcmp(typename, "Int") || !strcmp(typename, "Float")) return CCYAN;
    if (!strcmp(typename, "Str")) return CGREEN;
    if (!strcmp(typename, "Bool")) return CYELLOW;
    if (!strcmp(typename, "List") || !strcmp(typename, "Dict") || !strcmp(typename, "Set")) return CMAGENTA;
    return CGRAY; /* None */
}

static void print_vars(VM *vm, int debug) {
    if (known_vars_n == 0) { printf("%s(no variables yet)%s\n", CDIM, CRESET); return; }
    size_t cap = 64, len = 0;
    char *snippet = malloc(cap);
    snippet[0] = 0;
    for (int i = 0; i < known_vars_n; i++) {
        char line[300];
        snprintf(line, sizeof(line), "I %s\n", known_vars[i]);
        size_t l = strlen(line);
        while (len + l + 1 > cap) { cap *= 2; snippet = realloc(snippet, cap); }
        memcpy(snippet + len, line, l + 1);
        len += l;
    }
    Node *frag = parse(snippet);
    free(snippet);
    if (parser_had_error()) { ft(frag); return; } /* names always valid idents; shouldn't happen */
    if (debug) pt(frag, 0);
    merge_fragment(vm->p, frag);
    vm->nc = vm->p->n;
    vp(vm);

    Capture c = capture_begin(1);
    vr(vm);
    char *out = capture_end(c);

    char *line = strtok(out, "\n");
    for (int i = 0; i < known_vars_n && line; i++, line = strtok(NULL, "\n")) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        const char *typename = line;
        const char *value = colon + 1;
        while (*value == ' ') value++;
        printf("  %s%-10s%s %s=%s %s%s%s\n", CBOLD, known_vars[i], CRESET,
               CDIM, CRESET, type_color(typename), value, CRESET);
    }
    free(out);
}

static void run_fragment(VM *vm, const char *text, int debug) {
    Node *frag = parse(text);
    if (parser_had_error()) {
        ft(frag);
        return;
    }
    if (debug) pt(frag, 0);
    scan_vars(frag);
    merge_fragment(vm->p, frag);
    vm->nc = vm->p->n;
    vp(vm);
    vr_colored(vm);
}

static void load_file(VM *vm, const char *path, int debug) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open '%s'\n", path); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0;
    fclose(f);
    run_fragment(vm, buf, debug);
    free(buf);
}

static void print_help(void) {
    printf(
        "Vulpin REPL\n"
        "  :help          show this message\n"
        "  :vars          list variables assigned so far, with current values\n"
        "  :load <file>   parse & run a .vul file into the current session\n"
        "  :debug         toggle AST dump for each accepted input\n"
        "  :reset         start a fresh VM (clears all state)\n"
        "  :abort         discard a pending, not-yet-closed input (e.g. an\n"
        "                 unclosed function definition)\n"
        "  :quit / :q     exit\n");
}

int vulpin_repl_main(int argc, char **argv) {
    init_colors();
    tty_interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    atexit(raw_off); /* safety net: never leave the terminal in raw mode */
    int debug = 0;
    const char *preload = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--debug")) debug = 1;
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
            Node *prog = parse("");
            VM *vm = nv(prog);
            vp(vm);
            run_fragment(vm, argv[++i], debug);
            vf(vm);
            ft(prog);
            return 0;
        } else preload = argv[i];
    }

    Node *prog = parse("");
    VM *vm = nv(prog);
    vp(vm);

    char *hp = hist_path();
    if (hp) hist_load(hp);

    if (preload) load_file(vm, preload, debug);

    printf("%sVulpin REPL%s — %s:help%s for commands, %s:quit%s to exit\n",
           CBOLD, CRESET, CCYAN, CRESET, CCYAN, CRESET);

    char *pending = NULL; /* buffered text while a block is still open */

    for (;;) {
        char *line = vul_readline(pending != NULL);
        if (!line) { putchar('\n'); break; }

        if (!pending && line[0] == ':') {
            if (*line) hist_add(line);
            if (!strcmp(line, ":quit") || !strcmp(line, ":q")) { free(line); break; }
            else if (!strcmp(line, ":help")) print_help();
            else if (!strcmp(line, ":vars")) print_vars(vm, debug);
            else if (!strcmp(line, ":debug")) { debug = !debug; printf("%sdebug: %s%s\n", CDIM, debug ? "on" : "off", CRESET); }
            else if (!strcmp(line, ":reset")) {
                vf(vm); ft(prog);
                prog = parse(""); vm = nv(prog); vp(vm);
                clear_tracked_vars();
                printf("%ssession reset%s\n", CDIM, CRESET);
            } else if (!strncmp(line, ":load ", 6)) {
                load_file(vm, line + 6, debug);
            } else if (!strcmp(line, ":abort")) {
                free(pending); pending = NULL;
                printf("%spending input discarded%s\n", CDIM, CRESET);
            } else {
                printf("%sUnknown command '%s' (:help for a list)%s\n", CRED, line, CRESET);
            }
            free(line);
            continue;
        }

        if (*line) hist_add(line);

        size_t add_len = strlen(line) + 1;
        if (pending) {
            size_t old_len = strlen(pending);
            pending = realloc(pending, old_len + add_len + 1);
            pending[old_len] = '\n';
            memcpy(pending + old_len + 1, line, add_len);
        } else {
            pending = malloc(add_len);
            memcpy(pending, line, add_len);
        }
        free(line);

        mute_stderr();
        Node *probe = parse(pending);
        int had_err = parser_had_error();
        char err_msg[256];
        if (had_err) snprintf(err_msg, sizeof(err_msg), "%s", parser_error());
        unmute_stderr();

        if (had_err) {
            if (!strncmp(err_msg, "unterminated ", 13)) {
                /* still-open block: keep buffering, wait for more input */
                ft(probe);
                continue;
            }
            /* genuine syntax error: reparse so the parser prints its own
             * precise diagnostic (message + source location), captured and
             * recolored the same way vr_colored() handles runtime errors. */
            Capture c = capture_begin(2);
            Node *bad = parse(pending);
            char *err = capture_end(c);
            emit_colored_stderr(err);
            free(err);
            ft(bad);
            ft(probe);
            free(pending);
            pending = NULL;
            continue;
        }

        if (debug) pt(probe, 0);
        scan_vars(probe);
        merge_fragment(vm->p, probe);
        vm->nc = vm->p->n;
        vp(vm);
        vr_colored(vm);

        free(pending);
        pending = NULL;
    }

    if (hp) { hist_save(hp); free(hp); }
    free(pending);
    vf(vm);
    ft(prog);
    return 0;
}
