// PuTTYrv

static inline float fclampf(float x, float lo, float hi)
{
    return fmaxf(lo, fminf(hi, x));
}

static inline float fminf3(float a, float b, float c)
{
    return fminf(a, fminf(b, c));
}

static inline float fmaxf3(float a, float b, float c)
{
    return fmaxf(a, fmaxf(b, c));
}

static float clamp8tof(int v)
{
    return fclampf(v, 0, 255.0f) / 255.0f;
}

static int clampfto8(float v)
{
    return (int)roundf(fclampf(v, 0, 1) * 255.0f);
}

static void rgb_to_hsl(float r, float g, float b, float *h, float *s, float *l)
{
    float xmax = fmaxf3(r, g, b);
    float xmin = fminf3(r, g, b);
    float c = xmax - xmin;
    float lf = *l = (xmax + xmin) / 2.0f;
    if (c == 0) *h = 0;
    else if (xmax == r) {
        float ht = (g - b) / c;
        if (ht < 0) ht += 6.0f;
        *h = 60.0f * ht;
    } else if (xmax == g)
        *h = 60.0f * ((b - r) / c + 2.0f);
    else
        *h = 60.0f * ((r - g) / c + 4.0f);
    if (lf == 0 || lf == 1.0f) *s = 0;
    else *s = (xmax - lf) / fminf(lf, 1-lf);
}

static void rgb8_to_hsl(int r, int g, int b, float *h, float *s, float *l)
{
    float rf = clamp8tof(r);
    float gf = clamp8tof(g);
    float bf = clamp8tof(b);
    rgb_to_hsl(rf, gf, bf, h, s, l);
}

static float hsl_f(float n, float h30, float l, float a)
{
    float k = fmodf(n + h30, 12.0f);
    float m = fminf(fminf(k - 3.0f, 9.0f - k), 1.0f);
    m = fmaxf(-1.0f, m);
    return l - a * m;
}

static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b)
{
    if (h < 0.0f) h = fmodf(h, 360.0f) + 360.0f;
    s = fclampf(s, 0, 1.0f);
    l = fclampf(l, 0, 1.0f);
    /*
      f(n) = L - a * max(-1, min(k - 3, 9 - k, 1))
        k = (n + H/30) mod 12
        a = S * min(L, 1 - L)
      (R, G, B) = (f(0), f(8), f(4))
    */
    float a = s * fminf(l, 1.0f - l);
    float h30 = h / 30.0f;
    *r = hsl_f(0.0f, h30, l, a);
    *g = hsl_f(8.0f, h30, l, a);
    *b = hsl_f(4.0f, h30, l, a);
}

static void hsl_to_rgb8(float h, float s, float l, int *r, int *g, int *b)
{
    float rf, gf, bf;
    hsl_to_rgb(h, s, l, &rf, &gf, &bf);
    *r = clampfto8(rf);
    *g = clampfto8(gf);
    *b = clampfto8(bf);
}
