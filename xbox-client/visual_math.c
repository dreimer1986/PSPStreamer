/* nxdk exp2* are assertion stubs. LLVM may emit them for pow*(2, x),
 * even without an explicit source call. Supply real functions at link time,
 * rather than weakening assertions or changing shared preset formulas. */
#include <math.h>

double exp2(double x)
{
    if (isnan(x)) return x;
    if (x >= 1024.0) return HUGE_VAL;
    if (x <= -1075.0) return 0.0;
    /* Keep exp's argument small and scale exactly by an integer power of two.
     * This also handles subnormals without overflowing an intermediate. */
    double integral = floor(x);
    return ldexp(exp((x - integral) * 0.69314718055994530942), (int)integral);
}

float exp2f(float x)
{
    return (float)exp2((double)x);
}

/* The Xbox ABI uses 64-bit long double. */
long double exp2l(long double x)
{
    return (long double)exp2((double)x);
}

/* Audio smoothing needs expm1f as well. Avoid cancellation around zero. */
double expm1(double x)
{
    if (isnan(x) || x == 0.0) return x;
    if (fabs(x) < 0.5) {
        double term=x, sum=x;
        for (int i=2;i<=20;i++) {
            term *= x/i;
            double next=sum+term;
            if (next==sum) break;
            sum=next;
        }
        return sum;
    }
    /* Materialize exp before subtracting; do not synthesize expm1 recursively. */
    volatile double result=exp(x);
    return result-1.0;
}

float expm1f(float x) { return (float)expm1((double)x); }
long double expm1l(long double x) { return (long double)expm1((double)x); }
