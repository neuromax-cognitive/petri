#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "sim.h"
#include "tui.h"
#include "evolution.h"

#define RENDER_EVERY_MS 33.0f

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
        "  -C, --conditioning M   paired or control, training protocol\n"
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
    int have_duration = 0;
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
            { "conditioning", required_argument, NULL, 'C' },
            { "tap", required_argument, NULL, 'T' },
            { "no-food", no_argument, NULL, 'F' },
            { "thermo", no_argument, NULL, 'G' },
            { "quiet", no_argument, NULL, 'q' },
            { "help", no_argument, NULL, 'h' },
            { NULL, 0, NULL, 0 }
        };
        int ch;
        while ((ch = getopt_long(argc, argv, "Hd:s:c:a:Ae:E:P:O:n:C:T:FGqh", lo, NULL)) != -1) {
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
            case 'C':
                opts.conditioning = strcmp(optarg, "paired") == 0 ? 1 :
                                    (strcmp(optarg, "control") == 0 ? 2 : 0);
                opts.headless = 1;
                break;
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

    /* interactive dashboard, refuse to start without a real tty */
    if (!have_duration) {
        /* an interactive session runs until q, not until the batch timer
           runs out, the batch default only exists for the scripted modes */
        opts.duration_s = 1.0e9;
    }
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "terminal: stdin is not a tty, use --headless\n");
        rc = 1;
        goto cleanup;
    }
    if (tui_enter() != 0 || tui_stdin_nonblock() != 0) {
        fprintf(stderr, "terminal: raw mode unavailable, use --headless\n");
        rc = 1;
        goto cleanup;
    }

    fprintf(stderr, "dashboard: q quit, p pause, t tap, f food, +/- sim speed\n");
    {
        static tui_t tui;
        long n_steps = (long)(sim.opts.duration_s * 1000.0 / DT_MS);
        long steps_per_frame = (long)(RENDER_EVERY_MS / DT_MS);
        long frame_step = 0;
        long i;
        memset(&tui, 0, sizeof(tui));
        tui.compact = tui_wants_compact();
        for (i = 0; i < n_steps; i++) {
            if (!paused)
                sim_step(&sim);
            frame_step++;
            if (frame_step >= (long)((float)steps_per_frame * speed_mult)) {
                char buf[64];
                frame_step = 0;
                tui_render(&tui, &sim, paused, speed_mult);
                /* non blocking buffered drain, one read grabs every pending
                   byte so bursts are not lost between frames */
                for (;;) {
                    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                    if (n > 0) {
                        int k;
                        for (k = 0; k < (int)n; k++) {
                            if (buf[k] == 'q') { tui_leave(); goto cleanup; }
                            if (buf[k] == 'p') paused = !paused;
                            if (buf[k] == 't') sim.tap_active_ms = (int)TAP_MS;
                            if (buf[k] == 'f') sim.world.enable_food = !sim.world.enable_food;
                            if (buf[k] == '+') speed_mult *= 1.5f;
                            if (buf[k] == '-') speed_mult /= 1.5f;
                            if (speed_mult > 16.0f) speed_mult = 16.0f;
                            if (speed_mult < 0.25f) speed_mult = 0.25f;
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
        if (n_steps < (1L << 40))
            fprintf(stderr, "duration reached\n");
    }

cleanup:
    tui_leave();
    return rc;
}
