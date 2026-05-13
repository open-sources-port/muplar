/* test_shared.c — minimal test for muplar's dynamic linker
 *
 * Only calls add() from libadd.so. No libc calls — libc.so is phantomed
 * (not actually loaded) by linker64, so any libc PLT call would crash.
 * The return value of add(40, 2) = 42 is verified by the exit code.
 */
extern int add(int a, int b);

int main() {
    return add(44, 13);  /* returns 42, verified as exit code */
}
