#include "draw.h"

#define MAX_HP  100.f
#define MIN_HP  0.f

#define HIGH_HEALTH_MARK 80.f
#define LOW_HEALTH_MARK  40.f

#define HEALTHBAR_WIDTH   75.f
#define HEALTHBAR_HEIGHT  10.f
#define HEALTHBAR_PADDING 10.f

int numPlayersPerTeam = 5;
float allyHPs[5]  = {75.f, 33.f, 81.f, 5.f, 100.f};
float enemyHPs[5] = {75.f, 33.f, 81.f, 5.f, 100.f};

void GL::setupOrtho(void)
{
  /* save current state */
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glPushMatrix();

  /* get the viewport info */
  glGetIntegerv(GL_VIEWPORT, GL::viewport);

  /* set the viewport */
  glViewport(/* x */ 0,
             /* y */ 0,
             /* width */ GL::viewport[2],
             /* height */ GL::viewport[3]);

  /* make future matrix operations apply to projection matrix stack */
  glMatrixMode(GL_PROJECTION);

  /* replace projection matrix with identity matrix */
  glLoadIdentity();

  /* define the frustum */
  glOrtho(/* left */ 0,
          /* right */ GL::viewport[2],
          /* top */ GL::viewport[3],
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

void GL::drawHealthBar(float x, float y, float width, float height,
                        float hp, bool isAlly)
{
  float fullBarDisplacementY;
  float playerPercHP;

  /* init vars */
  fullBarDisplacementY = y + (height / 2.);
  playerPercHP = width * (hp / MAX_HP);

  /* color change won't work if textures are enabled */
  glDisable(GL_TEXTURE_2D);

  /* set line width */
  glLineWidth(height);

  /* draw health bar inner line */
  glBegin(GL_LINE_STRIP);

  /* set the current color */
  if (hp > HIGH_HEALTH_MARK)
    glColor3fv(Colors::green);
  else if (hp < LOW_HEALTH_MARK)
    glColor3fv(Colors::red);
  else
    glColor3fv(Colors::orange);

  /* set the vertex data */
  glVertex2f(x, fullBarDisplacementY);
  glVertex2f(x + playerPercHP, fullBarDisplacementY);

  /* finish drawing the health bar */
  glEnd();

  /* set line width */
  glLineWidth(height / 5.);

  /* draw health bar outline */
  glBegin(GL_LINE_STRIP);

  /* set the current color */
  if (isAlly)
    glColor3fv(Colors::white);
  else
    glColor3fv(Colors::black);

  /* set the vertex data */
  glVertex2f(x, y);
  glVertex2f(x + width, y);
  glVertex2f(x + width, y + height);
  glVertex2f(x, y + height);
  glVertex2f(x, y);

  /* finish drawing the outline of the health bar */
  glEnd();

  /* re-enable textures */
  glEnable(GL_TEXTURE_2D);
}

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
  glXSwapBuffersPtr fptr;
  float x, y;
  int i;

  /* get initial x, y values */
  x = GL::viewport[2] - HEALTHBAR_WIDTH - HEALTHBAR_PADDING;
  y = 150.f;

  /* save current state and do GL setup */
  GL::setupOrtho();

  /* draw ally hp bars */
  for (i = 0; i < numPlayersPerTeam; i++) {
    GL::drawHealthBar(x, y, HEALTHBAR_WIDTH, HEALTHBAR_HEIGHT,
                      allyHPs[i], /* isAlly */ true);
    y += HEALTHBAR_PADDING * 2;
  }

  /* add a small gap for better visuals */
  y += HEALTHBAR_PADDING * 2;

  /* draw enemy hp bars */
  for (i = 0; i < numPlayersPerTeam; i++) {
    GL::drawHealthBar(x, y, HEALTHBAR_WIDTH, HEALTHBAR_HEIGHT,
                      enemyHPs[i], /* isAlly */ false);
    y += HEALTHBAR_PADDING * 2;
  }

  /* restore saved GL state */
  GL::restoreGL();

  /* call the hooked function */
  fptr = (glXSwapBuffersPtr)dlsym(RTLD_NEXT, "glXSwapBuffers");
  return fptr(dpy, drawable);
}
