#include "draw.h"

#define MAX_HP  100.f

int num_players = 2;
char player_hps[32] = {75, 33};

void GL::setupOrtho(void)
{
  GLint viewport[4];

  /* save current state */
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glPushMatrix();

  /* get the viewport info */
  glGetIntegerv(GL_VIEWPORT, viewport);

  /* set the viewport */
  glViewport(/* x */ 0,
             /* y */ 0,
             /* width */ viewport[2],
             /* height */ viewport[3]);

  /* make future matrix operations apply to projection matrix stack */
  glMatrixMode(GL_PROJECTION);

  /* replace projection matrix with identity matrix */
  glLoadIdentity();

  /* define the frustum */
  glOrtho(/* left */ 0,
          /* right */ viewport[2],
          /* top */ viewport[3],
          /* bottom */ 0,
          /* near */ -1,
          /* far */ 1);

  /* make future matrix operations apply to the model view matrix stack */
  glMatrixMode(GL_MODELVIEW);

  /* replace model view matrix with identity matrix */
  glLoadIdentity();

  /* disable depth tests so our drawings are always visible on the screen */
  glDisable(GL_DEPTH_TEST);
}

void GL::restoreGL(void) { /* restore the state */ 
  glPopMatrix(); 
  glPopAttrib();
  glEnable(GL_DEPTH_TEST);
}

void GL::drawRect(float x, float y, float width, float height, int hp)
{
  float full_bar_displacement_y = y + (height / 2.);
  // TODO read this from somewhere
  float player_hp_perc = width * ((float) hp / MAX_HP);
  /* color change won't work if textures are enabled */
  glDisable(GL_TEXTURE_2D);

  /* treat the next four vertices as a quadrilateral */
  glLineWidth(height);
  glBegin(GL_LINE_STRIP);

  /* set the current color */
  glColor3f(1.0, 0.0, 0.0);

  /* set the vertex data */
  glVertex2f(x, full_bar_displacement_y);
  glVertex2f(x + player_hp_perc, full_bar_displacement_y);

  /* finish drawing the health bar */
  glEnd();

  glLineWidth(height / 5.);
  glBegin(GL_LINE_STRIP);

  /* set the current color */
  glColor3f(1.0, 0.0, 0.0);

  /* set the vertex data */
  glVertex2f(x, y);
  glVertex2f(x + width, y);
  glVertex2f(x + width, y + height);
  glVertex2f(x, y + height);

  /* finish drawing the outline of the health bar */
  glEnd();

  

  /* re-enable textures */
  glEnable(GL_TEXTURE_2D);
}

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
  glXSwapBuffersPtr fptr;

  GL::setupOrtho();
  // TODO figure these out based on the window size
  int cur_y = 100;
  float width = 75.f;
  float height = 10.f;
  for (int i = 0; i < num_players; i++) {
    GL::drawRect(100, cur_y, width, height, player_hps[i]);
    cur_y += height * 2;
  }
  GL::restoreGL();

  fptr = (glXSwapBuffersPtr)dlsym(RTLD_NEXT, "glXSwapBuffers");
  return fptr(dpy, drawable);
}
