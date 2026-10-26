#include <math.h>
#include <stdio.h>
#include <string.h>
#include "body.h"
#include "config.h"

#define DS_MM (BODY_LEN_MM / (float)N_SEG)

/* real ganglion counts per somatic motor class, white 1986 */
static int mn_class_count(int mclass)
{
    switch (mclass) {
    case MC_DB: return 7;
    case MC_VB: return 11;
    case MC_DA: return 9;
    case MC_VA: return 12;
    case MC_DD: return 6;
    case MC_VD: return 13;
    default:    return 1;
    }
}

static int mn_parse_class(const char* name)
{
    /* body wall muscles upper case into dbwml01 and would pass the prefix
       test, a real motor name always has the ganglion index right after
       the class letters */
    if (name[2] < '0' || name[2] > '9')
        return MC_NONE;
    if (strncmp(name, "DB", 2) == 0) return MC_DB;
    if (strncmp(name, "VB", 2) == 0) return MC_VB;
    if (strncmp(name, "DA", 2) == 0) return MC_DA;
    if (strncmp(name, "VA", 2) == 0) return MC_VA;
    if (strncmp(name, "DD", 2) == 0) return MC_DD;
    if (strncmp(name, "VD", 2) == 0) return MC_VD;
    return MC_NONE;
}

void body_build_motor_map(body_t* b, const connectome_t* c, nervous_t* ns)
{
    int i;
    (void)c;
    b->mm.n_mn = 0;
    for (i = 0; i < ns->n; i++) {
        const char* nm = ns->cells[i].name;
        int cls = mn_parse_class(nm);
        int num = 0, k;
        if (cls == MC_NONE)
            continue;
        for (k = 0; nm[k]; k++) {
            if (nm[k] >= '0' && nm[k] <= '9')
                num = num * 10 + (nm[k] - '0');
        }
        if (num <= 0)
            num = 1;
        if (b->mm.n_mn >= MAX_MOTOR_NEURONS)
            break;
        b->mm.idx[b->mm.n_mn] = i;
        /* ganglion index normalized to body coordinate, head is 0 */
        b->mm.pos[b->mm.n_mn] = ((float)num - 0.5f) / (float)mn_class_count(cls);
        b->mm.mclass[b->mm.n_mn] = (uint8_t)cls;
        b->mm.n_mn++;
    }
}

static void build_shape(body_t* b, float head_ang, float* x, float* y)
{
    int i;
    float th = head_ang;
    x[0] = 0.0f;
    y[0] = 0.0f;
    for (i = 0; i < N_SEG; i++) {
        float nx = x[i] + DS_MM * cosf(th);
        float ny = y[i] + DS_MM * sinf(th);
        b->theta[i] = th;
        th += b->k[i] * DS_MM;
        x[i + 1] = nx;
        y[i + 1] = ny;
    }
}

/*
 * b class motor neurons sense the bend a few segments behind their own
 * position, a class senses ahead. the receptor compares the bend across its
 * span instead of sensing absolute curvature: the spatial mean cancels, so
 * a uniform curl gets no feedback and decays, while the traveling contrast
 * of a wave regenerates itself at the right phase. this spatial lag is the
 * whole central pattern generator, no oscillator neuron exists.
 */
void body_proprioception(body_t* b, nervous_t* ns)
{
    int m;
    for (m = 0; m < b->mm.n_mn; m++) {
        int seg = (int)(b->mm.pos[m] * (float)N_SEG);
        int cls = b->mm.mclass[m];
        int src;
        float kontrast, drive = 0.0f;
        neuron_t* c;
        if (seg < 0) seg = 0;
        if (seg > N_SEG - 1) seg = N_SEG - 1;
        if (cls == MC_DB || cls == MC_VB || cls == MC_DD || cls == MC_VD)
            src = seg - PROP_LAG_SEG;
        else
            src = seg + PROP_LAG_SEG;
        if (src < 0) src = 0;
        if (src > N_SEG - 1) src = N_SEG - 1;
        kontrast = b->k[src] - b->k[seg];
        switch (cls) {
        case MC_DB: case MC_DD: drive = kontrast > 0.0f ?  kontrast : 0.0f; break;
        case MC_VB: case MC_VD: drive = kontrast < 0.0f ? -kontrast : 0.0f; break;
        case MC_DA:             drive = kontrast > 0.0f ?  kontrast : 0.0f; break;
        case MC_VA:             drive = kontrast < 0.0f ? -kontrast : 0.0f; break;
        default: drive = 0.0f; break;
        }
        c = &ns->cells[b->mm.idx[m]];
        c->i_prop = PROP_GAIN * drive;
    }
}

