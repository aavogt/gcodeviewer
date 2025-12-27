#include "raylib.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIN(a, b)                                                              \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a < _b ? _a : _b;                                                         \
  })
#define MAX(a, b)                                                              \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a > _b ? _a : _b;                                                         \
  })

char *c, *cend, *c0;

typedef struct {
  float x, y, z, e;
} P;
P ps[2];

static bool rel[4] = {false, false, false, false}; // X,Y,Z,E relative flags

static inline int isspace_ascii(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r';
}

void advance_ps_reset() {
  c = c0;
  ps[1] = (P){0, 0, 0, 0};
  for (int i = 0; i < 4; i++)
    rel[i] = 0;
}

bool advance_ps() {
  ps[0] = ps[1];

  if (c >= cend)
    return false;

  while (c < cend) {
    if (*c == '\n' || c == c0) {
      char *q = c + 1;

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
                        case 'E': ps[1].e = rel[3] ? ps[0].e + f : f; break;
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

P ps_max, ps_min, ps_avg, ps_trim;
#define NTRIM 200
static float sorting[NTRIM];
static float trimmed_avg(size_t off) {
  advance_ps_reset();
  int n = 0;
  while (n < NTRIM && advance_ps()) {
    float v = *(float *)((char *)&ps[1] + off);
    int j = n;
    while (j > 0 && sorting[j - 1] > v) {
      sorting[j] = sorting[j - 1];
      j--;
    }
    sorting[j] = v;
    n++;
  }
  if (n > 2) {
    float sum = 0.0f;
    for (int j = 1; j < n - 1; j++)
      sum += sorting[j];
    return sum / (float)(n - 2);
  } else if (n > 0) {
    float sum = 0.0f;
    for (int j = 0; j < n; j++)
      sum += sorting[j];
    return sum / (float)n;
  } else {
    return 0.0f;
  }
}

void gcode_bbox() {
  c = c0;
  int n = 0;
  size_t o[4] = {offsetof(P, x), offsetof(P, y), offsetof(P, z),
                 offsetof(P, e)};

#define FIELDF(p, off) (*(float *)((char *)(p) + (off)))

  advance_ps_reset();
  while (advance_ps()) {
    ps_max.x = MAX(ps_max.x, ps[1].x);
    ps_min.x = MIN(ps_min.x, ps[1].x);
    ps_max.y = MAX(ps_max.y, ps[1].y);
    ps_min.y = MIN(ps_min.y, ps[1].y);
    ps_max.z = MAX(ps_max.z, ps[1].z);
    ps_min.z = MIN(ps_min.z, ps[1].z);
    ps_max.e = MAX(ps_max.e, ps[1].e);
    ps_min.e = MIN(ps_min.e, ps[1].e);
    for (int i = 0; i < 4; i++)
      FIELDF(&ps_avg, o[i]) = FIELDF(&ps[1], o[i]);
    n++;
  }
  for (int i = 0; i < 4; i++)
    FIELDF(&ps_avg, o[i]) /= n;

  for (int i = 0; i < 4; i++)
    FIELDF(&ps_trim, o[i]) = trimmed_avg(o[i]);
}

static bool scene_dirty = true;
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
    if (statbuf_old.st_mtim.tv_sec <= statbuf.st_mtim.tv_sec &&
        statbuf_old.st_mtim.tv_nsec < statbuf.st_mtim.tv_nsec) {
      munmap(c0, cend - c0);
    } else {
      close(fd);
      return;
    }
  }
  c0 = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  scene_dirty = true;
  cend = c0 + statbuf.st_size;
  statbuf_old = statbuf;
  c = c0;
}

int main(int argc, char **argv) {
  if (argc == 1 || (argc >= 2 && (0 == strcmp(argv[1], "-h") ||
                                  0 == strcmp(argv[1], "--help")))) {
    printf("usage: %s file.gcode\n", argv[0]);
    printf("\n\tq ESC quit\n\tleft mouse drag rotate view\n");
    exit(0);
  }
  mmapfile(argv[1]);
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
  Camera3D prev_camera;

  // Render-to-texture cache for the 3D scene
  static RenderTexture2D rt;
  static bool rt_inited = false;

  int nframe = 0;

  while (!WindowShouldClose() && !IsKeyReleased(KEY_Q) &&
         !IsKeyReleased(KEY_ESCAPE)) {
    nframe++;
    nframe = nframe % 20;
    if (nframe)
      mmapfile(argv[1]); // check mtime and reload

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
      UpdateCamera(&camera, CAMERA_THIRD_PERSON);

    // Create/recreate render target on first use or window resize
    if (!rt_inited || IsWindowResized()) {
      if (rt_inited)
        UnloadRenderTexture(rt);
      rt = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
      scene_dirty = true;
      rt_inited = true;
    }

    // Rebuild the cached texture only when camera changes
    bool camera_changed = memcmp(&camera, &prev_camera, sizeof(Camera3D)) != 0;
    if (camera_changed || scene_dirty) {
      BeginTextureMode(rt);
      ClearBackground(BLACK);
      BeginMode3D(camera);

      advance_ps_reset();
      while (advance_ps()) {
        DrawLine3D((Vector3){ps[0].x, ps[0].y, ps[0].z},
                   (Vector3){ps[1].x, ps[1].y, ps[1].z},
                   ps[0].e < ps[1].e ? BLUE : YELLOW);
      }

      EndMode3D();
      EndTextureMode();
      prev_camera = camera;
      scene_dirty = false;
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
