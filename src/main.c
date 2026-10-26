#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "sim.h"
#include "evolution.h"

#define RENDER_EVERY_MS 40.0f

static struct termios g_tio_saved;
static int g_tio_raw = 0;
static int g_stdin_flags = -1;

static void term_restore(void)
{
    if (g_tio_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_tio_saved);
        write(STDOUT_FILENO, "\x1b[?25h\x1b[0m\x1b[2J\x1b[H", 17);
        g_tio_raw = 0;
    }
    if (g_stdin_flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, g_stdin_flags);
        g_stdin_flags = -1;
    }
}

static int term_raw(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &g_tio_saved) != 0)
        return -1;
    t = g_tio_saved;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0)
        return -1;
    g_tio_raw = 1;
    return 0;
}

static void sleep_ms(float ms)
{
    struct timespec ts;
    if (ms <= 0.0f)
        return;
    ts.tv_sec = (time_t)(ms / 1000.0f);
    ts.tv_nsec = (long)((ms - (float)ts.tv_sec * 1000.0f) * 1e6f);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    nanosleep(&ts, NULL);
}

/*
 * the frame is built into one static buffer and flushed in a single write,
 * no allocation and no screen tearing
 */
static char grid_ch[WORLD_H][WORLD_W];
static unsigned char grid_col[WORLD_H][WORLD_W];
enum { C_DEFAULT = 0, C_WORM, C_FOOD, C_ODOR, C_CTRL, C_WALL };

static const char* const COL_CODE[] = {
    "\x1b[0m", "\x1b[32m", "\x1b[33m", "\x1b[36m", "\x1b[34m", "\x1b[90m"
};

static void frame_putc(char* frame, size_t cap, size_t* fp, char ch)
{
    if (*fp + 1 < cap)
        frame[(*fp)++] = ch;
}

static void put_cell(int x, int y, char ch, int col)
{
    if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H)
        return;
    grid_ch[y][x] = ch;
    grid_col[y][x] = (unsigned char)col;
}

static void draw_line(int x0, int y0, int x1, int y1, char ch, int col)
{
    /* bresenham, integer only */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2;
        put_cell(x0, y0, ch, col);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void histogram(char* out, size_t cap, const char* label, float v01)
{
    static const int NBARS = 16;
    int n, i;
    size_t p = 0;
    if (v01 < 0.0f) v01 = 0.0f;
    if (v01 > 1.0f) v01 = 1.0f;
    n = (int)(v01 * NBARS + 0.5f);
    p += (size_t)snprintf(out + p, cap - p, "%-5s", label);
    for (i = 0; i < n && p < cap - 2; i++)
        out[p++] = '#';
    for (; p < cap - 1 && i < NBARS; i++)
        out[p++] = '.';
    out[p] = '\0';
}

/* read activation by cached index, -1 means the neuron is not in the net */
static float cell_act(const sim_t* s, int idx)
{
    return idx >= 0 ? neuron_act(&s->ns.cells[idx]) : 0.0f;
}

static void render(sim_t* s, int paused)
{
    static char frame[(WORLD_H + 8) * (WORLD_W + 48) * 8];
    size_t fp = 0;
    int x, y, i;

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            float mmx = ((float)x + 0.5f) * WORLD_CELL_MM;
            float mmy = ((float)y + 0.5f) * WORLD_CELL_MM;
            int rim = (x == 0 || y == 0 || x == WORLD_W - 1 || y == WORLD_H - 1);
            float da = hypotf(mmx - ODOR_A_X_MM, mmy - ODOR_A_Y_MM);
            float db = hypotf(mmx - ODOR_B_X_MM, mmy - ODOR_B_Y_MM);
            if (rim) {
                grid_ch[y][x] = '#';
                grid_col[y][x] = C_WALL;
            } else if (da < 1.0f) {
                grid_ch[y][x] = 'O';
                grid_col[y][x] = C_ODOR;
            } else if (db < 1.0f) {
                grid_ch[y][x] = 'C';
                grid_col[y][x] = C_CTRL;
            } else if (s->world.enable_food && s->world.food[y][x] > 0.35f &&
                       (x + y) % 2 == 0) {
                grid_ch[y][x] = ',';
                grid_col[y][x] = C_FOOD;
            } else {
                grid_ch[y][x] = ' ';
                grid_col[y][x] = C_DEFAULT;
            }
        }
    }

    /* worm skeleton, head at, tail tilde, body line between joints */
    for (i = 0; i < N_SEG; i++) {
        int ax = (int)(s->body.x[i] / WORLD_CELL_MM);
        int ay = (int)(s->body.y[i] / WORLD_CELL_MM);
        int bx = (int)(s->body.x[i + 1] / WORLD_CELL_MM);
        int by = (int)(s->body.y[i + 1] / WORLD_CELL_MM);
        char ch = (i == 0) ? '@' : (i == N_SEG - 1 ? '~' : 'o');
        draw_line(ax, ay, bx, by, ch, C_WORM);
    }

    fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp,
                           "\x1b[H\x1b[2J");
    for (y = 0; y < WORLD_H; y++) {
        int last = -1;
        for (x = 0; x < WORLD_W; x++) {
            if (grid_col[y][x] != last) {
                fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "%s", COL_CODE[grid_col[y][x]]);
                last = grid_col[y][x];
            }
            frame_putc(frame, sizeof(frame), &fp, grid_ch[y][x]);
        }
        if (y == 0) {
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp,
                    "  t %6.1fs %s  spd %6.3fmm/s", s->metrics.t_s,
                    paused ? "PAUSE" : "     ",
                    s->body.speed);
        } else if (y == 1) {
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp,
                    "  ci %+.2f omega %ld rev %ld",
                    s->metrics.chemotaxis_index,
                    s->metrics.n_omega, s->metrics.n_reversals);
        } else if (y == 3) {
            char h[48];
            histogram(h, sizeof(h), "ava", cell_act(s, s->idx_ava));
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        } else if (y == 4) {
            char h[48];
            histogram(h, sizeof(h), "avb", cell_act(s, s->idx_avb));
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        } else if (y == 5) {
            char h[48];
            histogram(h, sizeof(h), "aib", cell_act(s, s->idx_aib));
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        } else if (y == 6) {
            char h[48];
            histogram(h, sizeof(h), "aiy", cell_act(s, s->idx_aiy));
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        } else if (y == 8) {
            char h[48];
            histogram(h, sizeof(h), "5ht", s->mod.ser);
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        } else if (y == 9) {
            char h[48];
            histogram(h, sizeof(h), "da", s->mod.dop);
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        } else if (y == 10) {
            char h[48];
            histogram(h, sizeof(h), "oct", s->mod.oct);
            fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp, "  %s", h);
        }
        frame_putc(frame, sizeof(frame), &fp, '\n');
    }
    fp += (size_t)snprintf(frame + fp, sizeof(frame) - fp,
            "\x1b[0m q quit  p pause  t tap  +/- speed\n");
    fwrite(frame, 1, fp, stdout);
    fflush(stdout);
}

