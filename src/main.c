#include "raylib.h"
#include "raymath.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "segdistance.h"

/// get the area between segment p0-p1 and segment q0-q1
/// what
double segmentDistance(Vector3 p0, Vector3 p1, Vector3 q0, Vector3 q1) {
  // d = p0-q0
  // dd = (p1-p0) - (q1-q0)
  Vector3 d = Vector3Subtract(p0, q0), dp = Vector3Subtract(p1, p0),
          dq = Vector3Subtract(q1, q0), dd = Vector3Subtract(dp, dq);
  Quaternion q = QuaternionFromVector3ToVector3(dd, (Vector3){1, 0, 0});
  Vector3 acd = Vector3RotateByQuaternion(d, q);
  double r; // f1 special case return double instea
  f1(acd.x, Vector3Length(dd), acd.y, acd.z, &r);
  return r;
}

char *c, *cend, *c0;

Vector4 ps[2];

static bool rel[4] = {false, false, false, false}; // X,Y,Z,E relative flags

static inline int isspace_ascii(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r';
}

void advance_ps_reset() {
  c = c0;
  ps[1] = (Vector4){0, 0, 0, 0};
  for (int i = 0; i < 4; i++)
    rel[i] = 0;
}

bool advance_ps() {
  ps[0] = ps[1];

  if (c >= cend)
    return false;

  while (c < cend) {
    if (*c == '\n' || c == c0) {
      char *q = (c == c0) ? c : c + 1;

      // find end of line
      char *line_end = q;
      while (line_end < cend && *line_end != '\n')
        ++line_end;

      // semicolon is the comment or end of line
      char *semicolon = q;
      while (semicolon < cend && *semicolon != '\n' && *semicolon != ';')
        ++semicolon;

      // command at start of line?
      if (q < semicolon && (q[0] == 'G' || q[0] == 'M')) {
        char cmd = q[0];
        char *num_end;
        double gval_d = strtod(q + 1, &num_end);
        if (num_end != (q + 1)) {
          int gval = (int)gval_d;

          if (cmd == 'G') {
            switch (gval) {
              // clang-format off
              case 90: // G90: all absolute
                rel[0] = rel[1] = rel[2] = rel[3] = false;
                c = line_end;
                continue;
              case 91: // G91: all relative
                rel[0] = rel[1] = rel[2] = rel[3] = true;
                c = line_end;
                continue;
            // clang-format on
            case 1: { // G1: move
              ps[1] = ps[0];

              char *s = num_end; // after the numeric part of "G1"
              while (s < semicolon) {
                while (s < semicolon && isspace_ascii(*s))
                  ++s;
                if (s >= semicolon)
                  break;

                char axis = *s;
                if (axis == 'X' || axis == 'Y' || axis == 'Z' || axis == 'E') {
                  char *num_start = s + 1;
                  char *num_end_f;
                  float f = strtof(num_start, &num_end_f);
                  if (num_end_f != num_start) {
                    // clang-format off
                      switch (axis) {
                        case 'X': ps[1].x = rel[0] ? ps[0].x + f : f; break;
                        case 'Y': ps[1].y = rel[1] ? ps[0].y + f : f; break;
                        case 'Z': ps[1].z = rel[2] ? ps[0].z + f : f; break;
                        case 'E': ps[1].w = rel[3] ? ps[0].w + f : f; break;
                      }
                    // clang-format on
                    s = num_end_f;
                    continue;
                  }
                }
                // skip unknown token
                while (s < semicolon && !isspace_ascii(*s))
                  ++s;
              }

              c = line_end; // advance cursor to end of parsed line
              return true;
            }
            default:
              break;
            }
          } else if (cmd == 'M') {
            switch (gval) {
            case 82: // M82: E absolute
              rel[3] = false;
              c = line_end;
              continue;
            case 83: // M83: E relative
              rel[3] = true;
              c = line_end;
              continue;
            default:
              break;
            }
          }
        }
      }

      // not a recognized command; skip line
      c = line_end;
      continue;
    }
    ++c;
  }
  c = cend; // no more commands
  return false;
}

void write_csv(char *path) {
  FILE *h = fopen(path, "w");

  fprintf(h, "x,y,z,e,x2,y2,z2,e2\n");
  advance_ps_reset();
  while (advance_ps()) {
    fprintf(h, "%f,%f,%f,%f,%f,%f,%f,%f\n", ps[0].x, ps[0].y, ps[0].z, ps[0].w,
            ps[1].x, ps[1].y, ps[1].z, ps[1].w);
  }
}

Vector4 ps_max, ps_min, ps_avg, ps_trim;
#define NTRIM 200
static float sorting[NTRIM];
static float trimmed_avg(size_t off) {
  advance_ps_reset();
  int n = 0;
  float sum = 0;
  for (n = 0; n < NTRIM && advance_ps(); n++) {

#define FIELDF(p, off) (*(float *)((char *)(p) + (off)))

    float v = FIELDF(&ps[1], off);
    int j = n;
    for (; j > 0 && sorting[j - 1] > v; j--) {
      sorting[j] = sorting[j - 1];
    }
    sorting[j] = v;
  }

  n = 0;
  while (advance_ps()) {
    float v = FIELDF(&ps[1], off);
    float a = sorting[NTRIM / 2 - 1];
    float b = sorting[NTRIM / 2];
    if (v >= a && v <= b) {
      // no need to shift
      sum += v;
      n++;
      // printf("keep %f<%f<%f\n", a, v, b);
    }
    if (v < a) {
      // shift left inserting v
      int j = 1;
      for (; j < NTRIM && sorting[j] < v; j++) {
        sorting[j - 1] = sorting[j];
      }
      // printf("shift left into %d %f\n", j, v);
      sorting[j] = v;
    }
    if (v > b) {
      // shift right inserting v
      int j = NTRIM;
      for (; j > 0 && sorting[j] > v; j--) {
        sorting[j] = sorting[j - 1];
      }
      sorting[j - 1] = v;
      // printf("shift right into %d %f\n", j - 1, v);
    }
  }
  printf("%f\n", sum / n);
}

