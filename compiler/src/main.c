#include "arena.h"
#include "error.h"
#include "parser.h"
#include "sema.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

/* ── Utilities ────────────────────────────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "przp: cannot open '%s': %s\n", path, strerror(errno)); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fprintf(stderr, "przp: out of memory\n"); exit(1); }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* strip extension, return basename without it */
static void basename_no_ext(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    const char *base  = slash ? slash + 1 : path;
    const char *dot   = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

/* compile one .przp file → .ll → binary via clang */
static int compile_file(const char *src_path, const char *out_path, int release) {
    char *src = read_file(src_path);

    Arena arena;
    arena_init(&arena);

    error_init(src_path, src);
    Module *mod = parse(src, 0, &arena);
    if (!sema_check(mod)) { arena_free(&arena); free(src); return 1; }

    /* write .ll to a temp file next to the source */
    char ll_path[1024];
    snprintf(ll_path, sizeof(ll_path), "/tmp/przp_%d.ll", (int)getpid());

    FILE *ll_f = fopen(ll_path, "w");
    if (!ll_f) { fprintf(stderr, "przp: cannot write '%s'\n", ll_path); return 1; }
    int ok = codegen(mod, ll_f);
    fclose(ll_f);

    arena_free(&arena);
    free(src);

    if (!ok) { remove(ll_path); return 1; }

    /* invoke clang to produce the binary */
    char cmd[2048];
    const char *opt = release ? "-O2" : "-O0 -g";
    snprintf(cmd, sizeof(cmd), "clang %s %s -o %s", opt, ll_path, out_path);
    int ret = system(cmd);
    remove(ll_path);
    return (ret == 0) ? 0 : 1;
}

/* ── Sub-commands ─────────────────────────────────────────────────────────── */

static void cmd_sac(int argc, char **argv) {
    /* przp sac <files...> [-o=Name] [--release] */
    const char *out_name = "out";
    int release = 0;
    const char **files = malloc(sizeof(char*) * (size_t)argc);
    int nfiles = 0;

    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "-o=", 3)) { out_name = argv[i] + 3; continue; }
        if (!strcmp(argv[i], "--release")) { release = 1; continue; }
        files[nfiles++] = argv[i];
    }

    if (nfiles == 0) { fprintf(stderr, "przp sac: no input files\n"); free(files); exit(1); }

    /* For now: single-file only.  Multi-file linking is future work. */
    int rc = compile_file(files[0], out_name, release);
    free(files);
    exit(rc);
}

static void cmd_init(int argc, char **argv) {
    const char *name = (argc > 0) ? argv[0] : "myproject";

    /* create directory layout */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/src", name);
    system(cmd);

    /* przp.toml */
    char toml_path[256];
    snprintf(toml_path, sizeof(toml_path), "%s/przp.toml", name);
    FILE *f = fopen(toml_path, "w");
    if (f) {
        fprintf(f, "[package]\nname = \"%s\"\nversion = \"0.1.0\"\n\n[build]\nentry = \"src/main.przp\"\n", name);
        fclose(f);
    }

    /* src/main.przp */
    char main_path[256];
    snprintf(main_path, sizeof(main_path), "%s/src/main.przp", name);
    f = fopen(main_path, "w");
    if (f) {
        fprintf(f, "fn main() -> i32 {\n    @pf(\"Hello from %s!\\n\")\n    ret 0\n}\n", name);
        fclose(f);
    }

    printf("Created project '%s'\n", name);
    exit(0);
}

static const char *find_entry(void) {
    /* look for przp.toml, parse entry = "..." */
    FILE *f = fopen("przp.toml", "r");
    if (!f) return NULL;
    static char entry[256];
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " entry = \"%255[^\"]\"", entry) == 1) {
            fclose(f);
            return entry;
        }
    }
    fclose(f);
    return "src/main.przp";
}

static void cmd_build(int argc, char **argv) {
    int release = 0;
    const char *out_name = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--release")) release = 1;
        if (!strncmp(argv[i], "-o=", 3))  out_name = argv[i] + 3;
    }

    const char *entry = find_entry();
    if (!entry) { fprintf(stderr, "przp build: no przp.toml or entry point found\n"); exit(1); }

    char out_buf[256] = "out";
    if (out_name) {
        snprintf(out_buf, sizeof(out_buf), "%s", out_name);
    } else {
        /* derive output name from entry filename */
        basename_no_ext(entry, out_buf, sizeof(out_buf));
    }

    int rc = compile_file(entry, out_buf, release);
    exit(rc);
}

static void cmd_run(int argc, char **argv) {
    int release = 0;
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--release")) release = 1;

    const char *entry = find_entry();
    if (!entry) { fprintf(stderr, "przp run: no przp.toml or entry point found\n"); exit(1); }

    char out_buf[256];
    basename_no_ext(entry, out_buf, sizeof(out_buf));

    if (compile_file(entry, out_buf, release) != 0) exit(1);

    char run_cmd[512];
    snprintf(run_cmd, sizeof(run_cmd), "./%s", out_buf);
    int rc = system(run_cmd);
    exit(WEXITSTATUS(rc));
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

static void usage(void) {
    fprintf(stderr,
        "Usage: przp <command> [options]\n"
        "\n"
        "Commands:\n"
        "  init [name]           Create a new project\n"
        "  build [--release]     Build the project\n"
        "  run   [--release]     Build and run the project\n"
        "  sac <files> [-o=Out]  Compile individual files\n"
    );
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) usage();

    const char *cmd = argv[1];
    argv += 2;
    argc -= 2;

    if (!strcmp(cmd, "init"))  cmd_init(argc, argv);
    if (!strcmp(cmd, "build")) cmd_build(argc, argv);
    if (!strcmp(cmd, "run"))   cmd_run(argc, argv);
    if (!strcmp(cmd, "sac"))   cmd_sac(argc, argv);

    fprintf(stderr, "przp: unknown command '%s'\n", cmd);
    usage();
}
