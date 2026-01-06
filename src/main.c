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
#include "selected.h"

// maximum output csv filename length
#define NFILENAME 200

Vector3 Vector4To3(Vector4 a) { return (Vector3){a.x, a.y, a.z}; }

/// get the area between segment p0-p1 and segment q0-q1
double SegmentDistance(Vector3 p0, Vector3 p1, Vector3 q0, Vector3 q1) {
  // d = p0-q0
  // dd = (p1-p0) - (q1-q0)
  Vector3 d = Vector3Subtract(p0, q0), dp = Vector3Subtract(p1, p0),
          dq = Vector3Subtract(q1, q0), dd = Vector3Subtract(dp, dq);
  Quaternion q = QuaternionFromVector3ToVector3(dd, (Vector3){1, 0, 0});
  Vector3 acd = Vector3RotateByQuaternion(d, q);
  return f1(acd.x, Vector3Length(dd), acd.y, acd.z);
}

double SegmentDistance4(Vector4 ps[2], Vector4 qs[2]) {
  return SegmentDistance(Vector4To3(ps[0]), Vector4To3(ps[1]),
                         Vector4To3(qs[0]), Vector4To3(qs[1]));
}

char *c, *cend, *c0;

Vector4 ps[2];

bool rel[4]; // X,Y,Z,E relative flags

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
            case 0: case 1: { // G0 move G1: extrude move
              // clang-format on
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
    }
    if (v < a) {
      // shift left inserting v
      int j = 1;
      for (; j < NTRIM && sorting[j] < v; j++) {
        sorting[j - 1] = sorting[j];
      }
      sorting[j] = v;
    }
    if (v > b) {
      // shift right inserting v
      int j = NTRIM;
      for (; j > 0 && sorting[j] > v; j--) {
        sorting[j] = sorting[j - 1];
      }
      sorting[j - 1] = v;
    }
  }
  return sum / n;
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

// is this the operation that'll make me give up on advance_ps
// and instead allocate a buffer of floats with the points that's easier to
// random access?
Vector4 qs[2];
bool sel[4];
char *d, *d0, *dend;
void swapcd() {
  for (size_t i = 0; i < 2; ++i) {
    Vector4 tmp = qs[i];
    qs[i] = ps[i];
    ps[i] = tmp;
  }
  for (size_t i = 0; i < 4; ++i) {
    bool tmpb = sel[i];
    sel[i] = rel[i];
    rel[i] = tmpb;
  }
  char *t0 = c0;
  c0 = d0;
  d0 = t0;
  char *te = cend;
  cend = dend;
  dend = te;
}
bool advance_qs() {
  swapcd();
  bool r = advance_ps();
  swapcd();
  return r;
};
void advance_qs_reset() {
  d = d0;
  qs[1] = (Vector4){0, 0, 0, 0};
  for (int i = 0; i < 4; i++)
    sel[i] = 0;
}

// the old gcode file is d, d0, dend, qs, advance_qs()
// the new gcode file is c, c0, cend, ps, advance_ps()
// try to update selected[] so that the new indexes
// are as close as possible
// TODO: lines can break apart or combine
// SegmentDistance can't take polylines
// I need a different distance calculation which
// allows one line to be bare with some value
// returned indicating which sides are uncovered.
// In other words the result of a single call to
// SegmentDistance4Growable() will be like
// an intersection of intervals.
// TODO ps and qs sequences are usually sorted
// by z so there's no need to look far
void selected_refresh() {
  advance_qs_reset();
  int i = 0;
  while (advance_qs()) {
    int j = selected_index(i);
    if (j != SELECTED_EMPTY) {
      double dmin = INFINITY;
      int k = 0, kmin = 0;
      advance_ps_reset();
      while (advance_ps()) {
        double d = SegmentDistance4(ps, qs);
        if (d < dmin) {
          // XXX k not already taken but how do I check?
          // selected[] has old and new mixed up
          kmin = k;
        }
        k++;
      }
      selected[j] = kmin;
    }
    i++;
  }
}