void gcode_bbox() {
  c = c0;
  int n = 0;
  size_t o[4] = {offsetof(Vector4, x), offsetof(Vector4, y),
                 offsetof(Vector4, z), offsetof(Vector4, w)};

  advance_ps_reset();
  while (advance_ps()) {
    ps_max = Vector4Max(ps_max, ps[1]);
    ps_min = Vector4Min(ps_min, ps[1]);
    ps_avg = Vector4Add(ps_avg, ps[1]);
    n++;
  }
  ps_avg = Vector4Divide(ps_avg, (Vector4){n, n, n, n});

  for (int i = 0; i < 4; i++)
    FIELDF(&ps_trim, o[i]) = trimmed_avg(o[i]);
}

static bool dirty = true;
struct stat statbuf_old;
/// mmap or munmap/mmap the given file,
/// depending on mtime
/// store the beginning at c0, end at cend
/// c = c0
void mmapfile(char *file) {
  // mmap file
  int fd = open(file, O_RDONLY);
  struct stat statbuf;
  fstat(fd, &statbuf);
  if (c0) {
    bool newer = (statbuf.st_mtim.tv_sec > statbuf_old.st_mtim.tv_sec) ||
                 (statbuf.st_mtim.tv_sec == statbuf_old.st_mtim.tv_sec &&
                  statbuf.st_mtim.tv_nsec > statbuf_old.st_mtim.tv_nsec);
    if (newer) {
      munmap(c0, cend - c0);
    } else {
      close(fd);
      return;
    }
  }
  c0 = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  dirty = true;
  close(fd);
  cend = c0 + statbuf.st_size;
  statbuf_old = statbuf;
  c = c0;
}

int main(int argc, char **argv) {
  if (argc == 1 || (argc >= 2 && (0 == strcmp(argv[1], "-h") ||
                                  0 == strcmp(argv[1], "--help")))) {
    printf("usage: %s file.gcode\n", argv[0]);
    printf("\n\tq ESC quit\n\tleft mouse drag rotates the view\n");
    printf("\tright mouse drag pans the view\n");
    printf("\tmouse wheel drag zooms the view\n");

    exit(0);
  }
  mmapfile(argv[1]);
  write_csv("out.csv");
  advance_ps_reset();

  gcode_bbox();

  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "gcodeviewer");
  SetTargetFPS(60);

  const float fac = 0.2;
  Camera3D camera = {.position =
                         (Vector3){fac * (ps_max.x - ps_min.x) + ps_trim.x,
                                   fac * (ps_max.y - ps_min.y) + ps_trim.y,
                                   fac * (ps_max.z - ps_min.z) + ps_trim.z},
                     .fovy = 90,
                     .target = (Vector3){ps_trim.x, ps_trim.y, ps_trim.z},
                     .up = (Vector3){0, 0, 1},
                     .projection = CAMERA_ORTHOGRAPHIC};

  // Render-to-texture cache for the 3D scene
  static RenderTexture2D rt;
  static bool rt_inited = false;

  while (!WindowShouldClose() && !IsKeyPressed(KEY_Q) &&
         !IsKeyPressed(KEY_ESCAPE)) {
    {
      static int n = 0;
      n++;
      n = n % 20;
      if (n)
        mmapfile(argv[1]); // check mtime and reload
    }

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
      // rotate
      UpdateCamera(&camera, CAMERA_THIRD_PERSON);
      dirty += Vector2LengthSqr(GetMouseDelta()) > 0;
    }
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
      // pan
      Vector2 p = GetMousePosition();
      Vector2 q = Vector2Subtract(p, GetMouseDelta());
      Vector3 r = GetScreenToWorldRay(p, camera).position;
      Vector3 s = GetScreenToWorldRay(q, camera).position;
      Vector3 d = Vector3Subtract(s, r);
      camera.position = Vector3Add(camera.position, d);
      camera.target = Vector3Add(camera.target, d);
      dirty = true;
    }
    {
      // zoom
      float f = GetMouseWheelMove();
      camera.fovy = Clamp(camera.fovy / (1 + f / 6) - 7 * f, 20, 120);
      dirty += fabsf(f) > 0;
    }

    // Create/recreate render target on first use or window resize
    if (!rt_inited || IsWindowResized()) {
      if (rt_inited)
        UnloadRenderTexture(rt);
      rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
      dirty = true;
      rt_inited = true;
    }

    // Rebuild the cached texture only when camera changes
    if (dirty) {
      BeginTextureMode(rt);
      ClearBackground(BLACK);
      BeginMode3D(camera);

      advance_ps_reset();
      while (advance_ps()) {
        DrawLine3D((Vector3){ps[0].x, ps[0].y, ps[0].z},
                   (Vector3){ps[1].x, ps[1].y, ps[1].z},
                   ps[0].w < ps[1].w ? BLUE : YELLOW);
      }

      EndMode3D();
      EndTextureMode();
      dirty = false;
    }

    // Draw cached texture to the screen
    BeginDrawing();
    ClearBackground(BLACK);
    Rectangle src = (Rectangle){0, 0, (float)rt.texture.width,
                                -(float)rt.texture.height}; // flip vertically
    Rectangle dst =
        (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
    DrawTexturePro(rt.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
    EndDrawing();
  }
  UnloadRenderTexture(rt);
  CloseWindow();
}