static void usage(FILE* out)
{
    fprintf(out,
        "elegans, whole animal connectome simulator\n"
        "usage: elegans [options]\n"
        "  -H, --headless       batch mode, csv to stdout\n"
        "  -d, --duration S     simulated seconds, default 180\n"
        "  -s, --seed N         rng seed, default 1\n"
        "  -c, --connectome F   varshney csv, Source,Target,Type,Weight[,Sign]\n"
        "  -a, --ablate LIST    comma separated names, prefix match allowed\n"
        "  -A, --ablate-screen  screen every single neuron ablation\n"
        "  -e, --eval F         apply genome json, print fitness\n"
        "  -E, --evolve N       run N generations of evolution\n"
        "  -P, --population N   evolution population size, default 20\n"
        "  -O, --evolve-output F output path for best genome (default: best_genome.json)\n"
        "  -n, --evolve-fitness M  net or path, default path\n"
        "  -T, --tap MS         automatic tap every MS milliseconds\n"
        "  -F, --no-food        remove the bacterial lawn\n"
        "  -G, --thermo         enable thermal gradient\n"
        "  -q, --quiet          no csv rows in headless mode\n"
        "  -h, --help           this text\n");
}

int main(int argc, char** argv)
{
    static sim_t sim;
    sim_opts_t opts;
    int rc = 0;
    int paused = 0;
    float speed_mult = 1.0f;

    memset(&opts, 0, sizeof(opts));
    opts.duration_s = 180.0;
    opts.seed = 1;

    {
        static const struct option lo[] = {
            { "headless", no_argument, NULL, 'H' },
            { "duration", required_argument, NULL, 'd' },
            { "seed", required_argument, NULL, 's' },
            { "connectome", required_argument, NULL, 'c' },
            { "ablate", required_argument, NULL, 'a' },
            { "ablate-screen", no_argument, NULL, 'A' },
            { "eval", required_argument, NULL, 'e' },
            { "evolve", required_argument, NULL, 'E' },
            { "population", required_argument, NULL, 'P' },
            { "evolve-output", required_argument, NULL, 'O' },
            { "evolve-fitness", required_argument, NULL, 'n' },
            { "tap", required_argument, NULL, 'T' },
            { "no-food", no_argument, NULL, 'F' },
            { "thermo", no_argument, NULL, 'G' },
            { "quiet", no_argument, NULL, 'q' },
            { "help", no_argument, NULL, 'h' },
            { NULL, 0, NULL, 0 }
        };
        int ch;
        int have_duration = 0;
        while ((ch = getopt_long(argc, argv, "Hd:s:c:a:Ae:E:P:O:n:T:FGqh", lo, NULL)) != -1) {
            switch (ch) {
            case 'H': opts.headless = 1; break;
            case 'd': opts.duration_s = atof(optarg); have_duration = 1; break;
            case 's': opts.seed = (uint64_t)strtoull(optarg, NULL, 10); break;
            case 'c': opts.connectome_path = optarg; break;
            case 'a': opts.ablate_list = optarg; break;
            case 'A': opts.ablate_screen = 1; opts.headless = 1; break;
            case 'e': opts.eval_path = optarg; opts.headless = 1; break;
            case 'E': opts.evolve_gens = atoi(optarg); opts.headless = 1; break;
            case 'P': opts.population = atoi(optarg); break;
            case 'O': opts.evolve_output = optarg; break;
            case 'n': opts.evolve_fitness_net = (strcmp(optarg, "net") == 0); break;
            case 'T': opts.tap_interval_ms = (float)atof(optarg); break;
            case 'F': opts.no_food = 1; break;
            case 'G': opts.thermo = 1; break;
            case 'q': opts.quiet = 1; break;
            case 'h': usage(stdout); return 0;
            default: usage(stderr); return 1;
            }
        }
        /* conflicting batch modes are a user error, not a precedence rule */
        if (opts.eval_path && opts.evolve_gens > 0) {
            fprintf(stderr, "error: --eval and --evolve are mutually exclusive\n");
            return 1;
        }
        if (opts.ablate_screen && opts.evolve_gens > 0) {
            fprintf(stderr, "error: --ablate-screen and --evolve are mutually exclusive\n");
            return 1;
        }
        /* spec defaults per mode when the user did not pin a duration */
        if (!have_duration) {
            if (opts.evolve_gens > 0)
                opts.duration_s = 300.0;
            else if (opts.ablate_screen)
                opts.duration_s = 60.0;
        }
    }

    if (sim_init(&sim, &opts) != 0) {
        rc = 1;
        goto cleanup;
    }

    if (sim.opts.ablate_screen) {
        sim_ablate_screen(&sim, sim.opts.duration_s);
        goto cleanup;
    }

    if (sim.opts.eval_path) {
        sim_run(&sim, NULL, NULL);
        printf("fitness,%.4f\n", sim_fitness(&sim));
        printf("chemotaxis_index,%.4f\n", sim.metrics.chemotaxis_index);
        printf("mean_speed_mm_s,%.4f\n", sim.metrics.speed_ema);
        printf("omega_turns,%ld\n", sim.metrics.n_omega);
        goto cleanup;
    }

    if (sim.opts.evolve_gens > 0) {
        evolution_t evo;
        int eval_dur = (int)sim.opts.duration_s;
        const char* out_path = sim.opts.evolve_output ? sim.opts.evolve_output : "best_genome.json";
        evolution_init(&evo, sim.opts.seed,
                       sim.opts.population > 0 ? sim.opts.population : 20,
                       sim.opts.evolve_fitness_net);
        evolution_run(&evo, sim.opts.evolve_gens, eval_dur,
                      sim.opts.connectome_path, out_path);
        fprintf(stderr, "evolution complete, best genome saved to %s\n", out_path);
        goto cleanup;
    }

    if (sim.opts.headless) {
        sim_run(&sim, NULL, NULL);
        fprintf(stderr, "final ci %.3f speed %.3f omega %ld reversals %ld\n",
                sim.metrics.chemotaxis_index, sim.metrics.speed_ema,
                sim.metrics.n_omega, sim.metrics.n_reversals);
        goto cleanup;
    }

    /* interactive, refuse to start without a real tty */
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "terminal: stdin is not a tty, use --headless\n");
        rc = 1;
        goto cleanup;
    }
    if (term_raw() != 0) {
        fprintf(stderr, "terminal: raw mode unavailable, use --headless\n");
        rc = 1;
        goto cleanup;
    }
    g_stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
    if (g_stdin_flags == -1 ||
        fcntl(STDIN_FILENO, F_SETFL, g_stdin_flags | O_NONBLOCK) == -1) {
        rc = 1;
        goto cleanup;
    }

    {
        long n_steps = (long)(sim.opts.duration_s * 1000.0 / DT_MS);
        long steps_per_frame = (long)(RENDER_EVERY_MS / DT_MS);
        long frame_step = 0;
        long i;
        for (i = 0; i < n_steps; i++) {
            if (!paused)
                sim_step(&sim);
            frame_step++;
            if (frame_step >= (long)((float)steps_per_frame * speed_mult)) {
                char buf[64];
                frame_step = 0;
                render(&sim, paused);
                /* non blocking buffered drain, one read grabs every pending
                   byte so bursts are not lost between frames */
                for (;;) {
                    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                    if (n > 0) {
                        int k;
                        for (k = 0; k < (int)n; k++) {
                            if (buf[k] == 'q') { term_restore(); goto cleanup; }
                            if (buf[k] == 'p') paused = !paused;
                            if (buf[k] == 't') sim.tap_active_ms = (int)TAP_MS;
                            if (buf[k] == '+') speed_mult *= 1.5f;
                            if (buf[k] == '-') speed_mult /= 1.5f;
                        }
                        continue;
                    }
                    if (n < 0 && errno == EINTR)
                        continue;
                    /* eagain or eof, no pending input */
                    break;
                }
                sleep_ms(RENDER_EVERY_MS / speed_mult);
            }
        }
    }

cleanup:
    term_restore();
    return rc;
}