struct stat statbuf_old;
/// mmap or munmap/mmap the given file,
/// depending on mtime
/// store the beginning at c0, end at cend
/// c = c0
bool mmapfile(char *file) {
  // mmap file
  int fd = open(file, O_RDONLY);
  struct stat statbuf;
  fstat(fd, &statbuf);
  if (c0) {
    bool newer = (statbuf.st_mtim.tv_sec > statbuf_old.st_mtim.tv_sec) ||
                 (statbuf.st_mtim.tv_sec == statbuf_old.st_mtim.tv_sec &&
                  statbuf.st_mtim.tv_nsec > statbuf_old.st_mtim.tv_nsec);
    if (newer) {
      swapcd();

      c0 = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      close(fd);
      cend = c0 + statbuf.st_size;
      statbuf_old = statbuf;
      c = c0;

      selected_refresh();

      munmap(d0, dend - d0);
      return true;
    } else {
      close(fd);
      return false;
    }
  }
  c0 = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  cend = c0 + statbuf.st_size;
  statbuf_old = statbuf;
  c = c0;
  return true;
}

/// distance between a ray and a line segment
double DistanceToRay(Ray q, Vector3 f, Vector3 t) {
  Quaternion mkq =
      QuaternionFromVector3ToVector3(q.direction, (Vector3){1, 0, 0});
  Vector3 ft = Vector3Subtract(f, t), qt = Vector3Subtract(q.position, t);
  ft = Vector3RotateByQuaternion(ft, mkq);
  qt = Vector3RotateByQuaternion(qt, mkq);
  return f2(qt.x, qt.y, qt.z, ft.x, ft.y, ft.z);
}

// calipers
// snap view?
// two GetScreenToWorldRay

typedef enum {
  CLOSEST_SKIP_SELECTED = 1,
  CLOSEST_ONLY_SELECTED = 2,
  CLOSEST_SKIP_G1 = 4,
  CLOSEST_SKIP_G0 = 8,
} closest;

bool selected_keep_row(int i, closest flag) {
  bool is_sel = selected_find(i);
  bool is_g0 = ps[0].w >= ps[1].w;
  bool is_g1 = ps[0].w < ps[1].w;
  return !(flag & CLOSEST_SKIP_SELECTED && is_sel ||
           flag & CLOSEST_ONLY_SELECTED && !is_sel ||
           flag & CLOSEST_SKIP_G0 && is_g0 || flag & CLOSEST_SKIP_G1 && is_g1);
}

int closestToRay(Ray r, float *distance, closest flag) {
  float dmax = INFINITY, d;
  int i = 0, imax = -1;
  advance_ps_reset();
  while (advance_ps()) {
    if (selected_keep_row(i, flag)) {
      d = DistanceToRay(r, Vector4To3(ps[0]), Vector4To3(ps[1]));
      if (d < dmax) {
        imax = i;
        dmax = d;
      }
    }
    i++;
  }
  if (distance)
    *distance = dmax;
  return imax;
}

void write_csv(char *path, closest flag) {
  FILE *h = fopen(path, "w");

  fprintf(h, "x,y,z,e,x2,y2,z2,e2,isel\n");
  advance_ps_reset();
  int i = 0;
  while (advance_ps()) {
    if (selected_keep_row(i, flag))
      fprintf(h, "%f,%f,%f,%f,%f,%f,%f,%f,%d\n", ps[0].x, ps[0].y, ps[0].z,
              ps[0].w, ps[1].x, ps[1].y, ps[1].z, ps[1].w,
              (int)selected_index(i));
    i++;
  }
  fclose(h);
}

