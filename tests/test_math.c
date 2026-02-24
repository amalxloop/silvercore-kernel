/*
 * test_math.c  --  Unit tests for sc_math.h
 */
#include "sc_math.h"
#include <stdio.h>
#include <math.h>

#define NEAR(a,b)   (sc_fabsf((float)(a) - (float)(b)) < 1e-4f)
#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name)  printf("  [PASS] %s\n", name)

static int test_vec2(void) {
    SCVec2 a = sc_v2(3, 4);
    FAIL_UNLESS(NEAR(sc_v2_len(a), 5.0f), "v2 length");
    SCVec2 n = sc_v2_norm(a);
    FAIL_UNLESS(NEAR(sc_v2_len(n), 1.0f), "v2 normalise");
    PASS("vec2");
    return 0;
}

static int test_vec3(void) {
    SCVec3 a = sc_v3(1,0,0);
    SCVec3 b = sc_v3(0,1,0);
    SCVec3 c = sc_v3_cross(a, b);
    FAIL_UNLESS(NEAR(c.x, 0) && NEAR(c.y, 0) && NEAR(c.z, 1), "cross product");
    FAIL_UNLESS(NEAR(sc_v3_dot(a,b), 0.0f), "orthogonal dot");
    PASS("vec3");
    return 0;
}

static int test_mat4(void) {
    SCMat4 id = sc_mat4_identity();
    SCVec4 v  = sc_v4(1,2,3,1);
    SCVec4 r  = sc_mat4_mul_vec4(&id, v);
    FAIL_UNLESS(NEAR(r.x,1) && NEAR(r.y,2) && NEAR(r.z,3), "identity transform");

    SCMat4 t  = sc_mat4_translate(5, -3, 0);
    SCVec4 p  = sc_v4(0, 0, 0, 1);
    SCVec4 tp = sc_mat4_mul_vec4(&t, p);
    FAIL_UNLESS(NEAR(tp.x, 5) && NEAR(tp.y, -3) && NEAR(tp.z, 0), "translation");

    PASS("mat4");
    return 0;
}

static int test_ortho(void) {
    /* Ortho: point at left edge → NDC x = -1 */
    SCMat4 proj = sc_mat4_ortho(0, 1280, 720, 0, -1, 1);
    SCVec4 left = sc_v4(0, 360, 0, 1);
    SCVec4 ndc  = sc_mat4_mul_vec4(&proj, left);
    FAIL_UNLESS(NEAR(ndc.x, -1.0f), "ortho left edge");
    PASS("ortho");
    return 0;
}

static int test_lerp(void) {
    FAIL_UNLESS(NEAR(sc_lerpf(0,10,0.5f), 5.0f), "lerp mid");
    FAIL_UNLESS(NEAR(sc_lerpf(0,10,0.0f), 0.0f), "lerp start");
    FAIL_UNLESS(NEAR(sc_lerpf(0,10,1.0f), 10.0f), "lerp end");
    PASS("lerp");
    return 0;
}

int main(void) {
    printf("=== sc_math tests ===\n");
    int fail = 0;
    fail += test_vec2();
    fail += test_vec3();
    fail += test_mat4();
    fail += test_ortho();
    fail += test_lerp();
    if (!fail) printf("All math tests passed.\n");
    return fail;
}
