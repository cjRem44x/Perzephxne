/* Reference C implementations, compiled with a normal C compiler, exercising
   every eightbyte-classification shape the ABI wrapper needs to handle:
   - Vector2 (8B, single SSE eightbyte)
   - Vector3 (12B, SSE eightbyte + partial SSE eightbyte)
   - Rectangle (16B, two full SSE eightbytes)
   - Color (4B, single INTEGER eightbyte)
   - Mixed8 (8B, int+float merged into one INTEGER eightbyte)
   - Big20 (20B, exceeds 16 bytes -> MEMORY class, byval/sret)
   - Two64 (16B, two SSE eightbytes carried as plain double)
   Perzephxne's `extern fn` declarations for these must produce results
   identical to what real C code (compiled independently, with no knowledge
   of Perzephxne's internal calling convention) expects. */

typedef struct { float x, y; } Vector2;
typedef struct { float x, y, z; } Vector3;
typedef struct { float x, y, w, h; } Rectangle;
typedef struct { unsigned char r, g, b, a; } Color;
typedef struct { int a; float b; } Mixed8;
typedef struct { int a, b, c, d, e; } Big20;
typedef struct { double a, b; } Two64;

Vector2 vec2_add(Vector2 a, Vector2 b) {
    Vector2 r; r.x = a.x + b.x; r.y = a.y + b.y; return r;
}

Vector3 vec3_sum(Vector3 a, Vector3 b) {
    Vector3 r; r.x = a.x + b.x; r.y = a.y + b.y; r.z = a.z + b.z; return r;
}

Rectangle rect_id(Rectangle a) { return a; }
Color color_id(Color a) { return a; }
Mixed8 mixed8_id(Mixed8 a) { return a; }
Big20 big20_id(Big20 a) { return a; }
Two64 two64_id(Two64 a) { return a; }