int main(int argc, char **argv) {
  static char csvout[NFILENAME + 1] = "gcodeviewer_out.csv";
  static char csvselected[NFILENAME + 1] = "gcodeviewer_selected.csv";

  {
    char *csvprefix = getenv("CSV_PREFIX");
    if (csvprefix) {
      csvout[0] = csvselected[0] = '\0';
      if (strlen(csvprefix) + strlen("selected.csv") > NFILENAME) {
        fprintf(
            stderr,
            "CSV_PREFIX too long: main.c `#define NFILENAME %d` is too low\n",
            NFILENAME);
        exit(-1);
      }
      strncat(csvout, csvprefix, NFILENAME);
      strncat(csvselected, csvprefix, NFILENAME);
      strncat(csvout, "out.csv", NFILENAME);
      strncat(csvselected, "selected.csv", NFILENAME);
    }
  }

  if (argc == 1 || (argc >= 2 && (0 == strcmp(argv[1], "-h") ||
                                  0 == strcmp(argv[1], "--help")))) {
    printf("usage: %s file.gcode\n", argv[0]);
    printf("\n\tq ESC quit\n\tLEFT MOUSE DRAG rotates the view\n"
           "\tRIGHT MOUSE DRAG pans the view\n"
           "\tMOUSE WHEEL DRAG zooms the view\n"
           "\tSPACE adds the segment closest to the mouse to the selection\n"
           "\tALT-SPACE toggles selection of the segment closest to the mouse\n"
           "\tBACKSPACE removes the segment closest to the mouse from the "
           "selection\n"
           "\n\tThe  selection has a different rendering style"
           "\n\tand is saved to csv files %s and %s\n"
           "\n\t`CSV_PREFIX=abc_ %s` saves abc_out.csv and "
           "abc_selected.csv instead\n"
           "\n\tcsv files have columns x,y,z,e, x2,y2,z2,e2, isel"
           "\n\t  where xyze are coordinates of the start points and xyze2 are "
           "the end"
           "\n\t  and isel 0 is the first selected point, -1 is not selected\n",
           csvout, csvselected, argv[0]);

    exit(0);
  }
  selected_init();
  mmapfile(argv[1]);
  write_csv(csvout, 0);
  write_csv(csvselected, CLOSEST_ONLY_SELECTED);
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

  static bool once = false;
  while (!WindowShouldClose() && !IsKeyPressed(KEY_Q) &&
         !IsKeyPressed(KEY_ESCAPE)) {
    {
      static int n = 0;
      n++;
      n = n % 20;
      if (n && mmapfile(argv[1])) // check mtime and reload if needed
        goto redraw;
    }

    // The mouse buttons are already used for navigation.
    // The usual way is to shift-click, ctrl-click or toggle a mode somehow.
    // But here it's simpler because keyboard keys directly use mouse
    // coordinates. Mouse forward and back buttons should do something,
    // but MAX_MOUSE_BUTTONS is 8 and depending on my setup, xev either
    // gives mouse buttons 8 and 9, or with xdotool I produce
    // keycode 117 (keysym 0xff56, Next)
    // keycode 112 (keysym 0xff55, Prior)
    //
    // Left-alt-space toggles
    // backspace deletes
    // space adds
    {
      bool back = IsKeyPressed(KEY_BACKSPACE) ||
                  IsMouseButtonPressed(MOUSE_BUTTON_BACK);
      bool space =
          IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_FORWARD);
      bool alt = IsKeyDown(KEY_LEFT_ALT);
      if (back || space) {
        Vector2 p = GetMousePosition();
        Ray r = GetScreenToWorldRay(p, camera);
        closest flag = back  ? CLOSEST_ONLY_SELECTED
                       : alt ? 0
                             : CLOSEST_SKIP_SELECTED;
        int i = closestToRay(r, NULL, flag);
        if (back)
          selected_remove(i);
        else if (!selected_find(i))
          selected_add(i);
        else if (alt)
          selected_remove(i);
        write_csv(csvselected, CLOSEST_ONLY_SELECTED);
        goto redraw;
      };
    }

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
      // rotate
      UpdateCamera(&camera, CAMERA_THIRD_PERSON);
      if (Vector2LengthSqr(GetMouseDelta()) > 0)
        goto redraw;
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
      goto redraw;
    }
    {
      // zoom
      float f = GetMouseWheelMove();
      camera.fovy = Clamp(camera.fovy / (1 + f / 6) - 7 * f, 20, 120);
      if (fabsf(f) > 0)
        goto redraw;
    }

    // Recreate render target on window resize
    if (IsWindowResized()) {
      goto redraw;
    }

    BeginDrawing();
    EndDrawing();
    continue;

  redraw:
    // Rebuild the cached texture
    BeginDrawing();
    ClearBackground(BLACK);
    BeginMode3D(camera);

    advance_ps_reset();
    int j = 0;
    while (advance_ps()) {
      Color c = ps[0].w < ps[1].w ? BLUE : YELLOW;
      if (selected_find(j)) {
        DrawCapsule(Vector4To3(ps[0]), Vector4To3(ps[1]), 1, 10, 10, c);

      } else
        DrawLine3D(Vector4To3(ps[0]), Vector4To3(ps[1]), c);
      j++;
    }
    EndMode3D();
    EndDrawing();
  }
  CloseWindow();
}
