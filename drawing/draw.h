#pragma once
#include <dlfcn.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <iostream>

namespace Colors
{
  const GLfloat black[3]  = {0.004f, 0.004f, 0.004f};
  const GLfloat white[3]  = {1.f, 0.992f, 0.98f};
  const GLfloat red[3]    = {1.f, 0.051f, 0.051f};
  const GLfloat green[3]  = {0.412f, 0.702f, 0.298f};
  const GLfloat orange[3] = {1.f, 0.557f, 0.082f};
  const GLfloat blue[3]   = {0.012f, 0.145f, 0.298f};
}

namespace GL
{
  GLint viewport[4];

  void setupOrtho(void);
  void restoreGL(void);

  void drawHealthBar(float x, float y, float width, float height,
                      float hp, bool isAlly);


}

typedef void (*glXSwapBuffersPtr)(Display *, GLXDrawable);
void glXSwapBuffers(Display *dpy, GLXDrawable drawable);