/*
 * quasi static force balance on the whole worm at zero reynolds number.
 * unknowns are the rigid frame velocity (vx, vy) and rotation omega of the
 * head anchored frame. each segment feels anisotropic drag, c_n over c_t
 * after gray and lissmann 1964, internal shape change velocity enters the
 * right hand side, the 3x3 system is solved by cramer.
 */
static void force_balance(body_t* b, const float* lx, const float* ly, float dt)
{
    float a[3][3] = {{0}};
    float rhs[3] = {0};
    float det;
    const float c_t = DRAG_T;
    const float c_n = DRAG_T * DRAG_RATIO;
    /* previous shape carried forward into the frame the body has rotated
       into, the difference is then pure bending */
    float c = cosf(b->last_drot), s = sinf(b->last_drot);
    float qx[N_SEG + 1], qy[N_SEG + 1];
    int j;

    for (j = 0; j <= N_SEG; j++) {
        qx[j] = b->px[j] * c - b->py[j] * s;
        qy[j] = b->px[j] * s + b->py[j] * c;
    }

    for (j = 0; j < N_SEG; j++) {
        float rx = 0.5f * (lx[j] + lx[j + 1]);
        float ry = 0.5f * (ly[j] + ly[j + 1]);
        float ct = cosf(b->theta[j]), st = sinf(b->theta[j]);
        /* k = c_t tt^t + c_n nn^t */
        float kxx = c_t * ct * ct + c_n * st * st;
        float kyy = c_t * st * st + c_n * ct * ct;
        float kxy = (c_t - c_n) * ct * st;
        float zx = -ry, zy = rx;
        /* shape velocity of the segment midpoint relative to the head frame */
        float vsx = (0.5f * (lx[j] + lx[j + 1]) - 0.5f * (qx[j] + qx[j + 1])) / dt;
        float vsy = (0.5f * (ly[j] + ly[j + 1]) - 0.5f * (qy[j] + qy[j + 1])) / dt;
        float kzx = kxx * zx + kxy * zy;
        float kzy = kxy * zx + kyy * zy;
        float kvsx = kxx * vsx + kxy * vsy;
        float kvsy = kxy * vsx + kyy * vsy;

        a[0][0] += kxx; a[0][1] += kxy; a[0][2] += kzx;
        a[1][0] += kxy; a[1][1] += kyy; a[1][2] += kzy;
        a[2][0] += kzx; a[2][1] += kzy; a[2][2] += zx * kzx + zy * kzy;
        rhs[0] -= kvsx;
        rhs[1] -= kvsy;
        rhs[2] -= zx * kvsx + zy * kvsy;
    }

    det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
        - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
        + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (fabsf(det) < 1e-7f) {
        b->vx = b->vy = b->omega = 0.0f;
        return;
    }
    /* cramer, u = a^-1 rhs */
    b->vx = ((a[1][1] * a[2][2] - a[1][2] * a[2][1]) * rhs[0]
           - (a[0][1] * a[2][2] - a[0][2] * a[2][1]) * rhs[1]
           + (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * rhs[2]) / det;
    b->vy = (-(a[1][0] * a[2][2] - a[1][2] * a[2][0]) * rhs[0]
           + (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * rhs[1]
           - (a[0][0] * a[1][2] - a[0][2] * a[1][0]) * rhs[2]) / det;
    b->omega = ((a[1][0] * a[2][1] - a[1][1] * a[2][0]) * rhs[0]
             - (a[0][0] * a[2][1] - a[0][1] * a[2][0]) * rhs[1]
             + (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * rhs[2]) / det;
    /* the quasi static solve goes singular when the body straightens out,
       cap the frame rate to what muscle kinetics could ever produce */
    if (b->vx > 0.02f) b->vx = 0.02f;
    if (b->vx < -0.02f) b->vx = -0.02f;
    if (b->vy > 0.02f) b->vy = 0.02f;
    if (b->vy < -0.02f) b->vy = -0.02f;
    if (b->omega > 0.02f) b->omega = 0.02f;
    if (b->omega < -0.02f) b->omega = -0.02f;
}

void body_step(body_t* b, nervous_t* ns, float nmj_gain, float dt)
{
    static float lx[N_SEG + 1], ly[N_SEG + 1];
    float targ_d[N_SEG], targ_v[N_SEG];
    float max_k = K_MAX_INV_MM;
    int m, i;

    /* neuromuscular junction, gaussian spread along the body axis */
    for (i = 0; i < N_SEG; i++) {
        targ_d[i] = 0.0f;
        targ_v[i] = 0.0f;
    }
    for (m = 0; m < b->mm.n_mn; m++) {
        neuron_t* c = &ns->cells[b->mm.idx[m]];
        float a = c->ablated ? 0.0f : neuron_act(c);
        float mn_seg = b->mm.pos[m] * (float)N_SEG;
        int cls = b->mm.mclass[m];
        for (i = 0; i < N_SEG; i++) {
            float d = (float)i - mn_seg;
            float wgt = nmj_gain * expf(-(d * d) / (2.0f * NMJ_SIGMA_SEG * NMJ_SIGMA_SEG));
            /* d class inhibits the opposite side, preventing co contraction spasm */
            if (cls == MC_DB || cls == MC_DA) targ_d[i] += wgt * a;
            else if (cls == MC_VB || cls == MC_VA) targ_v[i] += wgt * a;
            else if (cls == MC_DD) targ_v[i] -= wgt * a;
            else if (cls == MC_VD) targ_d[i] -= wgt * a;
        }
    }

    for (i = 0; i < N_SEG; i++) {
        float alpha = dt / MUSCLE_TAU_MS;
        if (alpha > 1.0f) alpha = 1.0f;
        b->m_d[i] += alpha * (targ_d[i] - b->m_d[i]);
        b->m_v[i] += alpha * (targ_v[i] - b->m_v[i]);
        if (b->m_d[i] < 0.0f) b->m_d[i] = 0.0f;
        if (b->m_d[i] > 1.0f) b->m_d[i] = 1.0f;
        if (b->m_v[i] < 0.0f) b->m_v[i] = 0.0f;
        if (b->m_v[i] > 1.0f) b->m_v[i] = 1.0f;
    }

    /* cuticle viscoelasticity bends each segment toward the muscle imbalance */
    for (i = 0; i < N_SEG; i++) {
        float dk = (dt / TAU_BEND_MS) * (-b->k[i] + BEND_GAIN * (b->m_d[i] - b->m_v[i]));
        b->k[i] += dk;
        if (b->k[i] > max_k) b->k[i] = max_k;
        if (b->k[i] < -max_k) b->k[i] = -max_k;
    }

    /* px,py hold the previous head anchored shape, static lx,ly survive the
       call so they carry it, then build_shape overwrites them with the new one */
    if (b->shape_ready) {
        memcpy(b->px, lx, sizeof(b->px));
        memcpy(b->py, ly, sizeof(b->py));
    }
    build_shape(b, b->head_angle, lx, ly);
    if (!b->shape_ready) {
        memcpy(b->px, lx, sizeof(b->px));
        memcpy(b->py, ly, sizeof(b->py));
        b->shape_ready = 1;
    }

    force_balance(b, lx, ly, dt);

    /* integrate the rigid frame, then rebuild the global skeleton */
    b->head_x += b->vx * dt;
    b->head_y += b->vy * dt;
    b->head_angle += b->omega * dt;
    b->last_drot = b->omega * dt;
    if (b->head_angle > 6.2831853f) b->head_angle -= 6.2831853f;
    if (b->head_angle < 0.0f) b->head_angle += 6.2831853f;

    b->x[0] = b->head_x;
    b->y[0] = b->head_y;
    for (i = 0; i < N_SEG; i++) {
        b->x[i + 1] = b->x[i] + DS_MM * cosf(b->theta[i]);
        b->y[i + 1] = b->y[i] + DS_MM * sinf(b->theta[i]);
    }

    /* wall contact flags are owned by the world, see world_step */

    /* the nose points against the segment chain, forward crawl is positive */
    b->speed = -(b->vx * cosf(b->head_angle) + b->vy * sinf(b->head_angle)) * 1000.0f;

    /* head bend zero crossings give the undulation frequency */
    {
        float kh = b->k[N_SEG / 3];
        int sign = kh > 0.2f ? 1 : (kh < -0.2f ? -1 : 0);
        if (sign != b->k_sign && b->k_sign != 0)
            b->bend_count++;
        b->k_sign = sign;
    }
}

float body_signed_speed(const body_t* b)
{
    return b->speed;
}

void body_init(body_t* b)
{
    int i;
    memset(b, 0, sizeof(*b));
    b->head_x = 6.0f;
    b->head_y = 8.0f;
    b->head_angle = 0.0f;
    /* tiny seed asymmetry, neural noise amplifies it into the wave */
    for (i = 0; i < N_SEG; i++)
        b->k[i] = 0.25f * cosf((float)i * 0.5f);
    b->k_sign = 0;
}
