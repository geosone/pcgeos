/*
 * testgeode.c -- a geode for the ghal_module test (Port32/Tests).
 *
 * Built in the host's module format by cmake/GeosGeode.cmake (ELF shared
 * object or DXE3) and loaded by test_ghal_module.c, which plays the
 * kernel: it provides the imports KernelAdd, kernelCounter and
 * kernelName.  Covers calls and data references to imports, pointers in
 * data, a function pointer, bss, initializers and finalizers, and 64-bit
 * division (libgcc on 32-bit hosts).  No host C library.
 */

/* imports from the kernel */
extern int KernelAdd(int a, int b);
extern int kernelCounter;
extern const char kernelName[];

static int initRan;
static int bssZero[64];
int dataValue = 42;
static const char *const strings[] = { "alpha", "beta" };

static int Twice(int x)
{
    return 2 * x;
}

int (*const twicePtr)(int) = Twice;
int *const counterPtr = &kernelCounter;

__attribute__((constructor)) static void GeodeInit(void)
{
    initRan = 1;
    kernelCounter += 100;
}

__attribute__((destructor)) static void GeodeFini(void)
{
    kernelCounter += 1000;
}

int GeodeInitRan(void) { return initRan; }
int GeodeCall(int a, int b) { return KernelAdd(a, b) + Twice(1); }
const char *GeodeString(int i) { return strings[i]; }
int GeodeTwice(int x) { return twicePtr(x); }
int *GeodeCounterPtr(void) { return counterPtr; }
const char *GeodeKernelName(void) { return kernelName; }
int GeodeData(void) { return dataValue++; }

int GeodeBssSum(void)
{
    int i, s = 0;

    for (i = 0; i < 64; i++)
        s += bssZero[i];
    return s;
}

unsigned long long GeodeDiv(unsigned long long a, unsigned long long b)
{
    return a / b;
}
